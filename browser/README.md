# SYS-242: первый адаптер собственного браузерного поля

Это реализованный механизм редактирования, **не готовое расширение для почты**.
Главное Qt-приложение, профиль браузера и системные клавиши не меняются.

## Запуск проверок

Нужны уже установленные qmake6/Qt Core, make и C++ compiler для developer exporter, Python 3, Node (проверено 26.12), Chromium (проверено
155.0.8059.84), bwrap. Зависимости не скачиваются.

```sh
bin/build-browser-fixture
node tests/browser/adapter.test.mjs
node tests/browser/router.test.mjs
node tests/browser/scorer-security.test.mjs
node tests/browser/native-conformance.test.mjs
node tests/browser/lexicon.test.mjs
node tests/browser/run.mjs
```

Последняя команда запускает закрытый bwrap с отдельными HOME и профилем в tmpfs,
без сети, host runtime/DBus, clipboard, GPU и устройств ввода. Chromium sandbox
остаётся включённым. CDP идёт по дочерним pipes, порт не открывается. В среде,
запрещающей создание namespace, тест завершится ошибкой; запускать обычный браузер
или выключать sandbox вместо него нельзя. Дедлайн 45 секунд, bwrap die-with-parent.
Сборка создаёт synthetic-conformance.json (семь искусственных наборов весов и
числовой нативный эталон) и synthetic-model.js для fixture. Реальные источники
данных не открываются. Эти файлы не являются release package.
Снимок собственного поля: `build/browser-fixture/preview.png`.

## Что реально подключено

`adapter.js` получает синхронный локальный `decide(token)` и явную регистрацию
**собственного доверенного textarea** с ролью prose и раскладкой US. Неизвестные
поля не регистрируются. Парольные формы, hints credentials, отключённые/readonly,
code/spellcheck=false, composition и чужой фокус отсекаются до чтения value.
Отсутствие password input в форме само по себе не доказывает безопасность:
именно владелец интеграции обязан разрешить обычное поле. Поиск credentials
ограничен associated form, это дополнительный guard, а не классификатор всей
страницы. Поле вне form не становится защищённым от наличия чужого password
в другой части страницы. Владелец не должен регистрировать секретный/неизвестный
textarea, в том числе при «показать пароль» без autocomplete. DOM-атрибут не является
авторизацией. Страница и её скрипты остаются владельцами документа; это не защита
от вредоносного сайта или процесса того же пользователя.

Буквы сразу находятся в документе. Перед Space проверяются диапазон, слово,
контекст и снимок после evidence. Замена выполняется синхронно через native
`execCommand('insertText')`, проверяется её результат. Никакого удаления клавишами,
перепечатки, чтения clipboard, сети, истории или передачи слов через IPC нет.
Ошибка отключает адаптер. Только доказанный no-op восстанавливает выделение и
пропускает исходный Space; неожиданное изменение не откатывается вслепую.
Лимиты: документ 65 536 UTF-16, слово 4–32 буквы. Получение textarea.value всё ещё
читает строку целиком до проверки длины; лимит не гарантирует ограниченный пик памяти.

Генератор извлекает 94 собственные пары из `src/layoutconverter.cpp` и создаёт
browser converter. Второй вручную поддерживаемой таблицы нет; сохраняется защита
combining marks. Это **общий источник преобразования**. Дополнительно `policy.js` создаётся
путём выполнения настоящего C++ DecisionRouter для всех 512 состояний evidence
и двух профилей; 66 допустимых символов на раскладку экспортирует native
canonicalizeToken. Browser router валидирует metadata и выбирает результат
из общей таблицы, не переписывая evidence policy вручную. Browser `scorer.js`
реализует арифметику формата RSKN и проверен по C++ эталону. Это JS port,
не запуск C++ бинарника. `evidence.js` связывает scorer с явными full/control/stem
интерфейсами. Основной fixture пока не использует настоящие словари/веса; отдельный Hunspell
кандидат проверяется opt-in тестом ниже. Fixture-only evidence содержит несколько
явно синтетических словарных примеров и искусственную числовую модель: согласие
источников, известный ввод, неоднозначность и dictionary-only. Это не качество
распознавания и не словарь продукта.
Реальные локальные артефакты не открывались. JS не заменяет основной C++/Qt стек.

## Компактный stem-индекс

`lexicon.js` читает существующий RSKF v1: точное членство canonical keys, блоки
по 16 записей, front-coding, бинарный поиск по первым ключам. Данные копируются
в собственную ограниченную память; глобальный JS Set/массив всех слов не нужен.
Header/offsets/prefixes/sorting/trailing bytes проверяются, query не сохраняется.
`stats()` возвращает только keyCount/blockCount/payloadBytes/indexBytes; indexBytes
не включает payload. Максимум формата: 1 048 576 ключей, файл 64 MiB; при загрузке
есть временные копии, это не жёсткая гарантия пика RSS.

В fixture stem уже вызывается через этот reader и проверяет shared ambiguity.
Native writer/parser дал 8 исходных случаев, 469 мутаций файла и 12 356 membership
ответов; совпадение подтверждено в Node и закрытом Chromium. Все данные искусственные.
RSKF **не заменяет full/control Hunspell**: canonical keys не сохраняют регистр,
морфологию и словарные flags. RSKP pair parser пока не перенесён; EN/RU загружаются
отдельными RSKF. Реальные .aff/.dic/артефакты не читались и не выпускались.

## Опциональная full/control Hunspell-обвязка

`spell.js` создаёт четыре handles (EN/RU × full/control) в одном заранее
инициализированном Emscripten module. Control получается тем же удалением
PFX/SFX-строк, что native; остальные flags/case/compound правила остаются
у Hunspell. Typed query не пишется в MEMFS: только временный UTF8 malloc-буфер,
который адаптер зануляет перед free. Это best effort, не гарантия стирания
внутренних копий Hunspell/JS. Ошибка native query выключает profile; отсутствие
engine не превращается в false membership. На входе caps: aff 1 MiB, dic 16 MiB.
Модуль не загружает код/данные и не осуществляет сеть самостоятельно.

Отдельный developer test работает только с artificial .aff/.dic/model:

```sh
# Обычный prepare сети не использует; кандидат должен быть заранее проверен.
bin/prepare-browser-spell
# Только явный флаг разрешает скачать закреплённый архив КОДА в ignored build/:
# bin/prepare-browser-spell --fetch-candidate
node tests/browser/spell.test.mjs
node tests/browser/spell-run.mjs
```

`tools/browser_spell/candidate.json` закрепляет hunspell-wasm@0.3.0, SHA-256
архива/loader/WASM и `release_approved=false`. Нет npm install, lifecycle scripts,
реальных словарей или нового обязательного runtime dependency. Код/бинарник
кандидата не вложены в Git и не подключены к default fixture. Браузеру видна
только generated fixture directory, а не весь проект/build/home; sandbox и
network namespace включены. Loader получает bytes напрямую, попыток fetch/XHR
в сквозном прогоне — 0.

Результат: 93 native full/control cases и 7 byte-exact control cases совпали с
установленным Hunspell 1.7.5. Дополнительно прошли 12 browser assertions (case,
сгенерированная словоформа, следующая буква, native Undo, dispose/fail-open обычного
ввода), 5 Node cleanup/guard tests и прежние 32 browser assertions. Пример:
`Ghbdtn → Привет`, `rjns → коты` через реальные affix rules искусственного словаря.
Четыре engine использовали 18 284 544 B WASM heap; setup+проверки заняли ~84 ms
в одном прогоне. Это не RSS, не real-dictionary latency и не скорость нажатия.

### Source build вместо непрозрачного кандидата

Текущий `spell-run.mjs` использует собственную сборку upstream Hunspell 1.7.5,
commit `e8342700b4dde06130cdbba75f7e6500ffa58864`, Emscripten 7.0.0.
Lock: `tools/browser_spell/source-lock.json`; рецепт: `bin/build-browser-hunspell`.
Две отдельные sandbox-сборки на этой машине дали одинаковые SHA-256.
WASM 379 238 B, JS 58 920 B. Проверяется версия C API до создания handles,
SHA-256 обоих файлов и фактическая memory section: initial 16 MiB, maximum 128 MiB.
Повторный закрытый прогон: 93 native cases, 7 control cases, 12 browser assertions;
4 handles используют один heap 16 MiB; setup+все проверки ~51 ms, network attempts 0.
Это искусственные данные, не оценка памяти настоящих словарей или языковой точности.

Рецепт работает без скачивания: положите архивы из lock в
`build/hunspell-source-build/{hunspell.tar.gz,sdk.tar.xz}` и распакуйте Python
`tarfile.extractall(filter="data")` в `hunspell/` и `sdk/` соответственно.
Архивы должны пройти SHA-256 из lock **до распаковки**. SDK остаётся локальным
инструментом сборки, не устанавливается в ОС. Затем:

```sh
python3 bin/build-browser-hunspell out
python3 bin/build-browser-hunspell repeat
python3 bin/prepare-browser-spell
node --test tests/browser/spell.test.mjs
node tests/browser/spell-run.mjs
```

Compiler запускается в bwrap без сети/домашнего каталога с двумя workers;
source и SDK read-only. Ожидаемые output hashes проверяются после линковки.
Проверка архивов и output не защищает от злонамеренной подмены извлечённого SDK
владельцем машины. Для выпуска остаются полный комплект notices/source offer
Hunspell/MySpell/Emscripten и runtime, а также отдельный допуск реальных данных.
`release_approved=false`; MIT не перелицензирует сторонние компоненты.

## Ограничения и следующий проход

- Только собственный plain textarea, US→RU. Gmail/Telegram, contenteditable,
  React/ProseMirror, iframe/shadow DOM и сторонние редакторы не проверены.
- `execCommand` deprecated. На проверенной версии Chromium native Ctrl+Z возвращает
  исходное слово; следующий Space сохраняет отменённое слово без повторной замены.
  Другие движки/версии требуют отдельного допуска. Это тест
  штатной Undo, **не назначение Ctrl+Z программе**.
- Пользовательский ручной жест остаётся **двойной Super**: преобразовать последнее
  слово и переключить раскладку; следующие два нажатия — обратно. Этот адаптер
  не устанавливает и не перехватывает системный жест. Безопасная доставка жеста
  владельцу поля и переключение OS layout ещё не реализованы здесь.
- Обычное поле prose может содержать команды/код: смысл одного слова не выводится
  из textarea. Пунктуационные URL/пути отсеиваются, но общей защиты inline-кода
  нет. Нужна интеграция реального владельца редактора с ролями/диапазонами.
- Нет расширения, host permissions, native messaging, установщика и ежедневного
  режима. Policy, арифметический scorer и evidence binding уже подключены; следующий
  шаг: воспроизводимая сборка/допуск полного словарного engine, разрешённые данные и проверка integrity,
  затем адаптер конкретного редактора и проверяемый пакет с минимальными правами.
  Не выдавать эту fixture за совместимость со всеми приложениями.
- Shell/TUI остаются отдельным milestone, браузерная проверка их не заменяет.

Документация API: [Chrome scripting/activeTab](https://developer.chrome.com/docs/extensions/reference/api/scripting),
[MDN execCommand и история отмены](https://developer.mozilla.org/en-US/docs/Web/API/Document/execCommand),
[CDP Input](https://chromedevtools.github.io/devtools-protocol/tot/Input/).
Сторонний product-код или словарные данные не копировались. Собственные модули
лицензированы по [MIT](../LICENSE) по выбору владельца от 2026-11-13. Сторонние
компоненты/данные сохраняют свои уведомления и отдельный release-допуск.

## Real raw-dictionary compatibility result

The separately authorized 2026-11-13 test loaded the pinned RU/EN raw dictionary
packages with full notices into the source-built engine, outside the default page.
All 83 native/WASM cases and both control byte arrays matched; 2,000 additional
lookups completed. Four handles used 19.25 MiB WASM heap; dictionary init ~295 ms
in one run. This excludes browser/native RSS and JS input copies; lookup batches
are not physical keystroke latency. No model/editor/user text was loaded.
Packages and data stay ignored/local, distribution false. This proves the tested
dictionary-engine handoff, not real language accuracy or installable mail support.
Reproduction, precise scope and aggregate: [language packages](../data/LANGUAGE_PACKS.md).

## Verified local session and editor integration

`browser/session.js` now joins verified raw language packages, the unchanged RSKN
model, RSKP stem pair and source-built Hunspell to the existing router/textarea
adapter. The manifest is supplied by a trusted owner/catalog; a downloaded pack
cannot approve itself. Both purpose flags are exact booleans and independent.

Before the first async wait, the session snapshots policy/manifest, captures module
factories and owns bounded non-shared/non-detached byte copies of every listed asset.
SHA-256 must match for every asset, including complete notices, before any factory
runs. Model and RSKP/RSKF formats are validated before the engine loader executes.
There is no weaker profile, auto-fetch, code discovery or threshold change.
RSKP extraction is exact-length/version/language-count checked. An invalid input
rejects the entire session with a generic error and reverse cleanup of resources.
The caller's input buffers remain unchanged; temporary owned copies are wiped
best effort. This is not guaranteed erasure from JS strings, internal engine memory
or swap, and not a sandbox against hostile code in the same realm.

Only a ready session can register a field explicitly as owner-authorized prose.
The query path stays synchronous. Spell failure, missing handles or heap outside
the 128 MiB limit disables the whole session and removes its input hooks. Dispose
is idempotent and clears resource references. `decideUs` follows the router contract
(candidate string or null), never an async result. Ordinary editing survives a
failed load, failed engine or disposed session. Unknown/external fields are not
scanned or automatically authorized.

Reproduction (default is fully fictional):

```sh
node tests/browser/session.test.mjs
python3 bin/test-reviewed-dictionaries --editor
# Explicit scoped real integration only, not run by normal build/test:
python3 bin/test-reviewed-dictionaries --real --editor --input-root /home/mvn/Work/rasklad
```

The separate [profile review](../data/notices/BROWSER-PROFILE-REVIEW.md) and
[passport](../data/browser-profile.json) retain exact source metadata, MODIFICATIONS
and the two notice copies beside the existing derived artifacts. Four raw inputs,
one 88,290 B model and one 1,200,379 B stem pair are the only linguistic artifacts.
No reference-model.bin, corpus, features, labels, training or private text is read.
Outer bwrap mounts only those fixed files and code/notices; nested Chromium sees
only the generated minimal editor HTML. Third-party code is instantiated from
verified bytes with fetch/XHR disabled; control stays in child CDP pipes.

Completed real run: all 12 fixed functional cases plus Undo/dispose/integrity/network
checks passed (16 checks). `ghbdtn x → привет x`, title/upper case, known English,
URL/path/mixed/short gates and unregistered/code/credential-form fields behaved as
specified before execution. Native Undo returned `ghbdtn`; disposed sessions left
`ghbdtn ` untouched. Corrupted model bytes were refused before WASM instantiation.

Full hash+init: 421.4 ms in the final run; WASM heap 19.25 MiB, model file 88,290 B,
stem pair 1,200,379 B. These exclude JS/browser total RSS and are not per-key latency.
The local profile's 5,860,597 input bytes include engine/data/notices, not a finished
release package. Aggregate/source fingerprints: [result](../data/browser-editor-001-result.json).
11 session Node tests, 18 package/oracle Python tests and the previous 32 browser
regressions pass. Fixed examples do not establish linguistic accuracy or support
for Gmail/Telegram/other editors. No extension/install/global gesture is included;
double Super remains the planned manual operation. Distribution remains false.

## Actual transactional editor

`browser/prosemirror.js` now adapts the same verified session to a real upstream
ProseMirror editor through public buffer/history APIs. It preserves mixed marks,
annotations and native Undo/Redo without DOM insertion or execCommand. Owner
classification and registration precede bounded prefix access; revocation/stale
state and failed application dispatches disable safely. The host provides its
existing editor/history instances; there is no global app discovery or injection.

Real-profile tests passed 12 base cases +25 rich-editor/adversarial checks, plus
10 new Node privacy/lifecycle tests. This is not Slack compatibility yet. Sources,
full MIT notice handling, reproduction, exact result and exceptional Space behavior:
[ProseMirror adapter](../docs/PROSEMIRROR-ADAPTER-005.md).
