# Exact Hyprland: focus stress

2026-11-14. Один отдельно разрешённый запуск8008ad7 выполнен, allowance использован.
Результат data/hyprland-focus-stress-001-result.json. Набор НЕ прошёл полностью.

17baseline predicates прошли. Focus cases:3/4, все source preconditions и cleanup
подтверждены, compositor оставался жив. Воспроизведённый ранее reentrant
compositionend wrong-field bug после Claude fix НЕ повторился: hook1,
corrected payload остаётся в исходном поле, второе пусто. Password и Escape
cases также прошли. Проверки native Undo/nextword/mixed/long/crash прошли.

Отказ case2: pending word → other → prose → Space. В финале исходное слово
сохранилось без Space, другие поля пусты; active prose, owned state, starts/ends1/1,
hook0, cleanup true. Это реальная невыполненная строгая проверка, но по ней нельзя
приписать причину addon, Fcitx, compositor, Chromium или синхронизации fixture.
Space VK ACK не подтверждает доставку в DOM: helper roundtrip выполняется после
каждого press/release на его соединении, без ожидания Fcitx forwarding.

Независимый reviewer и Claude предложили минимальную диагностику фазы ОДНОГО
Space: known-Space DOM keydown/up (trusted/composing/prevented,targetenum),
beforeinput/input inputType enum и value-class в момент input, затем finalclass.
Нельзя читать key/data/текст или включать cleanup Space в стимул. Если нужного
keydown нет, это ещё не доказательство отсутствия keyboard event и виновности
addon: IME может менять код события. При недостаточности данных — отдельно
private keyboard-us контроль без addon, с честным указанием отсутствия preedit.

Claude сделал слишком сильный вывод «addon исключён» по одному конечному слову;
Codex его не принял: отсутствие нового фокуса у addon и недоставка его commit
пока не наблюдались напрямую. Предложение readiness-variant допустимо только
как ДОПОЛНИТЕЛЬНАЯ диагностика, не замена исходного быстрого сценария. Sleep,
повторный Space или разрешённая потеря delimiter не являются исправлением.

Следующий шаг — подготовить optional instrumentation (может менять timing),
проверить без host display/GPU, затем согласовать одну ограниченную exact-пробу.
Непроинструментированный regression сохраняется. Новый core fix не делается
по предположению. Slack и системные настройки не менялись, readyForLiveUse=false.

## Подготовленная диагностика

Claude Opus5.5 реализовал optional space-telemetry.mjs; Codex интегрировал и
проверил. Независимый focus_suite_critic проверил diff и пять SHA-256: блокеров
не найдено; reviewer сам тесты не запускал. 45 Python guards, 19 Node oracle
и 10 telemetry tests проходят. На закрытом Sway без host display/GPU/network
baseline17 и focus4/4 прошли с диагностикой: case2 даёт trusted keydown →
beforeinput(insertText) → input(insertText) → keyup на prose; value-class меняется
с original на original+Space. Trace valid/complete, dropped0.
Повтор исходного Sway сценария без диагностики также прошёл baseline17 +4/4.
Агрегат: data/space-telemetry-preflight-001.json.

Сбор ограничен одной фазой case2, 32 enum-записями; key/data не читаются,
неизвестная цель=-1. Snapshot прекращает сбор до cleanup Space. Это только
закрытая синтетическая fixture, не фоновый сбор пользовательского ввода.
Отсутствие Space-coded events не исключает IME Process event; переполнение
делает trace неполным. Capture defaultPrevented не является конечным значением.
Дополнительный CDP arm способен изменить timing: инструментированный успех
не доказывает исправление. Исходный строгий сценарий сохраняется.

Подготовлена команда test-hyprland-ime --run-approved --focus-stress
--space-telemetry --input-root /home/mvn/Work/rasklad. На момент подготовки она не была выполнена;
ниже записан результат отдельно разрешённой пробы002.

## Exact Hyprland: диагностическая проба 002

После отдельного согласия владельца выполнена ОДНА проба bed8364 с
--focus-stress --space-telemetry. Допуск использован. Агрегат:
data/hyprland-space-telemetry-002-result.json. Baseline17 прошли; focus3/4,
повторный отказ case2, source original без delimiter, other/password пусты.
Source precondition и cleanup прошли, compositor жив, wrong-field case4 pass.

Trace: valid=true, armed=true, complete=true, dropped=0, records=[]. Между
arm после refocus и atomic cut после исходного отказа не наблюдались
Space-coded keydown/up и никакие beforeinput/input на fixture document.
Это НЕ доказательство отсутствия всех keyboard events: non-Space code
фильтруется. Нет также наблюдения после cut; late delivery не исключена.
Сама instrumentation не замаскировала отказ в этой единственной пробе.
Серия1+1 показывает два наблюдения отказа, но не надёжную частоту.

Дальнейшая подготовка без новых host окон: классифицировать все keyboard
events только armed фазы фиксированными enum (Space/Unidentified/empty/other)
и keyCode229 bool; Process обычно относится к key, читать его не нужно.
В failed-only диагностике можно дождаться уже зарегистрированного input
с deadline<=500ms до cleanup, затем снять отдельный late snapshot.
Первоначальный FAIL сохраняется даже при позднем совпадении. Никаких
повторов Space, дополнительных core/config changes или полного debug log.

## Подготовка diagnosticv2

2026-11-15. Claude реализовал модуль, Codex интегрировал failed-only ожидание
и тесты. 45 Python guards, 19 oracle и19 telemetry tests проходят; private
headless Sway baseline17/focus4/4 pass, codeClass trace соответствует Space.
Нативный late-wait не выполнялся: Sway case2 проходит. Unit faults проверяют
input до/во время ожидания, beforeinput-only timeout, отмену snapshot/stop,
overflow, отсутствие key/data/id чтения и позднее совпадение без PASS.
Агрегат data/space-telemetry-v2-preflight-003.json. Независимый static review
блокеров не нашёл; reviewer тесты не запускал.

500ms — браузерный deadline, не гарантированный wall-time предел; внешние
driver/run deadlines сохранены. observedBeforeWait не различает input до
primary snapshot и сразу после него. Любой input завершает ожидание;
принадлежность и значения проверяются отдельным строгим late oracle.
Трасса отсекается до cleanup. Новый live допуск ещё требуется; не запускать
повторно по уже использованному разрешению002. Команда прежняя:
test-hyprland-ime --run-approved --focus-stress --space-telemetry
--input-root /home/mvn/Work/rasklad. Core, Slack и host настройки не менялись.

## Exact Hyprland diagnosticv2: проба003

Одна отдельно разрешённая проба6d5ae19 выполнена; допуск использован.
data/hyprland-space-telemetry-v2-003-result.json. Baseline17 pass, focus3/4;
case2 primarySnapshotCode0, lateSnapshotCode0, waitStatus=deadline. Полная
armed трасса всех keyboard/beforeinput/input: records[], valid/complete, dropped0.
За дополнительный500ms браузерный deadline input не наблюдался. Late oracle
также не совпал; original без Space, other/password пусты. Три остальных
focus cases и cleanup проходят, compositor жив.

В этой пробе увеличение срока наблюдения и снятие Space-code фильтра не
показали доставку. Это не локализует виновника и не доказывает вечную потерю.
Дальше DOM trace не расширяем: следующим нужен отдельный lab-only свидетель
ветки addon, а не core fix по гипотезе. Предпочтён read-only GetStats через
существующий private bus: фиксированные20 saturating255 счётчиков, без
текста/keys/timestamps, setter/reset и I/O в key callback. Before-word baseline
и after-oracle sample, затем post-query DOM snapshot с сохранением исходного
FAIL. Нулевой прирост значит только отсутствие наблюдения ДО getter.
Значение255 делает соответствующую delta неизвестной, а trace неполной.
Обычная сборка не должна содержать этот интерфейс. Новый host run не разрешён.
