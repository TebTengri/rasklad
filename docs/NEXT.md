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

- Installed Slack tested only with empty profile/private Xvfb+buses: no visiblewindow/a11ypeer in eitherstatus.
- Strict Unix HTTPS broker built; no --share-net;24guardsPASS. docs/SLACK-ISOLATED-001.md has exact limits/results.
- ONE owner-approved ECH A/B trial opened2connections percase, stillno UI; permission CONSUMED. Defaultdenyremains.
- Offline fixedstderr flags: busUnavailable, no fatal/display-init match; causeunknown. Process roles previouslyunreliable, nowunknown unlessknown.
- Native private Sway A/B: compositor ready; Slack0peers both, Chromium enabled1peer/64nodes. Window visibility unmeasured. docs/SLACK-WAYLAND-BOOTSTRAP-001.md.
- busUnavailable also occurs in successful Chromium control; not a proven Slack cause.
- 15s offline protocol control: Slack registry/surface yes, toplevel not observed; Chromium all3 yes. docs/SLACK-STARTUP-STAGE-001.md.
- Execute docs/ACTIVE-PLAN.md autonomously; no identical offline A/B repeats.
- Next inspect bootstrap/network fixture boundary; verify primary runtime/API sources before moreflags. No realprofilecopy/globala11y workaround.

## Разрешения и границы

- ECH exception CONSUMED: --allow-ech-outer requires fresh explicit permission.
- Exact Hyprland011 approval CONSUMED: новый host Wayland/GPU запуск запрещён без свежего согласия.
- Закрытые no-host/no-GPU/no-network собственные фикстуры разрешены.
- Owner permits normal Slack tests in one designated test channel (name/link pending). No automated sending, unrelated text/history, profile copying.
- Latest live metadata preflight:8processes/0a11ypeers/IsEnabledfalse;0text/writes.
- Slack restart/global settings/IME/config/autostart remain outside that channel authorization.
- Семь стабильных hooks и общий profile cache остаются; таймеров/polling не добавлено.
- Не читать архив CONTINUATION-PLAN/всю переписку; bin/project-map find <topic>.
