# Сторонние компоненты и данные

Проверка 2026-11-12, [PROJECT-AUDIT-001](docs/PROJECT-AUDIT-001.md). Это инвентаризация текущего исходного проекта и локальной сборки, не готовый комплект лицензий бинарного релиза. Обновление 2026-11-13: владелец выбрал [MIT](LICENSE) для собственного кода и документации. Условия сторонних файлов действуют независимо; лицензия проекта их не заменяет.

| Компонент | Использование | Условия и состояние |
| --- | --- | --- |
| Qt Core/Gui/Qml/Quick/QuickControls2, платформенные плагины | Установленные shared libraries; исходники Qt и библиотеки не вложены в репозиторий | Рассматривается LGPL-3.0; лицензии и notices вложенных компонентов Qt также требуют учёта при упаковке. Публичность нашего кода не заменяет выполнение LGPL |
| Hunspell 1.7.5 | Необязательная системная shared library; парсер словарей вызывается только при явном локальном профиле | Альтернативы MPL-1.1 / GPL-2.0-or-later / LGPL-2.1-or-later; локально выбран MPL-1.1 с MySpell notice. Полные сохранённые условия: `data/notices/Hunspell-*`. Распространение библиотеки не одобрено этим локальным допуском |
| libwayland client/server | Только отдельные developer-тесты и системный Qt Wayland backend; реализация библиотеки не скопирована | MIT; для распространения собственного test binary отдельно учесть зависимость |
| input-method-v2 XML | Единственный вендорный файл протокола в `tests/input_lab/protocol/`; сгенерированный код только в `build/` | MIT (Expat), источник закреплён по commit/SHA-256, полный copyright сохранён: [notice](tests/input_lab/protocol/README.md) |
| LibreOffice EN/RU dictionaries, .aff, производные stem/model артефакты | Только явно подключаемые локальные эксперименты в ignored `build/` | Разные исходные условия EN/SCOWL/Ispell и RU; полные первичные notices в `data/notices/`. `release_approved=false`; это не данные для бинарного релиза |
| Tatoeba CC0-only exports | Локальная оценка, без обучения на этом корпусе | Конкретный CC0-only snapshot и ограничения: `data/evaluation-002-sources.json`, `data/notices/Tatoeba-CC0-REVIEW.md`. Сам корпус в Git не входит |
| Rust Book EN/RU | Локальная оценка; главы и производные случаи в `build/` | MIT/Apache-2.0 и сохранённые copyright notices; версии/допуск в `data/evaluation-004-sources.json`. Не разрешение публиковать корпус/weights |
| Python, qmake, компилятор, bubblewrap, Hyprland | Уже установленные средства разработки/тестирования | Не поставляются с rasklad. Запуск бинарника не означает копирования его реализации в наш проект |

Таблица раскладки написана вручную по стандартным клавишам XKB; реализации WaySwitch/goswitch/xremap/keyd/XNeur не переносились. Архитектурная разведка и её ограничения: [OPEN_SOURCE_REVIEW](docs/OPEN_SOURCE_REVIEW.md).

## До бинарного выпуска

1. Выбрать лицензию собственного кода и явно исключить из неё сторонние notices/файлы с их отдельными условиями.
2. Зафиксировать фактическую упаковку и полный список библиотек/плагинов, их версии, лицензии, copyright и соответствующие исходники. Текущий список прямых ELF-зависимостей не является полным SBOM.
3. Выполнить применимые обязанности LGPL для используемых модулей Qt: тексты лицензий/уведомления, сведения о соответствующих исходниках и возможность замены/пересборки библиотек. Динамическая линковка сама по себе не выполняет все обязанности. Упаковка пока отсутствует.
4. Для Hunspell отдельно выбрать и выполнить условия распространения выбранной лицензии, если библиотека будет распространяться. Не переносить локальный допуск на любой будущий способ упаковки.
5. Для каждого словаря/weights провести отдельный release review, сохранить происхождение, полные notices и пометки преобразований. Сжатие и обучение не отменяют условий источника.

Первичные источники: [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations), [Qt Quick Controls license/attributions](https://doc.qt.io/qt-6/qtquickcontrols-index.html#license-and-attributions), [Hunspell 1.7.5](https://github.com/hunspell/hunspell/tree/v1.7.5), [паспорта и первичные словарные notices](data/README.md). Это техническая проверка известных источников и границ использования; спорные права на будущие артефакты не считаются разрешёнными автоматически.

Опциональный developer-кандидат `hunspell-wasm@0.3.0` (rotemdan): registry metadata
декларирует `(LGPL-2.0 OR GPL-2.0 OR MPL-1.1)`, архив содержит COPYING/COPYING.LESSER/
COPYING.MPL. Это не выбранная лицензия бинарного релиза: точный source build/version
WASM и полный комплект Hunspell/MySpell/Emscripten notices требуют отдельной
проверки. Lock: tools/browser_spell/candidate.json, release_approved=false.
Код/бинарник находятся только в ignored build/ для закрытого synthetic-теста,
не vendored и не включены в default build/runtime. Собственная C API обвязка MIT;
это не перелицензирует сторонний engine.

Текущий закрытый browser-тест заменил этот бинарный кандидат source-сборкой
Hunspell 1.7.5 (upstream commit и SHA-256 архивов в
`tools/browser_spell/source-lock.json`), Emscripten 7.0.0. Исходники upstream
не изменены; генерируются version/visibility headers. Две сборки совпали.
Hunspell/MySpell сохраняют собственные лицензии и notices; Emscripten/toolchain
и runtime требуют полного списка attribution перед распространением бинарника.
Рецепт и обвязка проекта MIT, бинарный release ещё не разрешён.

The optional ProseMirror editor fixture uses the eight pinned packages in
`tools/prosemirror/lock.json`. Each actual archive LICENSE was read and hashed;
all are MIT. Complete byte-distinct notices: `data/notices/ProseMirror-MIT.txt`,
`OrderedMap-MIT.txt`, `RopeSequence-MIT.txt`; per-package notices accompany ignored
cached/derived code. ESM specifier/source-map rewriting is marked in the fixture's
MODIFICATIONS. No npm install/scripts or third-party code vendoring. Registry
reported gitHead is provenance metadata; archive/code pins are the tested identity.
These permissions do not relicense model/dictionary/Hunspell components or approve
real artifact distribution. See docs/PROSEMIRROR-ADAPTER-005.md.

## Fcitx transport developer lab (SYS-244)

The optional project-authored addon and fixture link to installed Fcitx5 libraries
and use the installed Qt Fcitx platform input module. No upstream implementation
is copied/vendored. Installed package metadata: Fcitx5 5.1.23-1
(LGPL-2.1-or-later, Unicode-DFS-2016), fcitx5-qt 5.1.16-2
(LGPL-2.1-or-later, BSD-3-Clause, GPL-2.0-or-later: component-specific), Qt 6.12.0
with the existing Qt licensing conditions. Installed Fcitx Core/Utils headers
carry LGPL-2.1-or-later; the used Qt QPA private header offers LGPL-3.0-only or
GPL/commercial alternatives. The private QPA driver is a version-dependent lab
fixture, not a supported public Qt API or packaged product dependency.

Sources: https://github.com/fcitx/fcitx5 and https://github.com/fcitx/fcitx5-qt ;
Qt https://code.qt.io/cgit/qt/qtbase.git/ . Local files/versions/hashes are recorded
in data/fcitx-transport-environment.json. This local linking experiment does not
approve redistribution of system binaries, SDKs or their bundled dependencies.
Before packaging, audit exact component licenses, source/notice/relinking duties
separately. Own addon code remains MIT; that does not relicense Fcitx/Qt.
