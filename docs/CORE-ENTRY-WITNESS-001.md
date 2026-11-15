# Лабораторный вход Core: следующий пункт наблюдения

2026-11-15, SYS-245. Проба006 дала handler totals6/7 и Space-code0/1 при
проваленном case2. Новый вопрос: отсутствует ли press уже на входе публичного
InputContext::keyEvent либо исчезает между этой точкой и нашим watcher.
Это ещё не диагноз Hyprland/Fcitx и не исправление коррекции.

## Изоляция и контракт

--core-entry-witness требует --addon-witness и запрещает no-query. Выбирается
отдельный addon и project-local shim. Python проверяет точные SHA-256 Core и
Wayland frontend, архитектуру Linux x86_64 и SONAME до parent-соединения.
Системные файлы только читаются. При смене ABI тест отказывается работать.

Shim sealed/read-only монтируется в namespace. LD_PRELOAD передаётся только
собственному Fcitx subprocess, не runner/Node/Chromium. Constructor проверяет
private markers, разрешает точный публичный ELF symbol через RTLD_NEXT один
раз и удаляет переменную для потомков. Пустой/собственный original, неверные
markers или ошибка удаления завершают только тестовый процесс. Маркеры не
заменяют sandbox: namespaces и отсутствие host HOME/bus/input обеспечивает
runner. Новых устройств, сети, прав или изменений desktop config нет.

Обёртка использует установленные публичные headers и закреплённый Linux
x86_64 member-call ABI. Сохраняет isRelease/code65/focus booleans до original,
увеличивает entry counts до вызова, вызывает original ровно один раз и
возвращает его bool. После callback не обращается к ic/event. Сохраняются
только фиксированные числа; нет текста, сырых кодов, timestamps или context
идентификаторов. В key path нет I/O, allocation, lookup или форматирования.
Function pointer original хранится только для вызова API, не как input ID.

Обычные uint8 counters ограничены потоком constructor: чужой entry-thread
завершает тест125, чужой snapshot возвращает0 до чтения данных. Потоки для
самого наблюдения не создаются. Snapshot не имеет setters/reset. Linker map
экспортирует ровно hook и bounded snapshot API, прочие symbols скрыты.

Schema3 getter возвращает36чисел: [3,enabled,own22,shimSchema1,shimReady1,core10].
Reader проверяет всё и удаляет4служебных слова, получая32счётчика; saturation255
даёт unknown delta=-1. До case2 проверяется фактическая активность обоих
слоёв на уже выполненном baseline; ready marker без counters не принимается.
Существующий schema2 режим сохраняется отдельно. Нормальный профиль после
препроцессора побайтово прежний. Системные библиотеки не переустанавливаются.

## Реализация и ревью

Первый полный запрос Claude превысил240s и не дал результата; он не считается
завершённым. Меньший запрос вернул предложение shim: запрошен Opus5.5,
modelUsage содержит5.5 и4.8; эксклюзивное авторство конкретной модели не
утверждается. Codex исправил счёт entry после original на entry до original,
добавил compile/thread guards и интеграцию. Другие Codex-агенты подготовили
readonly bridge, Python admission и JS decoder. Независимые ревью C++/ABI/JS
завершены без блокеров; reviewer сами runtime не запускали.

ABI fixture использует собственный fake-original, проверяя счётчики внутри
callback, ровно два вызова и неизменные false/true результаты. Это не замена
положительной проверки настоящего Fcitx. Launcher по умолчанию инертен;
--run выполняет12 sealed bwrap контролей без Fcitx instance/ввода/display/GPU.
Для каждого нового sandbox FD перематываются: --ro-bind-data потребляет
общую позицию. Отрицательный контроль поймал ошибку пустого ELF в первом
launcher; она исправлена до признания всей серии успешной. Хеши читаются
из исполняемых sealed FD, не из потенциально пересобранных путей.

68 Python guards,12 reader tests и12 ABI/admission/thread контролей проходят.
Настоящий headless Sway baseline17/focus4 pass: own7/7 и core7/7, Space1/1,
core focus1 и returnfalse1 для passthrough. Это доказывает реальную preemption
в положительном контроле. Не доказывает причину отказа Hyprland. Данные:
data/core-entry-witness-preflight-001.json.

Наш wrapper использует публичный API; тело Fcitx/Wayland не копируется,
установленные upstream компоненты неизменны. Собственные диагностические
исходники MIT, библиотеки сохраняют прежние лицензии. Бинарные build-кэши
не включаются в публичное распространение.

## Следующая ограниченная проба

Один отдельно разрешаемый test-hyprland-ime --run-approved --focus-stress
--space-telemetry --addon-witness --core-entry-witness с прежним input-root,
до75s. Допуск006 использован; нового host запуска ещё нет.

- Core press0 и handler press0: исследовать путь до Core entry.
- Core press1 и handler press0: исследовать ранний Core return/filter.
- Core Space focus0/false: это отдельное свидетельство no-focus entry.
- Диагностический PASS снова не является исправлением; не подгонять oracle.

Aggregate counters не идентифицируют пары и не являются трассой каждого
frontend callback; getter и обёртка могут менять timing. Собственный Fcitx
и compositor уничтожаются после пробы; Slack и рабочий IME не подключаются.
