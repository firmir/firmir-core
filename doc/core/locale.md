# Firmir::Locale — язык системы и язык приложения

`Firmir::Locale` (`<firmir/locale.hpp>`, firmir-core) — процессный синглтон
(`"Firmir.Core.Locale"`), хранящий текущий язык и решающий, откуда он берётся.

## Источники языка (по приоритету)

1. **Переопределение приложения** — `Locale::setLanguage("ru")`. После первого
   вызова локаль **перестаёт следить за системным языком**: `language()`
   всегда возвращает заданное значение, а сигналы бэкенда игнорируются
   (соединение с бэкендом разрывается).
2. **Системный язык от бэкенда** — плагин `firmir-locale-<name>.so`
   (`<firmir/localebackend.hpp>`). Бэкенд сообщает язык через
   `language() = 0` и может в любой момент испустить сигнал
   `signalLanguageChanged` (`"languageChanged"`, аргумент `String`) — Locale
   перечитывает язык и рассылает своё `propertyChanged("language")`.
3. **Локаль процесса** — `FIRMIR_LANGUAGE`, затем `LC_ALL`/`LANG`
   (`"ru_RU.UTF-8"` → `"ru"`; `C`/`POSIX`/пусто → `"en"`). Используется,
   когда бэкенд не найден. `FIRMIR_LANGUAGE` приоритетнее бэкенда
   (явное переопределение для тестов и встраиваемых систем).

## Бэкенд-плагины

Плагин — `firmir-locale-<name>.so`, экспортирующий
`extern "C" const Firmir::LocalePlugin* firmir_locale_plugin()`:
`abiVersion` (`FIRMIR_LOCALE_PLUGIN_ABI`), `name`, `available()`, `create()`.
Поиск: `FIRMIR_CORE_PLUGIN_PATH` (через `:`), `plugins/core` рядом с
исполняемым файлом, системный каталог (`<prefix>/lib/firmir/plugins/core`).
`FIRMIR_LOCALE_BACKEND=<name>` фиксирует бэкенд по имени.

Тестовый бэкенд — `plugins/core/locale` (`firmir-locale-test.so`): сообщает
`"ru"` и переключает язык при записи динамического свойства
`simulateLanguage` (String) — с эмиссией `signalLanguageChanged`.

## Связка с Translator

`Firmir::Translator` по умолчанию берёт язык из `Locale::language()` и
следит за его изменениями (т.е. за сменой системного языка и за
`Locale::setLanguage`). Явный вызов `Translator::setLanguage()` отключает
это слежение — зеркально тому, как `Locale::setLanguage()` отключает
слежение за системой. Таким образом цепочка переопределений:

```
backend (система) → Locale (приложение) → Translator (UI)
```

каждый уровень может зафиксировать язык и «отрезать» верхний.

## API

```cpp
Locale* l = Locale::instance();
l->language();               // эффективный язык (override || системный)
l->systemLanguage();         // только системный
l->followsSystemLanguage();  // true, пока не задан override
l->setLanguage("ru");        // override приложения; перестаёт следить за ОС
l->backend();                // загруженный бэкенд или nullptr
```

Смена языка в любом источнике сопровождается `propertyChanged("language")`,
поэтому биндинги Firml на `trId(...)` обновляются сами.
