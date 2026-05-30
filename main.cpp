/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <event2/event.h>

// Глобальный event_base (наш event loop)
static struct event_base *base = NULL;

// Таймер 1: срабатывает каждую секунду
static void timer1_callback(evutil_socket_t fd, short events, void *arg) {
    static int counter = 0;
    counter++;
    printf("[Таймер 1] Сработал %d раз(а)\n", counter);
}

// Таймер 2: срабатывает каждые 3 секунды
static void timer2_callback(evutil_socket_t fd, short events, void *arg) {
    static int counter = 0;
    counter++;
    printf("[Таймер 2] Сработал %d раз(а) (каждые 3 сек)\n", counter);
    
    // После 5 срабатываний останавливаем этот таймер
    if (counter >= 5) {
        printf("[Таймер 2] Останавливаем после 5 срабатываний\n");
        struct event *ev = (struct event*)arg;
        event_del(ev);
    }
}

// Таймер 3: однократный, сработает через 7 секунд
static void timer3_callback(evutil_socket_t fd, short events, void *arg) {
    printf("[Таймер 3] ОДНОКРАТНЫЙ: прошло 7 секунд!\n");
    printf("[Таймер 3] Можно выключать приложение...\n");
    
    // Останавливаем event loop через 2 секунды (чтобы увидеть последние тики таймера 1)
    struct timeval tv = {2, 0};
    event_base_loopexit(base, &tv);
}

// Обработчик Ctrl+C
static void signal_callback(evutil_socket_t sig, short events, void *arg) {
    printf("\n[Сигнал] Получен Ctrl+C, завершаем работу...\n");
    event_base_loopbreak(base);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    printf("=== Простой event loop с таймерами на libevent ===\n\n");
    
    // 1. Создаем event loop
    base = event_base_new();
    if (!base) {
        fprintf(stderr, "Ошибка: не удалось создать event_base\n");
        return 1;
    }
    
    // 2. Создаем периодические таймеры
    struct event *timer1 = event_new(base, -1, EV_PERSIST, timer1_callback, NULL);
    struct timeval tv1 = {1, 0};  // 1 секунда
    event_add(timer1, &tv1);
    printf("✓ Таймер 1 запущен: интервал 1 секунда\n");
    
    struct event *timer2 = event_new(base, -1, EV_PERSIST, timer2_callback, NULL);
    struct timeval tv2 = {3, 0};  // 3 секунды
    event_add(timer2, &tv2);
    printf("✓ Таймер 2 запущен: интервал 3 секунды (остановится после 5 раз)\n");
    
    // 3. Создаем однократный таймер
    struct event *timer3 = event_new(base, -1, 0, timer3_callback, NULL);
    struct timeval tv3 = {7, 0};  // 7 секунд
    event_add(timer3, &tv3);
    printf("✓ Таймер 3 запущен: однократный через 7 секунд\n");
    
    // 4. Устанавливаем обработчик Ctrl+C
    struct event *signal_event = evsignal_new(base, SIGINT, signal_callback, NULL);
    event_add(signal_event, NULL);
    printf("✓ Обработчик Ctrl+C установлен\n\n");
    
    // 5. Запускаем event loop
    printf("Запуск event loop...\n");
    printf("----------------------------------------\n");
    
    event_base_dispatch(base);  // Блокируется до event_base_loopbreak или event_base_loopexit
    
    // 6. Очистка ресурсов
    printf("----------------------------------------\n");
    printf("Очистка ресурсов...\n");
    
    event_free(timer1);
    event_free(timer2);
    event_free(timer3);
    event_free(signal_event);
    event_base_free(base);
    
    printf("Программа завершена\n");
    return 0;
}