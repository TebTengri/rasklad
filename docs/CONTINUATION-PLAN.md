# План продолжения: SYS-244

## Актуальная контрольная точка

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
