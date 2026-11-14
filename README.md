# Firmir

Firmir — C++17 фреймворк. Ядро использует стандартную библиотеку
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
| Inter | `fonts-inter` | шрифт по умолчанию (опционально; без него fontconfig подставляет замену) |
| Изображения | `libpng-dev`, `libjpeg-dev` | Gui::Image |
| dbus-daemon | `dbus` | приватная сессионная шина в тестах и примере D-Bus (без него D-Bus-тесты пропускаются) |
| setarch | `util-linux` | запуск TSan-сборки с отключённым ASLR |
| linux/input.h | `linux-libc-dev` | GUI-плагин evdev (опционально) |
| drm/drm.h | `libdrm-dev` | GUI-плагин drm и kms-интеграция eglfs (опционально) |
| wayland-client/server | `libwayland-dev` | firmir-wayland-client/server/compositor и GUI-плагин |
| xkbcommon | `libxkbcommon-dev` | keymap в `firmir-wayland-compositor` (`setDefaultKeymap()`) и GUI-плагин wayland |
| wayland-scanner + protocols | `libwayland-bin`, `wayland-protocols` | C/C++ protocol generation, xdg-shell/viewporter/decoration/output |
| lcov + genhtml | `lcov` | отчёт о покрытии (см. «Тестовое покрытие») |
| gcovr | — | альтернативный отчёт о покрытии (опционально) |

## Структура

```
libs/
  core/         firmir-core     — Object, Thread, Timer, String, ByteArray,
                                  Variant, Settings, RegExp, Log, Future,
                                  файлы/потоки, JsonValue/JsonDocument,
                                  XmlElement/XmlDocument, Translator (trId),
                                  Locale (язык системы, бэкенд-плагины)
  dbus/         firmir-dbus     — DBusConnection + утилита firmir-dbus-tool
  networking/   firmir-networking — NetworkAccessManager/NetworkReply (OpenSSL)
  gui/          firmir-gui      — Screen, Window, Item, Text, Painter, Platform,
                                   Font/FontDatabase, Image/Flickable/ShaderEffect/MouseArea
  gui-controls/ firmir-gui-controls — Button, selection controls, views, indicators
  wayland/      firmir-wayland-client/server/compositor + C++ protocol generator
  render/       firmir-render   — абстракция примитивного рендеринга поверх GPU API
plugins/gui/    подгружаемые бэкенды firmir-gui-<имя>.so (headless, evdev, drm, wayland,
                eglfs + его device-интеграции в plugins/gui/eglfs/<имя>/)
plugins/render/ подгружаемые рендер-бэкенды firmir-render-<имя>.so
                (null, soft, opengl, vulkan)
plugins/core/   бэкенды сервисов firmir-core (locale/test: системный язык)
tools/doc/      firmir-doc      — генератор Markdown из ///-комментариев
tools/firml/    firmlc          — транспайлер Firml (.fml) → C++ (лексер/парсер, semantic
                analysis, кодогенерация: статические свойства, anchors, привязки
                через propertyChanged, `behavior on` → дерево анимаций;
                snapshot-тесты + e2e); cmake/FirmlCodegen.cmake: firml_add_target() —
                компиляция .fml → C++ во время сборки
examples/       firmir-example-{модуль}-{имя}: core-basics, json-document,
                xml-document, dbus-echo, networking-fetch, render-triangle,
                gui-animation, gui-calculator (калькулятор целиком на Firml:
                окно, клавиатура 4×5 через Grid + Repeater + ListModel,
                состояние и логика в .fml), gui-views-menu (меню видов на
                ListModel + Repeater, переключение — FML-обработчики),
                gui-controls-gallery (Controls Gallery: главное меню и
                страницы всех элементов; размер — треть экрана, кнопки
                minimize/maximize/close. Запуск:
                FIRMIR_GUI_DISPLAY=wayland ./build/examples/gui/controls-gallery/firmir-example-gui-controls-gallery
                renderer: FIRMIR_RENDER_BACKEND=soft|opengl|vulkan),
                wayland-compositor
tests/          собственный мини-фреймворк, один бинарник tests
```

Рендеринг GUI идёт через firmir-render: сцена элементов записывается в
командный буфер и исполняется выбранным backend (`FIRMIR_RENDER_BACKEND`;
по умолчанию — первый из `vulkan`, `opengl`, `soft`, который загрузился и
смог создать device; если плагин загрузился, но не инициализировался —
например, EGL без display — берётся следующий). `opengl`
использует EGL/GLES2/FBO/glReadPixels; `vulkan` — SPIR-V pipelines,
buffers/images/descriptors/render passes и readback в `CpuRenderTarget`.
Display backends получают одинаковый ARGB-кадр.

`Gui::Text` по умолчанию рендерится нативным стеком (Fontconfig/FreeType/
HarfBuzz) со шрифтом **Inter** (при отсутствии Inter fontconfig подставляет
замену); встроенный font8x8 остаётся fallback, когда нативный стек
недоступен. `FontDatabase` перечисляет семейства и регистрирует TTF/OTF из
файла или памяти. Поддерживаются
`font.family`, `font.pixelSize`, `font.pointSize`, bold/italic/underline/strikeout.

`firmir-gui-controls` содержит Button, CheckBox, RadioButton, Switch, ScrollView,
SplitView, StackView, SwipeView, TabBar, BusyIndicator и ProgressBar. Gallery в
`examples/gui/controls-gallery` демонстрирует их вместе с Flickable, Image,
ShaderEffect и MouseArea. Для headless smoke-проверки: `FIRMIR_GALLERY_HEADLESS=1`,
`FIRMIR_GALLERY_PAGE=0..12`, `FIRMIR_GALLERY_AUTO_QUIT=1`.

Базовая `firmir-gui` дополнительно содержит позиционеры `Column`/`Row`/`Grid`
(автораскладка детей, делегаты Repeater разворачиваются на месте), модель
`ListModel` (строки роль→значение поверх `AbstractDataList`) и фабрику
`Repeater`; в Firml они доступны как `Column`/`Row`/`Grid`/`ListModel` +
`ListElement`/`Repeater` (в делегате — `model.<роль>` и `index`).

Wayland разделён на `firmir-wayland-client`, `firmir-wayland-server` и
`firmir-wayland-compositor`. Используется только базовая libwayland, без
wlroots. `tools/wayland/firmir-wayland-generator.py` создаёт C++ proxy/resource
классы из XML. GUI backend использует client Display/Registry/dispatch API.
Пример собственного сервера:

```sh
./build/examples/wayland/firmir-example-wayland-compositor
# В другом терминале подставьте напечатанный socket:
WAYLAND_DISPLAY=wayland-0 FIRMIR_GUI_DISPLAY=wayland \
  FIRMIR_RENDER_BACKEND=vulkan \
  ./build/examples/gui/controls-gallery/firmir-example-gui-controls-gallery
```

Сокет создаётся в `/run/display/wayland-0` (каталог создаётся сам — на голой
KMS-консоли `XDG_RUNTIME_DIR` обычно не задан); если имя занято живым
композитором, берётся `wayland-1`, `wayland-2` и т.д. Переопределение:
`FIRMIR_WAYLAND_SOCKET_DIR=<каталог>`, `FIRMIR_WAYLAND_SOCKET=<имя или
абсолютный путь>`. `addSocketAuto()` возвращает путь (абсолютный, если сокет
лежит вне `XDG_RUNTIME_DIR`), поэтому клиенту достаточно подставить вывод:

```sh
WAYLAND_DISPLAY=/run/display/wayland-0 FIRMIR_GUI_DISPLAY=wayland <приложение>
```

`WaylandGuiItem` (в `firmir-wayland-compositor`) сам пересылает мышь, палец
(`wl_touch`) и клавиши в `wl_pointer`/`wl_touch`/`wl_keyboard` своего клиента:
достаточно `setView()` + `setSeat()`, а нажатие/фокус приходят сигналом
`signalActivated`. GUI-плагин wayland принимает `wl_touch` (down/up/motion/
cancel) и отдаёт его как `TouchEvent`, поэтому кнопки клиента работают и от
пальца.

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
| `FIRMIR_DRM_HEADERS` | `auto`, `on`, `off` | `auto` | заголовки DRM/KMS (`drm/drm.h`, `drm/drm_mode.h`) для плагина `drm` и kms-интеграции `eglfs`: `auto` — искать в системе |

Все библиотеки собираются как shared (`libfirmir-*.so`) в
`build/libs/<модуль>/`, исполняемые файлы — в `build/examples/<имя>/`,
`build/tests/`, `build/tools/doc/`, GUI-плагины — в `build/plugins/`.

Наличие опциональных заголовков (`drm/drm.h`, `linux/input.h`) определяется
поиском по файловой системе, без `check_include_file_cxx()`: тот кэширует
результат в `CMakeCache.txt` (поставили `libdrm-dev` после первой настройки —
cmake продолжает считать, что заголовка нет) и линкует тестовую программу,
что в scratchbox2/cross-окружениях может падать по причинам, не связанным с
самим заголовком. Ищутся `drm/drm.h` + `drm/drm_mode.h` в include-каталогах
`pkg-config` (`libdrm.pc`), затем в `/usr/include`, `/usr/include/libdrm`,
`/usr/local/include` и в `${CMAKE_SYSROOT}/usr/include`; найденный
нестандартный каталог добавляется плагинам как `-isystem`. Заголовки нужны
только uapi-шные: на Ubuntu/Sailfish они приходят из `linux-libc-dev`
(`/usr/include/drm/`), на Debian-подобных — из `libdrm-dev`
(`/usr/include/libdrm/`). Если автоопределение в вашем окружении не видит
целевой sysroot, сборку можно форсировать: `-DFIRMIR_DRM_HEADERS=on`
(или `off`, чтобы явно пропустить `drm`/`eglfs-kms`), а дополнительные
каталоги подсказать через `-DFIRMIR_HEADER_SEARCH_ROOTS=/путь/к/include`.
Итог поиска печатается при настройке: `drm/drm.h: found in …`.

Заголовки ядра — обычный C, и не каждая их копия компилируется из C++:
в `drm/drm.h` до версии ядра 4.0 структура `drm_buf_map` объявляет поле
с именем `virtual` (upstream позже спрятал его в `#ifdef __cplusplus` и
переименовал в `virt`), поэтому `#include <drm/drm.h>` из C++ падает с
`expected unqualified-id before 'virtual'`. В firmir этот заголовок
включается только через обёртку `firmir/gui/detail/drmuapi.hpp`: она
на время include маскирует ключевое слово
(`#define virtual firmir_drm_uapi_virtual_member`) и сразу снимает макрос,
так что структуры
и номера ioctl остаются ровно такими, какими их объявила системная
копия (firmir `drm_buf_map` не использует вовсе). Новый код должен
включать `<firmir/gui/detail/drmuapi.hpp>`, а не `<drm/...>` напрямую;
регрессия проверяется тестом `firmir/gui/drmuapi_old_header`, который
собирается против фикстуры `tests/fixtures/old_drm_header/` с таким
старым заголовком.

## Установка

```sh
cmake --install build            # префикс по умолчанию: /usr
# или в другое место:
cmake --install build --prefix /opt/firmir
```

Раскладка относительно префикса:

| Что | Куда |
|---|---|
| библиотеки `libfirmir-*.so` | `lib/` |
| примеры `firmir-example-*` | `bin/` |
| заголовки | `include/firmir/<модуль>/` |
| GUI-бэкенды `firmir-gui-*.so` | `lib/firmir/plugins/gui/` |
| device-интеграции eglfs | `lib/firmir/plugins/gui/eglfs/` |
| render-бэкенды `firmir-render-*.so` | `lib/firmir/plugins/render/` |
| core-бэкенды `firmir-<сервис>-*.so` | `lib/firmir/plugins/core/` |
| pkg-config `firmir-<модуль>.pc` | `lib/pkgconfig/` |

Установленные бинарники находят плагины в системном каталоге без
переменных окружения; pkg-config:

```sh
pkg-config --cflags --libs firmir-gui
```

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
./build/examples/core/firmir-example-core-basics
./build/examples/json/firmir-example-json-document
./build/examples/xml/firmir-example-xml-document
./build/examples/dbus/firmir-example-dbus-echo        # поднимает приватный dbus-daemon --session
./build/examples/networking/firmir-example-networking-fetch https://example.com
./build/examples/render/firmir-example-render-triangle  # записывает indexed-triangle draw через render-бэкенд
```

Первые четыре примера завершаются с кодом 0 и могут использоваться как
smoke-тесты. `firmir-example-networking-fetch` требует доступа в сеть и
принимает URL аргументом.

GUI-пример находит плагины в `build/plugins/` автоматически:

```sh
./build/examples/gui/firmir-example-gui-animation
```

Display-бэкенд по умолчанию выбирается перебором списка
`wayland,eglfs,drm,evdev,headless`: `wayland`, если есть композитор, к которому
можно подключиться, иначе `eglfs`, иначе `drm` (оба — при наличии
`/dev/dri/cardN`, input-бэкендом к ним идёт `evdev`), иначе `headless`.
На headless-бэкенде экранов нет — они создаются программно через control API
плагина (так делают тесты), поэтому без реального дисплея пример завершится
с кодом 1 и сообщением «no screens». Явный выбор бэкендов:

```sh
FIRMIR_GUI_PLUGIN_PATH=build/plugins \  # пути поиска firmir-gui-<имя>.so, через ':'
FIRMIR_GUI_BACKENDS=drm,eglfs \         # список бэкендов, которые нужно попробовать, по порядку
FIRMIR_GUI_DISPLAY=drm \                # закрепить display-бэкенд (перекрывает FIRMIR_GUI_BACKENDS)
FIRMIR_GUI_INPUT=evdev \                # закрепить input-бэкенд (перекрывает FIRMIR_GUI_BACKENDS)
./build/examples/gui/firmir-example-gui-animation
```

`FIRMIR_GUI_BACKENDS` — список имён через запятую (пробелы и регистр не важны,
повторы игнорируются); по умолчанию
`wayland,eglfs,drm,evdev,headless`. Display-бэкендами могут быть `headless`,
`wayland`, `drm`, `eglfs`, input-бэкендами — `headless`, `wayland`
(Wayland-плагин даёт и ввод) и `evdev`. Первое display-имя, чей плагин
загрузился, становится display-бэкендом, первое input-имя — input-бэкендом;
имена «не своей роли» игнорируются. Если бэкенд заведомо недоступен (у
`wayland` нет сокета, к которому можно подключиться, у `drm`/`eglfs` нет
`/dev/dri/cardN`, плагина нет в build-дереве), он пропускается с
предупреждением `display backend skipped (unavailable)` и пробуется следующий
из списка; если не подошёл никто — ошибка
`firmir-gui: no usable display backend in FIRMIR_GUI_BACKENDS=…`.

`evdev` — исключение: он не display-бэкенд, а input-бэкенд, который работает
*рядом* с аппаратными `drm`/`eglfs`, поэтому для них он пробуется всегда, даже
если его нет в списке (и идёт первым). Для `wayland` естественная пара —
собственный input-бэкенд Wayland-плагина, для `headless` — его собственный
headless-ввод (события реальных устройств без экрана не имеют смысла).
`FIRMIR_GUI_DISPLAY`/`FIRMIR_GUI_INPUT` закрепляют ровно один бэкенд (без
fallback'а и без проверки устройства) и перекрывают список. Неизвестное имя в
списке даёт предупреждение `unknown backend in FIRMIR_GUI_BACKENDS`.

### evdev

Input-бэкенд `evdev` читает `/dev/input/event*` напрямую (raw ioctl'ы, без
libinput) и следит за hotplug через inotify. Диагностика включается переменной
`FIRMIR_EVDEV_DEBUG`:

```sh
FIRMIR_EVDEV_DEBUG=1 ./app       # список устройств: путь, name, phys, тип
                                 # (touchscreen/mouse/keyboard), оси и их диапазоны
FIRMIR_EVDEV_DEBUG=events ./app  # ... и каждое прочитанное событие
```

Строка на устройство выглядит так:

```
[gui.evdev] /dev/input/event2 "Goodix Capacitive TouchScreen" phys=... \
    type touchscreen EV_KEY,EV_ABS abs-x 53 [0..1199] abs-y 54 [0..1999] \
    resolution 12 13 direct multi-touch btn-touch
```

Тачскрин отдаёт координаты в своих единицах, а не в пикселях, поэтому бэкенд
масштабирует их на размер основного экрана (его сообщает Platform через
`InputBackend::setScreenSize`) и при необходимости поворачивает/зеркалит.
Диапазоны берутся из `EVIOCGABS`. Ориентация по умолчанию — `auto`: если
соотношение сторон дигитайзера не совпадает с экраном (портретный экран и
landscape-дигитайзер), оси меняются местами, что и даёт «тачскрин повёрнут на
90°». Остальное задаётся явно:

```sh
FIRMIR_EVDEV_TOUCH_ROTATION=auto|0|90|180|270   # default auto
FIRMIR_EVDEV_TOUCH_FLIP=none|x|y|both           # default none
FIRMIR_EVDEV_TOUCH_DEVICE=/dev/input/event2     # только этот узел — дигитайзер
```

Что получилось, видно в логе при первом касании (и при смене размера экрана):

```
[gui.evdev] /dev/input/event2 "Goodix ..." touch x[0..1199] y[0..1999] \
    mapped to 1200 2000 rotation 0 (auto) - if taps land elsewhere, set ...
```

Если после смены ориентации нажатия зеркальны, помогает
`FIRMIR_EVDEV_TOUCH_FLIP=x|y|both`, если повёрнуты на 90° в другую сторону —
`FIRMIR_EVDEV_TOUCH_ROTATION=270`. Геометрия отображения считается в
`plugins/gui/evdev/touchmapping.hpp` и покрыта `tests/gui/test_evdev_touch.cpp`.

### eglfs

`eglfs` — полноэкранный бэкенд без композитора: сам плагин
(`firmir-gui-eglfs.so`) только маршрутизирует экраны и окна, а с устройством
работает device-интеграция `firmir-gui-eglfs-<имя>.so`. Интеграция выбирается
переменной `FIRMIR_EGLFS_INTEGRATION` (по умолчанию `kms`); имя нормализуется,
поэтому `kms`, `eglfs_kms` и `EGLFS-KMS` равнозначны. Сейчас есть `kms`
(KMS через raw DRM ioctl + GBM/EGL/GLES2), в планах — `kms-egldevice` и `emu`.
Ищутся интеграции в `FIRMIR_EGLFS_PLUGIN_PATH`, а по умолчанию — в подкаталоге
`eglfs/` каталогов `FIRMIR_GUI_PLUGIN_PATH` (build tree: `build/plugins/eglfs`).
Input-бэкенд — `evdev`, Platform подхватывает его автоматически.

```sh
FIRMIR_GUI_DISPLAY=eglfs \
FIRMIR_EGLFS_INTEGRATION=kms \                  # default (eglfs_kms == kms)
FIRMIR_EGLFS_PLUGIN_PATH=build/plugins/eglfs \  # поиск firmir-gui-eglfs-<имя>.so, через ':'
FIRMIR_EGLFS_KMS_DEVICE=/dev/dri/card0 \        # default; альтернатива — FIRMIR_DRM_DEVICE
./build/examples/gui/firmir-example-gui-animation
```

Интеграция `kms` требует DRM master (свободная VT или CAP_SYS_ADMIN). Размер
окна не «растягивается»: окно получает размер вывода — при показе бэкенд
сообщает окну размер режима (`output size WxH given to the window`), окно
меняет геометрию, и приложение (Firml-bindings на `width`/`height`) раскладывает
контент под него. Кадр рисуется 1:1. Если приложение всё же просит другой
размер (`Window::resize()`), окно его сохраняет: при наличии режима ровно
такого размера вывод переключается на него (`window size WxH - switched the
output mode to match`), иначе кадр рисуется 1:1 по центру вывода (`drawn 1:1 at
...`, поля чёрные), а кадр больше вывода — уменьшается до вписывания с
сохранением пропорций. `FIRMIR_GUI_KMS_SCALING=stretch` возвращает прежнее
поведение (растянуть кадр на весь вывод). Геометрия окна → режим/позиция
считается в `firmir/gui/detail/kmslayout.hpp` (`kmsFindMode()`,
`kmsFrameRect()`), покрыто `tests/gui/test_kms_layout.cpp`.
ABI подплагинов описан в `<firmir/gui/eglfsintegration.hpp>`: интеграция
экспортирует `firmir_eglfs_create_integration()` и
`firmir_eglfs_integration_abi()`.

Если экран не находится, `gui.eglfs.kms` пишет причину: `cannot become DRM
master` (занят другой программой — нужна свободная VT), `cannot enumerate KMS
outputs` (ioctl вернул ошибку) или `no usable KMS output` — со списком
коннекторов (`connector-<id> connected/disconnected/unknown`, число режимов и
энкодеров). Перечисление идёт через `firmir/gui/detail/drmenumeration.hpp`:
kernel требует буфер под каждое ненулевое поле `count_*` (включая `count_fbs`
и `count_props`), иначе второй вызов `GETRESOURCES`/`GETCONNECTOR` возвращает
`EFAULT` и выводов «нет».

`FIRMIR_RENDER_PLUGIN_PATH` (по умолчанию `build/plugins/render`), выбор —
`FIRMIR_RENDER_BACKEND` (по умолчанию — первый из `vulkan`, `opengl`,
`soft`, который загрузился и смог создать device).

Бэкенд `opengl` выбирает EGL-дисплей под окружение: при наличии
`DISPLAY`/`WAYLAND_DISPLAY` — обычный default display, иначе GPU берётся через
DRM-узел (платформа gbm, затем device) или surfaceless. Поэтому он
инициализируется и на голой консоли (eglfs), где
`eglInitialize(EGL_DEFAULT_DISPLAY)` падает с `XDG_RUNTIME_DIR is invalid or
not set`. Выбранный вариант виден в логе: `render.opengl: EGL display: <...>`.

### Ввод: указатель и тач

Мышь и тач идут одним путём: input-бэкенд → `Platform::routeInput` →
`Window::dispatchMouse`/`Window::dispatchTouch` → item под точкой и вверх по
родителям, пока кто-нибудь не сделает `event.accepted = true`.

Нажатие (мышиный `Press` или touch `Begin`) забирает указатель: элемент,
принявший его, получает все последующие `Move`/`Update` и `Release`/`End` —
даже если палец или курсор уже вышел за его границы. То же на уровне окна:
`Release`/`End` вне геометрии окна не теряется, а уходит в окно, принявшее
нажатие. Поэтому кнопка не остаётся «зажатой», если отпустить палец в стороне:
`pressed` сбрасывается, клик не срабатывает (release был вне кнопки), а
подсветка нажатия снимается сразу, как только палец ушёл с кнопки.

Для тача учитывается `TouchPoint::id`: второй палец обслуживается сам по себе
и не отбирает захват у первого (в окне хранится один захваченный контакт).
Захват живёт до `End`/`Release` или до удаления item'а.

### Анимации

Все анимации потока тикают от одного общего такта (`AnimationFrameDriver`,
интервал 8 мс), а не от таймера на каждую анимацию. Такт открывает «батч»
обновлений сцены: окно просят отрисовать один раз, когда все свойства такта
уже записаны. Без этого `x` и `y`, анимируемые двумя отдельными анимациями,
попадали в разные кадры — диагональное движение выглядело лесенкой
(дёргалось). Такт намеренно чаще частоты обновления экрана (60–120 Гц):
значения считаются от стенных часов, поэтому каждый показанный кадр берёт
свежую позицию, а более редкий такт давал бы кадры с двойным шагом.
Длительность анимации при этом точная: прогресс = прошло / duration.

## Тестовое покрытие

`ctest` выставляет `LD_LIBRARY_PATH` на каталоги библиотек и плагинов в
build-дереве (`cmake/FirmirTestEnv.cmake`) — на случай, если загрузчик в
песочнице сборки (scratchbox2, qemu-сборки пакетов) не учитывает пути, зашитые
в сами бинарники.

Тестовые бинарники при этом самодостаточны: в них зашиты пути до всех
каталогов с библиотеками и плагинами build-дерева (`DT_RPATH`,
`--disable-new-dtags`) — и абсолютные, и относительные от самого бинарника
(`$ORIGIN/../libs/...`, `BUILD_RPATH_USE_ORIGIN`). Поэтому запускать можно без
окружения, из любого текущего каталога и даже если build-дерево в рантайме
лежит по другому абсолютному пути, чем при конфигурации:

```sh
./build/tests/tests
```

Тесты, которым нужны системные ресурсы, деградируют в «skip», а не в провал:
`gui_native_font_and_database` ищет файл шрифта по путям популярных
дистрибутивов, затем рекурсивно в `/usr/share/fonts` (переопределяется
`FIRMIR_TEST_FONT=/path/to/font.ttf`), а при полном отсутствии шрифтов в
build-root'е (rpmbuild, scratchbox, минимальные контейнеры) пропускает проверки
метрик и `addApplicationFont()` с сообщением `[ SKIP ]`. Так же
`socket_notifier_exception_oob`: `MSG_OOB` на `socketpair(AF_UNIX)` принимают не
все ядра и не все эмуляторы, поэтому транспорт проверяется пробой, при отказе
используется пара loopback TCP, а если окружение вообще не сообщает об urgent
данных (`epoll` не выставляет `EPOLLPRI`), кейс пропускается — молчание
нотификатора там, где ядро событие отдаёт, остаётся провалом.

Сборка gcov-инструментированной конфигурации в `build-cov`, прогон тестов
и отчёт (требуются lcov и genhtml):

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

## Полная проверка

Каждая конфигурация собирается и тестируется прямыми вызовами cmake/ctest
(см. разделы «Сборка», «Запуск тестов», «Санитайзеры», «Тестовое покрытие»):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Компоненты выбираются опциями `FIRMIR_BUILD_LIB_*`, `FIRMIR_BUILD_PLUGIN_*`,
`FIRMIR_BUILD_EXAMPLES`, `FIRMIR_BUILD_DOC_TOOL`, `FIRMIR_BUILD_TOOL_FIRML`;
если библиотека не собирается, её тесты и примеры тоже пропускаются.

## Генерация документации

```sh
./build/tools/doc/firmir-doc libs build/doc
```

Генерирует Markdown из `///`-комментариев (`\class`, `\brief`, `\param`,
`\return`) всех заголовков в `libs/` и кладёт результат в `build/doc/`.
