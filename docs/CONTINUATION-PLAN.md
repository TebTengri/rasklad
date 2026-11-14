# План продолжения: SYS-244

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
