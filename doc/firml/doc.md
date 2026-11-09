# Язык Firml (.fml)

Firml — декларативный язык описания GUI-дерева фреймворка Firmir (в духе QML),
компилируемый транспайлером `firmlc` (`tools/firml`) в C++17. Один `.fml`-файл
описывает одно дерево объектов; firmlc разбирает его (лексер → парсер →
семантический анализ) и генерирует пару `<stem>.hpp` / `<stem>.cpp` с функцией
`create<Stem>()`, строящей это дерево средствами `firmir-gui`.

Формальная грамматика — `tools/firml/grammar.txt`; этот документ описывает
язык и его семантику в целом.

## Структура файла

```fml
#include <firmir/items/Rectangle.hpp>   // include-директивы — только в начале
#include "common.fml"                    // кавычки: относительно .fml, затем -I
using FG = Firmir.Gui.Rectangle          // алиасы типов (опционально)

Rectangle {                              // ровно один корневой объект
    id: root
    width: 300
}
```

- Точки с запятой после членов объекта опциональны.
- `#include <путь>` (угловые скобки): поиск только в каталогах `-I` (при сборке
  по умолчанию — `libs/gui/include`); `#include "путь"`: сначала относительно
  каталога `.fml`-файла, затем каталоги `-I`.
- Include регистрирует тип по stem'у файла (`Rectangle` для
  `firmir/items/Rectangle.hpp`); заголовок обязан содержать `class <stem>`.
- Включённый тип либо встроенный (полная метаинформация: свойства, сигналы,
  методы), либо *opaque* — объявлен в C++, но метаданных нет: его свойства,
  методы и сигналы не проверяются (warning при использовании). Значения
  opaque-свойств имеют тип `Any`: проходят во все контексты, конвертируются
  в рантайме (`Object::setProperty`/`getProperty`, `detail::anyConvert`) —
  поэтому opaque-классы должны регистрировать свои свойства через
  `registerProperty` (иначе значение попадёт в динамический багаж).
- Повторный include того же типа — ошибка; include, не добавивший ни одного
  используемого типа, — warning.
- `using Alias = Qualified.Type` — алиас обязан ссылаться на включённый тип;
  имя алиаса начинается с заглавной буквы; завершающая `;` опциональна.

## Лексика

- Комментарии: `// строка` и `/* блок */`.
- Идентификаторы: `[A-Za-z_][A-Za-z0-9_]*`. Имена типов и алиасов — с заглавной
  буквы; id, свойства и роли — со строчной.
- Числа: `0`, `12`, `3.14`, `1e3`, `1.5e-2`. Суффикс `ms` (`250ms`) допустим
  только у длительностей анимаций. Отрицательные числа — унарным минусом: `-1`.
- Строки: `"..."` с escapes `\" \\ \n \t \r \0 \xHH \uXXXX`.
- Литералы: `true`, `false`, `undefined` (последний — только как значение
  якоря, т.е. явный сброс).
- Цвета задаются строкой и неявно конвертируются в `color`: `"#RGB"`,
  `"#RRGGBB"`, `"#AARRGGBB"`.

## Типы объектов

Имя типа может быть квалифицированным: `Firmir.Gui.Rectangle` — резолвится по
последнему сегменту, каждый сегмент начинается с заглавной буквы.

Встроенные типы (полная метаинформация):

| Тип | Item-наследник | Свойства (в дополнение к Item) | Методы | Сигналы |
|---|---|---|---|---|
| `Item` | — | (см. ниже) | `update()` | — |
| `Rectangle` | да | `color`, `radius` (радиус скругления углов, пкс; 0 — без скругления), `border.color`, `border.width` (рамка внутри границ; по умолчанию ширина 0) | | |
| `Text` | да | `text`, `color`, `fontSize` | | |
| `Button` | да | `text`, `color`, `textColor`, `fontSize`; `pressed` (ro) | | `clicked`, `pressed`, `released` |
| `Window` | нет | `title`, `width`, `height` | `show()`, `hide()`, `close()` | |
| `Timer` | нет | `interval` (мс), `singleShot`, `running` | `start()`, `stop()` | `timeout` |
| `Column`, `Row` | да | `spacing` | | |
| `Grid` | да | `spacing`, `columns`, `rows` | | |
| `Repeater` | да | (`model` — специальное, см. ниже) | | |
| `ListModel` | нет | `count` (ro) | | |
| `ListElement` | нет | (только роли-литералы) | | |
| `Image` | да | `source`, `fillMode` (строка: `Stretch`, `PreserveAspectFit`, `PreserveAspectCrop`, `Pad`), `smooth` | | |
| `EditableText` | да | `text`, `multiline` | | `accepted` |
| `MouseArea` | да | `hoverEnabled`; `mouseX`, `mouseY`, `pressed`, `containsMouse` (ro) | | `entered`, `exited` (сигналы с аргументами недоступны — используйте `onMouseXChanged`/`onPressedChanged`/...) |
| `Flickable` | да | `contentWidth`, `contentHeight` | | |
| `ShaderEffect` | да | `fragmentShader`, `color` | | |
| `CheckBox` | да | `text`, `checked`, `tristate` | | `clicked` |
| `RadioButton` | да | `text`, `checked` | | `clicked` |
| `Switch` | да | `text`, `checked` | | `clicked` |
| `ProgressBar` | да | `value`, `from`, `to`, `indeterminate` | | |
| `BusyIndicator` | да | `running` | | |
| `ScrollView` | да | `contentWidth`, `contentHeight` | | |
| `StackView` | да | `depth` (ro) | | |
| `SwipeView` | да | `currentIndex`, `interactive` | | |
| `SplitView` | да | `orientation` (`Horizontal`/`Vertical`) | | |
| `TabBar` | да | `currentIndex`; `count` (ro) | `addTab(string)` | |

Дополнительные типы из других библиотек (например, `Firmir.Multimedia.VideoOutput`)
подключаются как *opaque* через `#include` (см. ниже).

Особенности контейнеров: дети `ScrollView`/`Flickable` создаются внутри их
`contentItem()`; дети `StackView`/`SwipeView`/`SplitView` дополнительно
регистрируются методами `push`/`addItem` при создании.

Свойства `Item`: `x`, `y`, `width`, `height`, `implicitWidth`,
`implicitHeight`, `transformOrigin`, `rotation`, `scale`, `opacity`, `z`,
`focusPolicy` (number); `visible`, `enabled`, `clip`, `focus`,
`activeFocusOnTab` (bool); `activeFocus` (bool, ro); `childrenRect` (rect, ro).
`ro` — только чтение.

Синглтон `Firmir.Gui.App` (коротко `App`): read-only свойства `screenWidth`,
`screenHeight` (обновляются при hotplug экрана); методы `show()`, `hide()`,
`quit()`, `setTitle(string)`, а также управление окном для кнопок
`showMinimized()`, `showNormal()`, `showMaximized()`, `toggleMaximized()`,
`close()`.

Синглтон `Firmir.Gui.Theme` (коротко `Theme`): визуальные константы
приложения — цвета (`primaryColor`, `highlightColor`, `errorColor`, ...),
размеры шрифтов (`fontSizeTiny` … `fontSizeHuge`), размеры элементов
(`itemSizeSmall` … `itemSizeHuge`, `iconSize…`, `buttonWidth…`), отступы
(`paddingSmall/Medium/Large`, `horizontalPageMargin`), прозрачности
(`opacityFaint/Low/High`, `highlightBackgroundOpacity`), `pixelRatio`,
`fontFamily`/`fontFamilyHeading` и `colorScheme` (`"lightOnDark"` /
`"darkOnLight"`). Все свойства записываемые: и чтение в привязке
(`color: Theme.highlightColor`), и запись в обработчике
(`onClicked: Theme.highlightColor = "#FFA000"`). Изменения приходят через
`propertyChanged`, привязки переоцениваются, контролы перерисовываются
сами (их paint() читает тему).

Синглтон `Firmir.Core.Translator` (коротко `Translator`; старое имя
`Firmir.Gui.Translator` принимается как алиас): свойство `language`
(тег вроде `"en"`/`"ru"`); методы `setLanguage(string)`,
`addTranslationFile(string path)`, `removeTranslationFile(string path)`.
Файл перевода — JSON: `{"language": "ru", "strings": {"id": "Текст"}}`.
Функция выражений `trId("id")` возвращает перевод id на активном языке (или
сам id, если перевода нет) и зависит от `Translator.language`: при смене
языка или набора файлов все тексты с `trId(...)` обновляются автоматически.

Ограничения по размещению: `Window` — только корневой объект файла; `Timer` и
`ListModel` — невизуальные объекты; `ListElement` — только строка внутри
`ListModel`; `Connections` и `Binding` — только дочерние объекты (не корень),
без `id`, без своих детей и не в делегате `Repeater`.

## Объекты и члены

Виды членов объекта:

- `id: имя` — со строчной буквы, уникально в файле; объект с id доступен в
  выражениях и обработчиках.
- `свойство: выражение` — статическое значение или привязка (см. ниже).
- Вложенный `Type { ... }` — дочерний объект; порядок объявления = порядок
  children (z-order по умолчанию).
- `anchors { ... }` и `anchors.<имя>: <выражение>` — якоря.
- `border.color: <выражение>` и `border.width: <выражение>` — сгруппированные
  свойства рамки (только у `Rectangle`, см. таблицу типов). Любые
  подсвойства с точкой можно задавать группой: `border { width: 2;
  color: "#333" }` эквивалентно `border.width: 2; border.color: "#333"`;
  внутри группы работают выражения и привязки.
- `behavior on <свойство> { <анимация> }` — поведение.
- `property <тип> <имя> [: <выражение>]` — своё динамическое свойство.
- Обработчики `on<Сигнал>` и `on<Свойство>Changed`.

## Выражения

Операторы по убыванию приоритета:

| Операторы | Ассоциативность |
|---|---|
| `.` (доступ к члену), вызов `f(...)` | постфикс |
| `!`, `-` (унарные) | справа |
| `*` `/` `%` | влево |
| `+` `-` | влево |
| `<` `<=` `>` `>=` | влево |
| `==` `!=` | влево |
| `&&` | влево |
| `||` | влево |
| `?:` | вправо |

- Типы значений: `number` (double), `string`, `bool`, `color`, `rect`
  (только чтение `childrenRect`).
- Арифметика и сравнения порядка — над `number`; `+` также конкатенирует
  строки (`string + string`, `string + number`, `number + string`).
- Встроенные функции: `min(a, b)`, `max(a, b)`, `lighter(color, factor)`,
  `darker(color, factor)`, `trId(id)` (перевод строки, см. синглтон
  `Translator` выше).
- Доступ к значениям: `свойство` (своё), `id.свойство`, `parent.свойство`,
  `Firmir.Gui.App.screenWidth`, параметры обработчика; в делегате `Repeater` —
  `model.<роль>` и `index`.
- Скобки `(...)` группируют выражения.

## Привязки

Если выражение свойства ссылается на свойства объектов (через id, parent, свои
имена, App) или на параметры модели, оно становится *привязкой*: значение
вычисляется при создании дерева и пересчитывается при `propertyChanged`
каждой зависимости. Зависимости вычисляются статически; циклические
зависимости — ошибка компиляции. Привязкой может быть и инициализатор своего
свойства.

## Anchors

Две формы — блоком и построчно:

```fml
anchors { left: parent.left; top: header.bottom; leftMargin: 8 }
anchors.centerIn: parent
anchors.fill: parent
```

- Линии: `left`, `right`, `horizontalCenter` (горизонтальная ось) и `top`,
  `bottom`, `verticalCenter` (вертикальная). Значение — `<объект>.<линия>`,
  где объект — `parent` или id sibling'а, объявленного раньше.
- `fill: <объект>` и `centerIn: <объект>` — заполнение и центрирование
  (цель — parent или sibling).
- Отступы (number): `margins`, `leftMargin`, `rightMargin`, `topMargin`,
  `bottomMargin`, `horizontalCenterOffset`, `verticalCenterOffset`.
- Значение `undefined` явно сбрасывает якорь.
- Ограничения: якоря только у Item-наследников и не у корневого `Window`; один
  якорь задаётся один раз; конфликтующие якоря одной оси (например,
  `left` + `horizontalCenter`) — ошибка; якорные цепочки не должны
  образовывать цикл.

## Обработчики сигналов

```fml
onClicked: root.update()                                  // одна команда
onClicked { count = count + 1; root.update() }            // блок команд через ;
onSomeSignal(int code, string message): { root.update() } // типизированные параметры
onWidthChanged: root.update()                             // <свойство>Changed
```

- `on<Сигнал>` — обработчик сигнала типа (у `Button` — `clicked`, `pressed`,
  `released`; у `Timer` — `timeout`). Для известных сигналов проверяется
  арность. Неизвестные имена тоже разрешены — так подключаются динамические
  сигналы, испускаемые из C++ через `emitSignal`; тогда параметры объявляются
  с типами: `int`/`double`/`float`/`string`/`bool`/`color`.
- `on<Свойство>Changed` — обработчик изменения любого свойства (включая свои
  `property`); доступен один параметр — новое значение.
- Команда — вызов метода, вызов своей функции (`id.имя(...)`, см. «Функции»)
  или присваивание; условная логика выражается тернарным оператором `?:`.
- Получатель метода/функции: `id`, `parent`, `Firmir.Gui.App`. Списки методов
  встроенных типов проверяются (`Item.update`, `Window.show/hide/close`,
  `Timer.start/stop`, `App.show/hide/quit/setTitle/showMinimized/showNormal/
  showMaximized/toggleMaximized/close`); у opaque-типов — нет.
- Цель присваивания: `свойство` (своё), `id.свойство`, `parent.свойство`.
  Нельзя присваивать read-only свойства, свойства синглтона и свойства
  `Timer` (используйте `start()`/`stop()`).

## Connections и Binding (QML-стиль)

Языковые конструкции без собственного runtime-объекта: не требуют `#include`
и не попадают в структуру объектов — кодогенератор разворачивает их в обычные
подключения/привязки к цели.

```fml
Connections {
    target: button
    onClicked: { root.count = root.count + 1 }
    onWidthChanged: root.update()          // изменение свойства цели
}

Binding {
    target: box
    property: "width"
    value: root.width / 2                  // отслеживаемая привязка
}
```

- `Connections`: члены — `target: <id>` и обработчики; обработчики
  проверяются против типа цели (сигналы и `<свойство>Changed`), тела видят
  область видимости места объявления (`parent` — родитель `Connections`).
- `Binding`: члены — `target: <id>`, `property: "<имя>"`, `value: <выражение>`.
  Имя свойства проверяется по таблице типа цели (включая свои `property`
  и вложенные вроде `border.width`), read-only свойства запрещены; выражение
  с зависимостями становится отслеживаемой привязкой (переоценка при
  изменении зависимостей), без зависимостей — одноразовой записью.
- Цель может быть объявлена позже по файлу (id видны после первого прохода).
  Нюанс: статические записи одного свойства применяются в порядке их
  появления в файле — объявляйте `Binding` после целевого объекта, чтобы он
  перекрывал статическое значение, заданное в самом объекте.

## Свои свойства

```fml
property number count: 0
property string entry: "0"
property bool editing
```

Типы: `number`, `string`, `bool`, `color`. Живут в динамическом хранилище
`Object` и шлют `propertyChanged`, поэтому работают в привязках и обработчиках
наравне с нативными; инициализатор может быть привязкой. Имя — со строчной
буквы, не `id`, не перекрывает нативное свойство типа, не дублируется.
Нельзя объявлять у `Timer`, `ListModel`, `ListElement` и в делегате
`Repeater`.

## Функции

```fml
function greet(name: string) -> string {
    return "Hello, " + name + "!"
}

function doubleClicks() -> number {
    return clicks * 2          // зависимость: root.clicks
}

function applySize(w: number, h: number) {   // без `-> type` — «void»
    width = w
    height = h
}
```

- Объявляются внутри объекта, у которого есть `id` (получатель вызова);
  вызываются как `id.имя(аргументы)` — в том числе `parent.имя(...)` — из
  привязок и из команд обработчиков.
- Параметры: `имя: тип`, типы — `number`/`string`/`bool`/`color`; тип
  результата после `->`. Функция без `->` ничего не возвращает и вызывается
  только из обработчиков; в выражениях — ошибка.
- Тело — команды как в обработчиках (вызовы методов, присваивания,
  `const x = comp.createObject(parent)`), а для возвращающей функции —
  последняя строка `return <выражение>`. `return` не последним — ошибка.
- Порядок объявления не важен: функции могут вызывать друг друга (рекурсия
  запрещена — обнаруживается при проверке).
- Реактивность: вызов в привязке наследует зависимости return-выражения —
  `x: root.doubleClicks()` перевычисляется при изменении `root.clicks`.
- Имя — со строчной буквы, не перекрывает нативное свойство/метод типа, не
  дублирует своё свойство или функцию. Нельзя объявлять у `Timer`,
  `ListModel`, `ListElement`, `Connections`, `Binding`, `Component` и в
  делегате `Repeater`.
- Компиляция: функции становятся лямбдами `std::function` с именем
  `<id>_<имя>` и захватом `[=]`; сначала объявления всех функций, затем
  определения.

## Поведения (behavior)

```fml
behavior on x { NumberAnimation { duration: 250ms easing: InOutQuad } }
```

Поведение автоматически анимирует каждое изменение свойства (в том числе
присваивания из обработчиков). Типы анимаций:

- `NumberAnimation` — для number-свойств; `ColorAnimation` — для color;
- `PauseAnimation` — пауза (`duration`);
- `SequentialAnimation`, `ParallelAnimation` — контейнеры с вложенными
  анимациями (последовательное/параллельное выполнение).

Свойства анимаций: `duration` — литерал с суффиксом `ms`; `easing` —
идентификатор из списка `Linear`, `InQuad`, `OutQuad`, `InOutQuad`,
`OutCubic`, `OutExpo`; у `NumberAnimation`/`ColorAnimation` — `from`, `to`.
Поведения недоступны в делегатах `Repeater`.

## Позиционеры: Column, Row, Grid

Раскладывают видимых детей в порядке объявления и автоматически пересчитывают
раскладку при добавлении/удалении детей и при изменении `width`, `height` или
`visible` ребёнка. Невидимые дети пропускаются. Делегаты `Repeater`
участвуют в раскладке так, будто объявлены на месте самого `Repeater`.
`implicitWidth`/`implicitHeight` позиционера пересчитываются под содержимое,
поэтому незаданные `width`/`height` принимают размер содержимого.

- `Column` — вертикально, `Row` — горизонтально; зазор между детьми —
  `spacing`.
- `Grid` — сетка: `columns` (по умолчанию 4; 0 — вычисляется по `rows`),
  `rows`; ячейки одинакового размера — по самому крупному ребёнку.

## Модели и Repeater

```fml
ListModel {
    id: tags
    ListElement { label: "alpha"; size: 12 }
    ListElement { label: "beta"; size: 16; badge: "#FF8000" }
}

Column {
    Repeater {
        model: tags            // id ListModel или неотрицательное число
        Button {
            text: model.label  // роль модели
            x: index * 5       // номер экземпляра (0-based)
        }
    }
}
```

- `ListElement` — строка модели; допустим только внутри `ListModel`; значения
  ролей — только литералы: строка, число (включая отрицательные),
  `true`/`false`, цвет-строка `"#…"` (тип color).
- Тип роли выводится по литералам всех строк; конфликт типов одной роли —
  ошибка. Роль, отсутствующая в части строк, при чтении даёт дефолт типа
  (`0`, `""`, `false`, `Color()`).
- `Repeater`: ровно один дочерний объект-делегат; `model` — id `ListModel`
  или неотрицательное число экземпляров. При изменении модели делегаты
  пересоздаются асинхронно (через цикл событий); серия изменений за один
  проход цикла (например, наполнение модели по строкам) коалесцируется в
  одно пересоздание — список не мерцает по разу на строку.
- В делегате доступны `model.<роль>` и `index` (number). Во всём поддереве
  делегата запрещены: `id`, свои `property`, anchors, `behavior`, вложенные
  `Repeater` и `ListModel`.
- Делегаты инстанцируются после инициализации остальных привязок, поэтому
  могут читать свои свойства внешних объектов; объекты, на которые ссылается
  делегат, должны быть объявлены до `Repeater` в файле.
- Когда `model` — `ListModel`, делегаты пересоздаются при изменении
  `count` модели (append/clear из C++): пересоздание откладывается до
  следующей итерации цикла событий, поэтому удаление строки из обработчика
  самого делегата безопасно.

## Component (отложенное создание объектов)

`Component` объявляет шаблон поддерева, который НЕ создаётся при загрузке
файла, а инстанцируется в рантайме вызовом `createObject()` из обработчика:

```fml
Component {
    id: badgeComponent

    Rectangle {
        id: badge
        width: 50
        height: 20
        property string label: "badge"
        Text { id: badgeText; text: "Badge"; anchors.centerIn: parent }
        Button { id: badgeButton; onClicked: badge.label = "clicked" }
    }
}

Button {
    id: spawnButton
    onClicked: {
        const badge1 = badgeComponent.createObject(container)
        badge1.label = "spawned"          // запись свойства созданного объекта
        badgeComponent.createObject(container) // можно и без присваивания
    }
}
```

- У `Component` обязателен `id` и ровно один дочерний объект — корень
  шаблона; своих свойств/обработчиков/якорей у `Component` нет.
- `createObject(parent)` создаёт новое поддерево под указанным родителем
  (id объекта-Item или `parent`; для корня шаблона `parent` — это аргумент
  вызова). Каждый вызов строит независимый экземпляр: свои копии объектов,
  привязок, обработчиков и `id` (id внутри шаблона локальны для него и снаружи
  недоступны).
- `const <имя> = comp.createObject(...)` объявляет локальную константу
  обработчика; дальше с ней можно работать как с объектом: `obj.prop = value`,
  `obj.method(...)`. Тип известен статически (корень шаблона), включая его
  `property`-объявления.
- Ограничения: `Component` нельзя вкладывать в `Component` или в делегат
  `Repeater`; `Repeater` внутри `Component` пока не поддержан. Шаблон
  инстанцируется лениво, поэтому его выражения могут ссылаться на любые
  внешние объекты файла независимо от порядка объявления.
- В C++ объект компонента доступен как `Firmir::Gui::Component*`
  (`<firmir/gui/component.hpp>`) — поле `<id>` в сгенерированной структуре;
  `createObject(Firmir::Gui::Item*)` можно вызывать и напрямую из C++.

## Сводка семантических правил

- Ровно один корневой объект — инстанцируемый тип (Item-наследник, `Window`
  или `Timer`); `App` используется только как синглтон;
  `ListModel`/`ListElement` корнем быть не могут.
- `id`: начинается со строчной буквы, уникален в файле, недоступен в
  делегатах.
- Неизвестное свойство известного типа, присваивание read-only свойства,
  повторное присваивание одного свойства — ошибки (для opaque-типов —
  warning).
- Неявные преобразования типов: `string` → `color`; конкатенация строк с
  числами через `+`.
- Функции: получатель должен иметь `id`, имена уникальны в пределах объекта,
  типы аргументов и результата проверяются, рекурсия запрещена; вызов в
  привязке наследует зависимости return-выражения.
- Циклы привязок и якорных цепочек — ошибки компиляции.

## Компилятор firmlc

```
firmlc [--dump-ast | --dump-sema | --dump-gen] <input.fml> [-I <path>]... [--root-dir <dir>] [-o <dir>]
```

- `--dump-ast` / `--dump-sema` / `--dump-gen` — печать AST, семантического
  резюме или сгенерированного C++ на stdout (взаимоисключающие флаги).
- Без `-o` сгенерированные `.hpp`+`.cpp` печатаются на stdout; с `-o` —
  пишутся в каталог как `<stem>.hpp`/`<stem>.cpp`. С `--root-dir` stem —
  путь относительно корня (заголовок получает расширение `.h`), что
  сохраняет структуру каталогов.
- Коды выхода: 0 — ок (warnings на stderr), 1 — ошибка Firml
  (`файл:строка:колонка` на stderr), 2 — ошибка вызова/IO.

Сгенерированный API: структура `<Stem>Objects` с типизированными указателями
на корень и на каждый объект с `id`, и функция `create<Stem>()`: для
корня-`Window` — `create<Stem>(Firmir::Gui::Screen*)` (окно создаётся и
показывается генератором), для остальных корней —
`create<Stem>(Firmir::Gui::Item* parent)`.

## CMake-интеграция

```cmake
include(<firmir>/tools/firml/cmake/FirmlCodegen.cmake)
firml_add_target(my_codegen
    SOURCES app.fml panel.fml
    [TYPE STATIC|OBJECT]      # по умолчанию STATIC
    [INCLUDE_DIRS <dirs...>]  # дополнительные -I для #include
    [ROOT_DIR <dir>])         # пробрасывается в --root-dir
target_link_libraries(my_app PRIVATE my_codegen)
```

Каждый `.fml` компилируется firmlc'ом во время сборки в
`${CMAKE_CURRENT_BINARY_DIR}/firml_generated/`; этот каталог попадает в public
include-path цели (потребители пишут `#include "app.hpp"`). Цель публично
линкует `firmir-gui`; stem'ы файлов в пределах одной цели должны быть
уникальны.

## Пример

```fml
#include <firmir/items/Window.hpp>
#include <firmir/items/Text.hpp>
#include <firmir/items/Button.hpp>
#include <firmir/items/Column.hpp>

Window {
    id: win
    title: "Counter"
    width: 240
    height: 160
    property number count: 0

    Column {
        anchors.centerIn: parent
        spacing: 8

        Text {
            text: "Count: " + win.count
            fontSize: 20
        }
        Button {
            text: "+1"
            onClicked: win.count = win.count + 1
        }
    }
}
```

Рабочие примеры — `examples/gui/example_gui`, `examples/gui/views-menu`,
`examples/gui/calculator`; тестовые файлы с эталонными дампами —
`tools/firml/testdata/`.
