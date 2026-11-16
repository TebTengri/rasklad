# rasklad — короткая точка входа

- Worktree: /tmp/rasklad-message-adapter; branch SYS-242/message-adapter.
- Main /home/mvn/Work/rasklad не переключать; чужие проекты не смотреть.
- SYS-246 In Progress; PR17 draft; лицензия собственного кода MIT.
- Один исполнитель, одно итоговое ревью существенной границы; экономный режим AGENTS.md.
- Работать этапами автономно; не ждать «дальше» после каждого коммита.

## Что готово в закрытом LAB

- Offline detector/profile + default-deny policy, shared family Off/On.
- После On пропуск raw input до безопасного разделителя, включая новые поля/reset.
- Полный owned Sway/Chromium Off→On→Off: 8/8; normal18+focus4 PASS.
- Lazy LabProperty; payload только по требованию, cleanup terminal Off после release drain.
- Pending/Deferred/InFlight сохраняются; новая raw пара не теряет release из-за старой записи.
- SDK resource/reentry cases ASan+LSan PASS; старые16Off+6preedit тоже PASS.
- ADR-003: Submitted означает локальное завершение, не подтверждённую доставку.
- ADR-004 + data/context-resources-001-result.json: точные ресурсы/ограничения.

## Следующий блок

1. Выбрать минимальный способ подключения к реальному приложению с проверяемым полем.
2. Начать с адресного чтения docs/SLACK-DESKTOP-001.md: прошлый a11y запуск дал 0 узлов; не повторять без нового основания.
3. Разделить готовность LAB и production; UI/установка/реальные приложения пока не включены.

## Разрешения и границы

- Exact Hyprland011 approval CONSUMED: новый host Wayland/GPU запуск запрещён без свежего согласия.
- Закрытые no-host/no-GPU/no-network собственные фикстуры разрешены.
- Slack draft/text/write/restart, установленный IME, конфиги, автозапуск не трогать.
- Семь стабильных hooks и общий profile cache остаются; таймеров/polling не добавлено.
- Не читать архив CONTINUATION-PLAN/всю переписку; bin/project-map find <topic>.
