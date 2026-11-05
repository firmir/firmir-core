# Firmir

Firmir — C++17 фреймворк в духе Qt. Ядро использует стандартную библиотеку
и Linux API (epoll, eventfd, timerfd); модули расширения подключают OpenSSL,
Vulkan/OpenGL, FreeType/Fontconfig/HarfBuzz, PNG/JPEG и базовую libwayland.

## Требования

| Зависимость | Версия / пакет | Назначение |
|---|---|---|
| CMake | ≥ 3.16 | система сборки |
| GCC | ≥ 9 (`g++`) или Clang ≥ 10 | C++17, `-Wall -Wextra -Werror` |
| pthreads | в составе glibc | `Threads::Threads` |
| OpenSSL | `libssl-dev` | модуль networking (HTTPS) |
| EGL + GLES2 | `libegl1-mesa-dev`, `libgles2-mesa-dev` | OpenGL render-плагин (опционально) |
| Vulkan + SPIR-V | `libvulkan-dev`, `glslang-tools`, `libshaderc-dev`, `mesa-vulkan-drivers` | Vulkan renderer и ShaderEffect |
| Шрифты | `libfreetype-dev`, `libfontconfig1-dev`, `libharfbuzz-dev` | Font, FontDatabase, Unicode Text |
| Изображения | `libpng-dev`, `libjpeg-dev` | Gui::Image |
| dbus-daemon | `dbus` | приватная сессионная шина в тестах и примере D-Bus (без него D-Bus-тесты пропускаются) |
| setarch | `util-linux` | запуск TSan-сборки с отключённым ASLR |
| linux/input.h | `linux-libc-dev` | GUI-плагин evdev (опционально) |
| drm/drm.h | `libdrm-dev` | GUI-плагин drm (опционально) |
| wayland-client/server | `libwayland-dev` | firmir-wayland-client/server/compositor и GUI-плагин |
| wayland-scanner + protocols | `libwayland-bin`, `wayland-protocols` | C/C++ protocol generation, xdg-shell/viewporter/decoration/output |
| lcov + genhtml | `lcov` | отчёт о покрытии (`setup.sh --coverage`) |
| gcovr | — | альтернативный отчёт о покрытии (опционально) |

## Структура

```
libs/
  core/         firmir-core     — Object, Thread, Timer, String, ByteArray,
                                  Variant, Settings, RegExp, Log, Future, файлы/потоки
  json/         firmir-json     — JsonValue/JsonDocument
  xml/          firmir-xml      — XmlElement/XmlDocument
  dbus/         firmir-dbus     — DBusConnection + утилита firmir-dbus-tool
  networking/   firmir-networking — NetworkAccessManager/NetworkReply (OpenSSL)
  gui/          firmir-gui      — Screen, Window, Item, Text, Painter, Platform,
                                   Font/FontDatabase, Image/Flickable/ShaderEffect/MouseArea
  gui-controls/ firmir-gui-controls — Button, selection controls, views, indicators
  wayland/      firmir-wayland-client/server/compositor + C++ protocol generator
  render/       firmir-render   — абстракция примитивного рендеринга поверх GPU API
plugins/gui/    подгружаемые бэкенды firmir-gui-<имя>.so (headless, evdev, drm, wayland)
plugins/render/ подгружаемые рендер-бэкенды firmir-render-<имя>.so
                (null, soft, opengl, vulkan)
tools/doc/      firmir-doc      — генератор Markdown из ///-комментариев
tools/firml/    firmlc          — транспайлер Firml (.fml) → C++ (лексер/парсер, semantic
                analysis, кодогенерация: статические свойства, anchors, привязки
                через propertyChanged, `behavior on` → дерево анимаций;
                snapshot-тесты + e2e); cmake/FirmlCodegen.cmake: firml_add_target() —
                компиляция .fml → C++ во время сборки
apps/           red-window      — Controls Gallery: главное меню и страницы всех
                элементов; размер — треть экрана, кнопки minimize/maximize/close.
                Запуск: FIRMIR_GUI_DISPLAY=wayland ./build/apps/red-window/red-window
                (renderer: FIRMIR_RENDER_BACKEND=soft|opengl|vulkan)
examples/       example-{core,json,xml,dbus,networking,render}, example_gui
tests/          собственный мини-фреймворк, один бинарник tests
setup.sh        сборка и CTest; опционально ASan/TSan/coverage и выбор компонентов
```

Рендеринг GUI идёт через firmir-render: сцена элементов записывается в
командный буфер и исполняется выбранным backend (`FIRMIR_RENDER_BACKEND`,
по умолчанию `soft`). `opengl` использует EGL/GLES2/FBO/glReadPixels; `vulkan` —
SPIR-V pipelines, buffers/images/descriptors/render passes и readback в
`CpuRenderTarget`. Display backends получают одинаковый ARGB-кадр.

`Gui::Text` сохраняет встроенный font8x8 для совместимости, а после назначения
`Gui::Font` использует Fontconfig/FreeType/HarfBuzz. `FontDatabase` перечисляет
семейства и регистрирует TTF/OTF из файла или памяти. Поддерживаются
`font.family`, `font.pixelSize`, `font.pointSize`, bold/italic/underline/strikeout.

`firmir-gui-controls` содержит Button, CheckBox, RadioButton, Switch, ScrollView,
SplitView, StackView, SwipeView, TabBar, BusyIndicator и ProgressBar. Gallery в
`apps/red-window` демонстрирует их вместе с Flickable, Image, ShaderEffect и
MouseArea. Для headless smoke-проверки: `FIRMIR_GALLERY_HEADLESS=1`,
`FIRMIR_GALLERY_PAGE=0..12`, `FIRMIR_GALLERY_AUTO_QUIT=1`.

Wayland разделён на `firmir-wayland-client`, `firmir-wayland-server` и
`firmir-wayland-compositor`. Используется только базовая libwayland, без Qt и
wlroots. `tools/wayland/firmir-wayland-generator.py` создаёт C++ proxy/resource
классы из XML. GUI backend использует client Display/Registry/dispatch API.
Пример собственного сервера:

```sh
./build/examples/wayland/example_wayland_compositor
# В другом терминале подставьте напечатанный socket:
WAYLAND_DISPLAY=wayland-0 FIRMIR_GUI_DISPLAY=wayland \
  FIRMIR_RENDER_BACKEND=vulkan ./build/apps/red-window/red-window
```

Для неблокирующего D-Bus вызова используйте `DBusConnection::asyncCall()`:
возвращённый `DBusMessage*` принадлежит соединению, а в его сигнале
`DBusMessage::signalFinished()` объект уже содержит method-return или error.
Ответ читается через `SocketNotifier` в цикле событий потока соединения.
`NetworkAccessManager::setRequestMode()` выбирает `WorkerThread` (умолчание)
или `CurrentThread` с чтением ответа через `SocketNotifier`;
`NetworkReply::signalFinished()` испускается в обоих режимах.

## Лицензия

MIT (см. [LICENSE](LICENSE)). Встроенный шрифт font8x8 — Public Domain.

## Сборка

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

Опции CMake:

| Опция | Значения | По умолчанию | Описание |
|---|---|---|---|
| `FIRMIR_BUILD_TESTS` | `ON`/`OFF` | `ON` | собирать тестовый бинарник и включать CTest |
| `FIRMIR_BUILD_EXAMPLES` | `ON`/`OFF` | `ON` | собирать примеры |
| `FIRMIR_BUILD_DOC_TOOL` | `ON`/`OFF` | `ON` | собирать `firmir-doc` |
| `FIRMIR_BUILD_TOOL_FIRML` | `ON`/`OFF` | `ON` | собирать транспайлер `firmlc` |
| `FIRMIR_BUILD_LIB_*` | `ON`/`OFF` | `ON` | выбор библиотек (`CORE`, `JSON`, `XML`, `DBUS`, `NETWORKING`, `GUI`, `RENDER`) |
| `FIRMIR_BUILD_PLUGIN_GUI_*` | `ON`/`OFF` | `ON` | выбор GUI-плагинов (`DRM`, `EVDEV`, `HEADLESS`) |
| `FIRMIR_BUILD_PLUGIN_RENDER_NULL` | `ON`/`OFF` | `ON` | null render-бэкенд для тестов и fallback |
| `FIRMIR_BUILD_PLUGIN_RENDER_SOFT` | `ON`/`OFF` | `ON` | software render-бэкенд |
| `FIRMIR_BUILD_PLUGIN_RENDER_OPENGL` | `ON`/`OFF` | `ON` | OpenGL ES 2 бэкенд (пропускается без EGL/GLES2) |
| `FIRMIR_SANITIZER` | `""`, `address`, `thread` | `""` | санитайзер: ASan+UBSan или TSan |

Все библиотеки собираются как shared (`libfirmir-*.so`) в
`build/libs/<модуль>/`, исполняемые файлы — в `build/examples/<имя>/`,
`build/tests/`, `build/tools/doc/`, GUI-плагины — в `build/plugins/`.

## Запуск тестов

```sh
ctest --test-dir build --output-on-failure
```

CTest регистрирует тесты:

- `firmir.tests` — основной бинарник `build/tests/tests` (собственный
  мини-фреймворк, 142 кейса; ненулевой код возврата при первом провале);
- `firmir-doc` — smoke-тест генератора документации на фикстуре
  `tools/doc/testdata/`;
- `firmlc.<case>` / `firmlc.sema.<case>` / `firmlc.gen.<case>` — snapshot-тесты
  транспайлера Firml: для каждой фикстуры `tools/firml/testdata/<case>.fml`
  stdout сверяется с `<case>.ast` (только парсер), `<case>.sema` (парсер +
  semantic analysis, с `-I testdata/include`) или `<case>.gen` (генерация
  C++), для `err_*` — stderr и ненулевой код возврата с `<case>.err`, для
  warning-кейсов — stderr с `<case>.warn`.
- `firmlc.e2e.app` — end-to-end: firmlc генерирует C++ из
  `tools/firml/testdata/e2e/app.fml`, код компилируется против настоящей
  firmir-gui и запускается (проверяет anchors на «живых» изменениях).
- `firmlc.handlers.app` — generated signal/property handlers, typed arguments,
  `using`, singleton `Firmir.Gui.App` и нажатие Button через headless input.

Бинарник можно запустить и напрямую:

```sh
./build/tests/tests
```

## Запуск примеров

```sh
./build/examples/core/example-core
./build/examples/json/example-json
./build/examples/xml/example-xml
./build/examples/dbus/example-dbus        # поднимает приватный dbus-daemon --session
./build/examples/networking/example-networking https://example.com
./build/examples/render/example-render    # записывает indexed-triangle draw через render-бэкенд (default: null)
```

Первые четыре примера завершаются с кодом 0 и могут использоваться как
smoke-тесты. `example-networking` требует доступа в сеть и принимает URL
аргументом.

GUI-пример рассчитан на реальный бэкенд и находит плагины в
`build/plugins/` автоматически:

```sh
FIRMIR_GUI_DISPLAY=drm FIRMIR_GUI_INPUT=evdev ./build/examples/gui/example_gui
```

На headless-бэкенде (умолчание) экранов нет — они создаются программно через
control API плагина (так делают тесты), поэтому `example_gui` без реального
дисплея завершится с кодом 1 и сообщением «no screens». Выбор бэкендов:

```sh
FIRMIR_GUI_PLUGIN_PATH=build/plugins \  # пути поиска firmir-gui-<имя>.so, через ':'
FIRMIR_GUI_DISPLAY=drm \                # display-бэкенд (default: headless)
FIRMIR_GUI_INPUT=evdev \                # input-бэкенд (default: из display-плагина)
./build/examples/gui/example_gui
```

Рендер-бэкенды ищутся аналогично: `firmir-render-<имя>.so` в
`FIRMIR_RENDER_PLUGIN_PATH` (по умолчанию `build/plugins/render`), выбор —
`FIRMIR_RENDER_BACKEND` (по умолчанию `null`).

## Тестовое покрытие

Один шаг — опция `--coverage` у setup.sh (требуются lcov и genhtml):

```sh
./setup.sh --coverage
```

Скрипт собирает gcov-инструментированную конфигурацию в `build-cov`,
прогоняет тесты и пишет:

- `build-cov/coverage.info` — tracefile lcov (без `/usr/*` и `tests/*`);
- `build-cov/coverage-html/index.html` — HTML-отчёт.

Тот же результат вручную:

```sh
cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -fprofile-update=atomic" \
      -DCMAKE_EXE_LINKER_FLAGS=--coverage
cmake --build build-cov -j"$(nproc)"
find build-cov -name '*.gcda' -delete   # сброс счётчиков прошлых прогонов
ctest --test-dir build-cov --output-on-failure

lcov --capture --directory build-cov --output-file build-cov/coverage.info \
     --ignore-errors mismatch,negative,gcov
lcov --remove build-cov/coverage.info '/usr/*' '*/tests/*' \
     --output-file build-cov/coverage.info
genhtml build-cov/coverage.info --output-directory build-cov/coverage-html
lcov --summary build-cov/coverage.info
```

Замечания по флагам:

- `-fprofile-update=atomic` — обязателен: тесты многопоточные, иначе в
  `.gcda` появляются отрицательные счётчики;
- `find ... -delete` перед ctest — старые `.gcda` конфликтуют с новыми
  `.gcno` («stamp mismatch») и обрывают capture;
- `--ignore-errors mismatch,negative,gcov` — обход известных проблем
  lcov 2.0 + GCC 13 («mismatched end line» и предупреждения на STL-заголовках).

Быстрая сводка по строкам без lcov, средствами gcov:

```sh
cd build-cov
find . -name '*.gcno' -exec gcov -n {} + 2>/dev/null | \
    grep -E "^File|Lines executed"
```

Альтернативный HTML-отчёт через gcovr (`sudo apt install gcovr` /
`pip install gcovr`):

```sh
gcovr --root . --exclude tests --html-details coverage.html build-cov
```

## Санитайзеры

```sh
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build-asan -DFIRMIR_SANITIZER=address
cmake --build build-asan -j"$(nproc)"
ctest --test-dir build-asan --output-on-failure

# ThreadSanitizer (тесты автоматически оборачиваются в `setarch -R`,
# т.к. TSan несовместим с ASLR на части ядер)
cmake -S . -B build-tsan -DFIRMIR_SANITIZER=thread
cmake --build build-tsan -j"$(nproc)"
ctest --test-dir build-tsan --output-on-failure
```

## Полная проверка одной командой

```sh
./setup.sh [-j N] [--keep-going] [--all-tests]
```

По умолчанию собирается только plain-конфигурация и запускается CTest.
`--asan` и `--tsan` включают соответствующие sanitizer-сборки, `--coverage`
собирает покрытие, а `--all-tests` последовательно выполняет plain, ASan,
TSan и coverage. Компоненты выбираются флагами `--lib-*`, `--plugin-*`,
`--no-examples`, `--no-doc-generate` и `--no-doc-build`; если библиотека не
собирается, её тесты, примеры и документация тоже пропускаются. Скрипт
завершается ненулевым кодом при первой ошибке (`--keep-going` — продолжить
после неё) и печатает сводку PASS/FAIL.

## Генерация документации

```sh
./build/tools/doc/firmir-doc libs build/doc
```

Генерирует Markdown из `///`-комментариев (`\class`, `\brief`, `\param`,
`\return`) всех заголовков в `libs/` и кладёт результат в `build/doc/`.
`setup.sh` генерирует документацию только для выбранных библиотек в
`build/doc/<библиотека>/` (отключается `--no-doc-generate`).
