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
