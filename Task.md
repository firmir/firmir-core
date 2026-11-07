# Firmir — архитектура и план работ

Firmir — C++17 фреймворк без внешних зависимостей
(только stdlib + Linux API; OpenSSL — только для HTTPS в модуле networking).

## Структура

```
libs/
  core/         Object, Thread, Timer, EventLoop, String, ByteArray,
                Variant, Settings, RegExp, Log, Concurrent, файлы/потоки
  json/         JsonValue, JsonArray, JsonObject, JsonDocument
  xml/          XmlElement, XmlDocument
  dbus/         DBusConnection + утилита firmir-dbus-tool
  networking/   NetworkAccessManager, NetworkRequest, NetworkReply
  gui/          Screen, Window, Item (2D scene graph), Painter, Platform
plugins/
  gui/          подгружаемые бэкенды firmir-gui-<имя>.so:
    headless/   виртуальные экраны/окна + инъекция ввода (тесты)
    evdev/      ввод из /dev/input (клавиатура/мышь/тач), inotify-хотплаг
    drm/        DRM/KMS через raw ioctl: перечисление коннекторов,
                dumb buffer + modeset, опрос хотплага раз в секунду
tools/
  doc/          firmir-doc — генератор Markdown из ///-комментариев
  firml/        firmlc — транспайлер Firml (.fml) → C++ (ручной лексер/парсер,
                без внешних инструментов; snapshot-тесты на ctest)
examples/       firmir-example-{модуль}-{имя} (core-basics, json-document,
                xml-document, dbus-echo, networking-fetch, render-triangle,
                gui-animation, gui-calculator, wayland-compositor)
tests/          собственный мини-фреймворк, один бинарник firmir.tests
```

## Архитектурные решения

- **PIMPL** для всех публичных классов: в заголовке только
  `class Impl; std::unique_ptr<Impl> d;`. Публичные заголовки не тянут
  `sys/*`, `<thread>`, `<mutex>`, `<atomic>`.
- **Сигналы** — `std::function`, connect/disconnect по имени, режимы
  Direct/Queued/Auto, RAII-хэндлы `Connection`/`ScopedConnection`.
- **Свойства** — у каждого `Object`: зарегистрированные нативные свойства
  (`registerProperty` в конструкторе, чтение/запись через
  `setProperty`/`getProperty` с мягкой конверсией типов, сигнал
  `propertyChanged(name, value)` при реальном изменении, метаданные через
  `propertyMetaList`) + динамический `std::any`-bag для незарегистрированных
  имён. Сеттеры сами зовут `notifyPropertyChanged`; `destroyed` эмитится в
  начале деструктора.
- **Anchors** — у `Item`: шесть line-anchors (`AnchorTarget` = item + line;
  цель — parent или sibling), `fill`/`centerIn`, margins/offsets; пересчёт —
  по `propertyChanged` цели и собственным width/height. left+right тянут
  width (top+bottom — height); центры — только без сторон.
- **GUI-бэкенды (плагины .so)** — headless (тесты), evdev, drm и **wayland**:
  wl_compositor/wl_shm/xdg-shell, shm double-buffering, поток dispatch
  (prepare_read/poll), экраны через wl_output, pointer-ввод (wl_seat),
  закрытие окна → quit. Первое приложение: `apps/red-window`.
- **Рендеринг GUI через firmir-render** — Painter записывает команды в
  RenderCommandBuffer; backends: soft (CPU), OpenGL ES 2 (EGL/FBO/readback),
  Vulkan (SPIR-V, render passes/pipelines/descriptors/offscreen readback).
  RGBA-текстуры Text/Image/ShaderEffect живут в RenderResources до разрушения
  device, поэтому один scene graph работает на всех backends.
- **Fonts/controls** — Fontconfig + FreeType + HarfBuzz для native Text;
  FontDatabase поддерживает system/application fonts. Отдельная
  firmir-gui-controls реализует requested QuickControls-подобные классы.
- **Wayland modules** — client (Display/Registry/Proxy + XML→C++ generator),
  server (Display/Client/Resource), compositor (surfaces/outputs/seat/shells,
  xdg-shell/viewporter/decoration/output). GUI backend использует client module.
  Собственный example compositor способен принять gallery и показать shm-кадр.
- **DBus/network async IO** — `DBusConnection::asyncCall` переводит чтение
  bus-fd на SocketNotifier и заменяет pending `DBusMessage` ответом перед
  сигналом `finished()`. `NetworkAccessManager::RequestMode` выбирает worker
  thread или SocketNotifier в потоке менеджера; `NetworkReply::finished()`
  испускается в обоих режимах.
- **Анимации** — иерархия `Gui::Animation` (start/stop, сигнал `finished`):
  `PropertyAnimation`/`NumberAnimation` (числа), `ColorAnimation` (цвет,
  по каналам), `PauseAnimation`, контейнеры `SequentialAnimation` и
  `ParallelAnimation`; easing (Linear…OutExpo); таймер ~60fps в event loop,
  запись через `setPropertyDirect`. **Behaviors** (`Behavior on`):
  `Item::addBehavior(property, tree)` — hook записи `Object::setProperty`
  перехватывает изменения свойства и перезапускает анимацию к новому
  значению (`from` — текущее на момент триггера).
- **Цикл событий** — epoll + eventfd (пробуждение), очередь задач FIFO
  под мьютексом, дренируется в потоке-владельце объекта.
- **Timer** — timerfd в epoll потока-владельца, без отдельных потоков.
- **Networking** — блокирующие `Socket`/`TlsSocket` внутри отсоединённого
  `std::thread`; `NetworkReply::isFinished()` — атомарный опрос
  (`Concurrent::run` не подходит: деструктор `Future` блокируется).
- **Settings** — INI-подобный формат, `setValue/value/remove/sync`.
- **GUI-плагины** — базовая библиотека `firmir-gui` не линкуется с
  бэкендами: `Platform` dlopen()ит `firmir-gui-<имя>.so` из
  `FIRMIR_GUI_PLUGIN_PATH` (или `../plugins` относительно исполняемого
  файла). Выбор: `FIRMIR_GUI_DISPLAY` (default headless), `FIRMIR_GUI_INPUT`
  (default: input-фабрика display-плагина). Плагин может экспортировать
  обе фабрики (`firmir_gui_create_display`, `firmir_gui_create_input`).
  Плагины не линкуют firmir-core (иначе в .so оказалась бы вторая копия
  состояния ядра) — только заголовки `firmir-gui-backend`.
- **GUI-рендер** — у каждого `Window` свой render-поток (`RenderThread`:
  поток с condition_variable, кадры по запросу `update()`, с коалесцингом);
  `Painter` записывает сцену в RenderCommandBuffer (affine transforms,
  clip/scissor, opacity), выбранный backend (`soft` или `opengl`) возвращает
  ARGB32-кадр display-бэкенду через `PlatformWindow::present()`. Дерево
  защищено рекурсивным `sceneMutex`; создание/удаление элементов — через
  `Window::createItem/destroyItem` (ctor базового класса аттачит объект до
  завершения производного ctor, поэтому голый `new T(parent)` для
  присоединённых к показанному окну элементов — гонка).
- **GUI-ввод** — `Platform` маршрутизирует события input-бэкенда: мышь/тач —
  в верхнее видимое окно под точкой, клавиатура — в окно с focusItem;
  внутри окна событие идёт в deepest enabled+visible элемент и всплывает
  по родителям, пока `accepted`. Клик по элементу даёт ему фокус
  (`focusInEvent/focusOutEvent`). Коллбэки бэкендов из чужих потоков
  перебрасываются в GUI-поток через `postEvent`.

## Сборка и проверка

```sh
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
# ASan/UBSan:     cmake -S . -B build-asan -DFIRMIR_SANITIZER=address && ... (см. README)
# TSan:           cmake -S . -B build-tsan -DFIRMIR_SANITIZER=thread && ...
# coverage:       cmake -S . -B build-cov  -DCMAKE_BUILD_TYPE=Debug \
#                 -DCMAKE_CXX_FLAGS="--coverage -fprofile-update=atomic" ... (см. README)
# установка:      cmake --install build   (префикс по умолчанию /usr)
```

## Статус задач

- [x] Object/Thread/Timer/EventLoop — ядро, сигналы, события, таймеры
- [x] Модули json, xml, dbus
- [x] `Settings` (persistent key/value) — INI, sync, чтение read-only файлов
- [x] Модуль networking — HTTP/HTTPS (OpenSSL), выбор detached worker thread
      или SocketNotifier в потоке менеджера (`RequestMode`), `finished()`
- [x] DBus asyncCall — ответ через SocketNotifier и `DBusMessage::finished()`
- [x] Инструмент firmir-doc — Markdown из `///`-комментариев (`\class`,
      `\brief`, `\param`, `\return`), ctest на фикстуре
- [x] Примеры перенесены в `examples/`, библиотеки в `libs/`
- [x] Модуль render — абстракция примитивного рендеринга (buffer/texture/
      shader/pipeline/target/command buffer/swapchain) + null/soft/OpenGL ES 2
- [x] PIMPL для всех публичных классов (проверено: Thread, Timer,
      SocketNotifier, RegExp, Log, DBusConnection, Json*, Xml*)
- [x] Модуль gui — `Screen` (hotplug через сигналы Platform), `Window`
      (render-поток, `frameRendered`), `Item` (геометрия, implicit size,
      childrenRect, opacity, visible, enabled, transforms/clip/z-order,
      focus/activeFocus/policies, mouse/touch/key), плагинные бэкенды display/input
      (headless, evdev, drm; wayland — см. ниже), пример `firmir-example-gui-animation`
- [ ] Транспайлер Firml → C++ (`tools/firml`, бинарь `firmlc`):
      M1 готов — лексер, рекурсивный спуск (объекты, свойства, `#include`,
      anchors обеих форм, animate, выражения с `?:`), AST + dump, CLI.
      M2 готов — резолв `#include`, таблица типов, символы/id, типизация
      выражений, anchors (конфликты/оси/sibling-порядок), граф зависимостей
      с детектом циклов, warning на неиспользуемый include; `--dump-sema`.
      M3 готов — квалифицированные типы `Firmir.Gui.Rectangle` (`.` → `::`),
      кодогенерация C++ (hpp+cpp: `create<Stem>(Item*)` + struct указателей
      по id): статические свойства через `setProperty`, anchors всех форм.
      M4 готов — привязки: начальное вычисление + `connect` на
      `propertyChanged` каждой зависимости (Direct, контекст — владелец
      привязки), чтение через `anyConvert<T>(obj->getProperty("p"))`;
      поддержаны forward-reference на поздние id, self-deps, margin-привязки,
      собственные свойства без `this.`.
      M5 готов — синтаксис `behavior on <prop> { <дерево анимаций> }` вместо
      `animate`: типы NumberAnimation/ColorAnimation/PauseAnimation/
      SequentialAnimation/ParallelAnimation (валидация типов, duration
      `<n>ms`, easing-whitelist, соответствие типа свойства), кодогенерация
      дерева + `addBehavior`. `--dump-gen`, 5 gen-снапшотов + e2e с
      behavior-проверкой.
      M6 готов — `tools/firml/cmake/FirmlCodegen.cmake`: `firml_add_target(
      <t> SOURCES <fml>... [TYPE STATIC|OBJECT] [INCLUDE_DIRS ...])` — build-
      time кодогенерация через firmlc (add_custom_command, зависимости:
      .fml + firmlc + item headers), STATIC/OBJECT-библиотека со сгенериро-
      ванными .cpp, PUBLIC-линк на firmir-gui и include-директория с
      заголовками; покрыто тестом firmlc.m6.app (testdata/m6).
      M7 готов — `using`, typed signal handlers, `on<Property>Changed`,
      method-call commands, `ROOT_DIR`/`--root-dir`, singleton App и Button;
      snapshot `40_handlers` + headless e2e `firmlc.handlers.app`.
      M8 готов — корневой `Window` (`create<Stem>(Screen*)`: окно + App-
      singleton создаются генератором, `show()` в конце), невизуальный
      `Timer` (`interval`/`singleShot`/`running`, сигнал `timeout`, методы
      start/stop), присваивания в обработчиках (`id.prop = expr` — пишут
      через setProperty, поведения анимируют), свойства синглтона
      `Firmir.Gui.App.screenWidth/screenHeight` (read-only, обновляются при
      hotplug экрана); у `Window` зарегистрированные свойства
      `width`/`height`/`title`. Snapshot `50_window_timer` + err-кейсы +
      headless e2e `firmlc.window.app`; пример examples/gui (firmir-example-gui-animation) переписан на Firml.
      M9 готов — объявление своих свойств `property <number|string|bool|
      color> <name>[: <expr>]` (живут в динамическом std::any-bag Object,
      каждая запись шлёт `propertyChanged` → биндинги/обработчики работают
      как у нативных; инициализатор может быть привязкой) и конкатенация
      строк оператором `+` (String+String / String+Number / Number+String,
      числа через `String::number`). Snapshot `60_custom_props` + err-кейсы +
      headless e2e `firmlc.calc.app` (мини-калькулятор, клики через
      `emitSignal("clicked")`, проверка текста дисплея); полноценный
      калькулятор на Firml — `examples/gui/calculator`.
- [x] firmir-wayland-generator переписан на C++ (tools/wayland,
      парсинг XML через firmir-xml, вывод байт-в-байт совместим с
      Python-версией; Python-зависимость сборки удалена).
- [x] Property/anchors/animate рантайм: у `Firmir::Object` зарегистрированные
      свойства (`registerProperty`, конверсия типов, `propertyChanged`,
      `propertyMetaList`, сигнал `destroyed`) + динамический bag; у
      `Firmir::Gui::Item` — anchors (`AnchorLine`/`AnchorTarget`,
      `setAnchor*/fill/centerIn/clearAnchors`, margins/offsets, пересчёт по
      `propertyChanged`), item-типы `Rectangle`/`Text`, `Color::fromString/
      lighter/darker`, `PropertyAnimation` + easing (Linear/InQuad/OutQuad/
      InOutQuad/OutCubic/OutExpo).
- [x] Vulkan offscreen renderer + pixel tests на lavapipe.
- [x] Font/FontDatabase, native arbitrary-font Text, Flickable/ShaderEffect/Image/MouseArea.
- [x] firmir-gui-controls (11 controls) и Controls Gallery со всеми страницами.
- [x] firmir-wayland-client/server/compositor, XML→C++ generator, migrated backend,
      собственный compositor example с реальным first frame клиента.
- [ ] Покрытие тестами 100% (ветки ошибок, граничные случаи)
- [x] Wayland-бэкенд для gui (wl_compositor/wl_shm/xdg-shell, pointer input)
- [ ] DRM-бэкенд: page flip vsync вместо простого memcpy, окна меньше
      экрана (сейчас только fullscreen-поверхность на коннектор)

---

## Приложение: исходное задание (M0)

Автономный цикл: написать → собрать → запустить → отладить, до идеала.

### Ограничения
- C++17, только stdlib + Linux API (epoll, eventfd, timerfd). Никаких внешних
  зависимостей.
- Слоты = std::function, без мета-компилятора.
- GCC>=9/Clang>=10, `-Wall -Wextra -Werror`; прогнать ASan+UBSan и TSan.
- PIMPL для всех трёх классов: в заголовке только `class Impl;
  std::unique_ptr<Impl> d;` (плюс объявления методов). Публичные заголовки не
  тянут sys/*, <thread>, <mutex>, <atomic>.

### Структура (ровно 3 папки + корневой CMakeLists.txt)
```
lib/include/firmir/{object,thread,timer}.hpp
lib/src/*.cpp + lib/src/detail/*.hpp (внутренние)
example-app/main.cpp
tests/ (свой мини-фреймворк)
```
Цели: firmir, example-app, tests. Опция FIRMIR_BUILD_TESTS=ON.

### Классы (namespace Firmir)
- **Object**: иерархия parent/children (рекурсивное удаление, дитя создаётся в
  потоке родителя), копирование запрещено; сигналы по имени + std::function,
  connect/disconnect, RAII-хэндл (Connection/ScopedConnection), режимы
  Direct/Queued/Auto, потокобезопасно; Event, event(), postEvent (thread-safe),
  sendEvent, фильтры; startTimer/killTimer (через Timer) + timerEvent;
  setProperty/property/hasProperty (std::any); setName/name/findChild/
  findChildren; thread()/moveToThread (инварианты: без parent, без children,
  только из потока-владельца, Thread не перемещается; default =
  Thread::mainThread()); deleteLater() в очередь потока-владельца; прямой
  delete из чужого потока запрещён.
- **Thread**: start/quit/exit/wait/isRunning/isFinished; setup/run/cleanup,
  run() по умолчанию = exec(); exec() возвращает код; static
  currentThread()/mainThread(); moveToThread запрещён. Цикл событий на epoll +
  eventfd (пробуждение), задачи FIFO под мьютексом, дренируются в
  потоке-владельце. Thread = управляющий объект (живёт в потоке-создателе).
- **Timer**: setInterval/interval, setSingleShot/isSingleShot, isActive,
  start()/start(ms)/stop(); сигнал "timeout" через Object::connect.
  Реализация: timerfd в epoll потока-владельца; start/stop из чужого потока —
  через Queued-задачу; singleShot авто-стоп. Без отдельных std::thread.

### Тесты
владение/автоудаление; Direct/Queued/Auto; moveToThread; postEvent порядок
FIFO; deleteLater из чужого потока; Timer single/periodic + stop из чужого
потока; main exec/quit; свойства; стресс N потоков × M сигналов (TSan-чисто).
ctest, ненулевой код при провале.

### example-app
Thread + Object-наследник (moveToThread), Queued-запуск работы, сигнал
результата в главный поток, singleShot Timer-страж. Завершение ~1 с, exit 0.

### Запрещено
TODO/FIXME/заглушки; останавливаться на ошибке; спрашивать разрешения;
внешние библиотеки; Application/String/ByteArray/File и пр.; GUI/сеть/БД;
мета-объектная система.
