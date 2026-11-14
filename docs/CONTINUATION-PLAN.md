# План продолжения: SYS-244

## Актуальная контрольная точка

Новая отдельно разрешённая проба bed8364 выполнена: exact Hyprland baseline17
pass, focus3/4, case2 снова original без Space. Trace valid/armed/complete,
dropped0, records[]: до cut нет Space-coded keydown/up или beforeinput/input.
Это не исключает non-Space-code keyboard event или late delivery. Допуск
использован, новых host окон не открывать. data/hyprland-space-telemetry-002-result.json.

Diagnosticv2 Claude реализована, Codex интегрировал, independent critic проверил:
all-key fixed codeclass enums + failed-only input event wait500ms с отдельным
late snapshot и неизменным первоначальным FAIL. 45 Python +19 oracle +19telemetry
tests pass; private headless Sway baseline17/focus4/4 pass. Native late-wait
не exercised (case2 проходит на Sway); он проверен unit faults.
data/space-telemetry-v2-preflight-003.json. Следующий шаг — одна отдельно
согласованная exact Hyprland проба v2 до75s с тем же ограниченным scope.

Коллективное ревью следующего этапа: docs/SPACE-DELIVERY-REVIEW-002.md.
Три независимых reviewer + фактический Claude Opus5.5 разобрали пути Space;
vkReady-гипотеза понижена из-за keymap/key FIFO, новые core/config fixes
не обоснованы. Дополнительный rAF/sleep не end-to-end barrier. Следующим
остаётся уже подготовленный single Space DOM trace на exact Hyprland;
для live окна по-прежнему требуется ответ на предложенный разовый допуск.

Exact Hyprland focus trial8008ad7: baseline17 прошли, focus3/4. Исправление
wrong-field подтверждено case4; case2 rapid blur/refocus теряет Space. Причина
не установлена, addon нельзя исключить по финальному значению. Предыдущий
одиночный live допуск использован; новых окон без отдельного согласия нет.

Optional bounded Space telemetry Claude готова и независимо проверена.
45 Python guards, 19 Node oracle, 10 telemetry tests проходят; instrumented
private Sway baseline17 + focus4/4 pass, trace показывает Space input в prose.
Агрегат data/space-telemetry-preflight-001.json; ограничения и следующая
команда в docs/HYPRLAND-FOCUS-STRESS-001.md. Нужен один отдельно согласованный
exact Hyprland diagnostic trial до75s. Инструментация может менять timing;
не считать её успех исправлением. Исходный строгий тест не ослаблять,
не добавлять sleep/retry Space. Slack и host IME не подключены.

Claude Opus5.5 реализовал4focus scenarios и C++ commit ordering fix; Codex
и independentcritic нашли и исправили ложно-положительные gates/cleanupmodel.
Sway strictsamecase BEFORE: payloadперешёлизproseвother; AFTERsameoracle4/4pass.
Причина: наш empty-preedit update ДОcommitString. Теперь localstate staged,
payloadfirst, cleanupnotificationafter; lifetime+hasFocus guard, nooldstatewrites
aftercallbacks. data/focus-stress-before-fix-001.json и focus-stress-reference-001.json.
17baseline +4focus cases Swaypass; GTK17pass; Qtpass.19Node oracle faulttests,
43Wayland+8Chromium+10Fcitxguards. Newbaselineoriginalword preservation REQUIRED.
Explicitend policy onlyclosedreviewedChrome155 fixture withrealVKpositivecontrol;
не genericDOMtrust. Подробно docs/FOCUS-STRESS-PLAN.md. AGENTS.md сохраняет роли.
Следующий шаг согласованный exactHyprland --focus-stress сновымaddon, до75s.
Новыхhostокон вэтойработе не было; oldsinglelivegrant consumed. Потом genuine
cross-surface stale-serial test (DOMfocus != protocolserialproof), Slackstilloff.

SUCCESS exact Hyprland INPUT: отдельно согласованный dbd221b runner прошёл
16/16cases с existing fullcore в Chromium155/nativeWayland/Fcitx. Composition,
две коррекции/EnglishKeep,Undo,password,blur,mixed/long,Backspace/Escape,
postIMEdeath input и сохранениеpendingword подтверждены для этойfixture.
Compositor stayedalive; ownIME stayedstopped; DBusinputfrontend absent; noCDPtyping.
data/hyprland-ime-001-result.json + docs/HYPRLAND-IME-001.md. readyForLiveUse=false,
Slackunconnected. Следующий шаг adversarial focus/serial suite на samefixture,
не detector research и не немедленное подключение Slack. Единственная разрешённая
live input проба использована; новых окон без нового согласованного scope нет.

Подготовлен bin/test-hyprland-ime: existing16case native Chromium/Fcitx fixture
на exact installed nestedHyprland. Общая native_ime_clients.py с reference,
Swayregression16pass; fakeparent fullrunner negativecontrol pass; privatebus
positive smoke pass без GPU/display.41Waylandguards. Live target ещё не выполнен,
нужен отдельный допуск (предыдущая startupсерия3/3 исчерпана). Подробнее
docs/HYPRLAND-IME-001.md. Не трогать Slack/hostIME и не менятьprogramgates заранее.

SUCCESS startup smoke: серияGDB3/3 использована; third probe со generated
node-level DEVNAME прошёл private_socket_ready +1s liveness, exit0. Причина
SIGSEGV локализована CGBMAllocator constructor; наш snapshot давал
drmGetDeviceNameFromFd2=null из-за отсутствующего node/uevent DEVNAME. No-GPU
контроль подтвердил null→expectedname; host/installedlibs не менялись.
data/hyprland-debugger-001-result.json, docs/HYPRLAND-BACKEND-DIAGNOSIS.md.
Следующий шаг: подготовить exact Hyprland + existing Chromium/Fcitx fixture
без debugger, затем focus/serial adversarial suite. Это ещё не поддержка ввода
или Slack. Серия3liveпроб исчерпана; новых окон пока не запускать.

Согласованная live-проба b366b5a со snapshot закончилась SIGSEGV до display,
а не прежним SIGABRT; источник неизвестен. data/hyprland-nested-003-result.json.
systemd metadata команды /lab/hypr.conf подтверждает test crash; core отсутствует.
Не делать вывод о конкретной GPU/allocator стадии из смены сигнала.
Подготовлен --debug-startup, no-host fake-parent control с GDB прошёл,33guards.
Следующее действие: согласованная bounded --run-approved --gpu-metadata
--debug-startup, получить стек без args/locals/network, затем разбирать факты.
Не запускать новые live-пробы до разрешения и не добавлять sysfs/devices.

Подготовлено минимальное исправление discovery: tools/drm_metadata_snapshot.py
и --gpu-metadata в nested probe. Доказательства HYPRLAND-BACKEND-DIAGNOSIS.md:
FD consumed actualHypr; исправлен disable_logs; synthetic controls -22/0/-19;
реальные metadata snapshot + оба nodes RO/NODEV дают libdrm0/0 без GPU open/display.
31 guards pass. Два reviewer-агента участвовали, затем достигли usage limit;
Claude review timeout не засчитан. Следующий конкретный шаг — согласованный
одиночный --run-approved --gpu-metadata (~16s), не broad sysfs/devices fallback.

Вторая отдельно разрешённая проба подтвердила backend_creation_failed;
data/hyprland-nested-002-result.json. Source review указывает на DRM discovery /
allocator dependency, но точный отказ не установлен. Следующий шаг — подробный
private classifier и минимальные metadata requirements libdrm, не третий blind
host запуск. Не включать рабочий IME и не расширять devices по умолчанию.

Первая согласованная live nested Hyprland проба завершилась SIGABRT до создания
private display (data/hyprland-nested-001-result.json). Причина неизвестна: logs
первого runner отбрасывались, core отключён. Добавлен bounded private startup
log classifier; повтор не выполнен и требует согласования. Не расширять /dev
или /sys и не трогать host Slack/IME для обхода отказа.

Native Wayland reference прошёл 16 checks +8 guards с полным локальным ядром;
см. WAYLAND-IME-001.md и MULTI-MODEL-REVIEW-002.md. Не расширять reference labs
и не возвращаться к обучению/словарам. Подготовить bounded nested test именно
установленного Hyprland, затем согласовать доступ к parent Wayland/GPU перед
запуском. Рабочий Slack и host IME не трогать. После точного compositor-теста
нужны adversarial focus/serial проверки и отдельно согласованный exact Slack.
На ключевых развилках повторять независимую критику других моделей и проверять
их утверждения экспериментом. Ни текущая схема, ни большинство голосов сами
по себе не доказывают оптимальность. Ниже сохранена история контрольных точек.

2026-11-14. Рабочая ветка SYS-242/message-adapter, draft PR17. Задача SYS-244 — настоящий Fcitx preedit/commit transport; SYS-243 — exact Slack, остаётся незавершённой.

- [x] Claude: минимальный project-local addon, одно синтетическое правило, bounded per-context state, только явно именованный lab-клиент.
- [x] Codex: независимая ревизия addon, лицензий и границ ввода.
- [x] Codex parallel agent: private D-Bus/Fcitx/real Qt IM module runner без host HOME/display/seat/network. Default plan-only.
- [x] Настоящий roundtrip keys -> Fcitx -> client preedit/commit. Собственный offscreen fixture доказывает только transport, не native Slack.
- [ ] Space, Backspace, Escape, modifiers, focus, capabilities/password, bounded word, normal exit и crash; измерять фактические гарантии.
- [ ] При доказанном transport подключить готовый local detector без повторного обучения/чтения raw dictionaries по старым разрешениям.
- [ ] Подготовить конкретный exact Slack trial/install/rollback для отдельного согласования; не запускать его ночью без владельца.
- [ ] Зафиксировать код/команды/агрегаты/ограничения в Git, PR17 и Linear. Сохранить точный следующий шаг перед лимитом.

Рабочая система/обычный Slack/существующий IME не меняются. Нет background keyboard capture, root/evdev/uinput, clipboard или пользовательского текста в cloud. Новые средства разработки остаются отдельными от обычных bin/build и bin/run.

Контрольная точка 2026-11-14: настоящий transport прошёл, source hashes/агрегат
в data/fcitx-transport-001-result.json. Результат не равен готовому Slack backend.
Два Codex-помощника остановлены системным usage limit; статус goal=usageLimited.
Продолжение: изолированный настоящий внешний клиент Chromium/Fcitx (проверить
официальный project-local Xvfb), затем сформировать конкретный exact Slack trial.
Незавершённое слово при crash теряется; эта задача остаётся открытой.

Следующая контрольная точка: Chromium155 + настоящий GTK/Fcitx transport прошёл
в private signed Xvfb без CDP typing. Точный Undo, mixed/long/password и blur
проверены. См. docs/CHROMIUM-IME-001.md. Xvfb находится только в ignored build/,
подпись проверена; в систему ничего не установлено. Следующий незавершённый шаг:
подключить существующее локальное evidence core вместо фиксированного правила,
не заявляя качество по transport-тесту; затем exact Slack trial.

Полный evidence core теперь проверен в Chromium: 16 functional checks +8 guards,
2 разных исправления/English Keep, invalid-profile fail-closed, Backspace/Escape,
blur/Undo. Source/report: data/chromium-ime-profile-001-result.json. Fcitx+core
около54.7MiB RSS, CPU0.15s в тесте. Abrupt IME death теряет текущее слово — OPEN.
Следующие приоритеты: (1) native Wayland/Hyprland transport и focus safety,
(2) exact Slack backend/test plan без автоматического X11 downgrade,
(3) crash composition recovery. Нельзя возвращаться к detector research/UI.
Read-only Slack metadata:8 verified processes,0 matching X11 windows/platform
flags absent; это не доказательство native Wayland (окно может быть скрыто).
