# Задача: библиотека Firmir (C++17, без Qt)

Автономный цикл: написать → собрать → запустить → отладить, до идеала.

## Ограничения
- C++17, только stdlib + Linux API (epoll, eventfd, timerfd). Никаких внешних зависимостей, никаких слов «Qt».
- Без moc/кодогенерации; слоты = std::function.
- GCC>=9/Clang>=10, `-Wall -Wextra -Werror`; прогнать ASan+UBSan и TSan.
- PIMPL для всех трёх классов: в заголовке только `class Impl; std::unique_ptr<Impl> d;` (плюс объявления методов). Публичные заголовки не тянут sys/*, <thread>, <mutex>, <atomic>.

## Структура (ровно 3 папки + корневой CMakeLists.txt)
```
lib/include/firmir/{object,thread,timer}.hpp
lib/src/*.cpp + lib/src/detail/*.hpp (внутренние)
example-app/main.cpp
tests/ (свой мини-фреймворк, без GTest)
```
Цели: firmir, example-app, tests. Опция FIRMIR_BUILD_TESTS=ON.

## Классы (namespace Firmir)
- **Object**: иерархия parent/children (рекурсивное удаление, дитя создаётся в потоке родителя), копирование запрещено; сигналы по имени + std::function, connect/disconnect, RAII-хэндл (Connection/ScopedConnection), режимы Direct/Queued/Auto, потокобезопасно; Event, event(), postEvent (thread-safe), sendEvent, фильтры; startTimer/killTimer (через Timer) + timerEvent; setProperty/property/hasProperty (std::any); setName/name/findChild/findChildren; thread()/moveToThread (инварианты: без parent, без children, только из потока-владельца, Thread не перемещается; default = Thread::mainThread()); deleteLater() в очередь потока-владельца; прямой delete из чужого потока запрещён.
- **Thread**: start/quit/exit/wait/isRunning/isFinished; setup/run/cleanup, run() по умолчанию = exec(); exec() возвращает код; static currentThread()/mainThread(); moveToThread запрещён. Цикл событий на epoll + eventfd (пробуждение), задачи FIFO под мьютексом, дренируются в потоке-владельце. Thread = управляющий объект (живёт в потоке-создателе).
- **Timer**: setInterval/interval, setSingleShot/isSingleShot, isActive, start()/start(ms)/stop(); сигнал "timeout" через Object::connect. Реализация: timerfd в epoll потока-владельца; start/stop из чужого потока — через Queued-задачу; singleShot авто-стоп. Без отдельных std::thread.

## Тесты
владение/автоудаление; Direct/Queued/Auto; moveToThread; postEvent порядок FIFO; deleteLater из чужого потока; Timer single/periodic + stop из чужого потока; main exec/quit; свойства; стресс N потоков × M сигналов (TSan-чисто). ctest, ненулевой код при провале.

## example-app
Thread + Object-наследник (moveToThread), Queued-запуск работы, сигнал результата в главный поток, singleShot Timer-страж. Завершение ~1 с, exit 0.

## Финал: отчёт
дерево файлов; команды сборки/тестов; сводка (0 ошибок/предупреждений, ctest N/N, ASan/UBSan/TSan чисто, example exit 0); архитектура (PIMPL, epoll+eventfd, timerfd, std::function-сигналы, thread affinity, FIFO); подтверждение отсутствия внешних зависимостей.

## Запрещено
TODO/FIXME/заглушки; останавливаться на ошибке; спрашивать разрешения; внешние библиотеки; Application/String/ByteArray/File и пр.; GUI/сеть/БД; мета-объектная система; копирование кода Qt.
