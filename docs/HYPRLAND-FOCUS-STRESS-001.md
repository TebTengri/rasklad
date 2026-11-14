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
--space-telemetry --input-root /home/mvn/Work/rasklad. Она ещё НЕ выполнена:
для нового bounded live trial требуется отдельное согласие владельца.
