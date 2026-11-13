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

**До выпуска:** версия Hunspell внутри чужого WASM и связь с конкретным source
build не аттестованы. Loader допускает рост heap вплоть до 2 GiB; текущий тест
этого не достигает. Совпадение 93 случаев не доказывает полную эквивалентность
1.7.5. Следующий шаг — воспроизводимая сборка выбранного upstream source с
проверяемой версией, ограниченным heap и полными notices. Затем отдельный
release-допуск реальных данных. MIT проекта не заменяет лицензии кандидата.

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
