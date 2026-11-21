# Поколения grab: server-only metadata discriminator

Trial009: server grab Space emission attempt1/1, client callback0/1 во всех5
коррелированных повторах. Этот вариант проверяет, предшествует ли первый
наблюдённый keymap целевого grab последнему отправленному deactivate+done.
Не доказывает client proxy destruction, acknowledgement или socket delivery.

## Контракт

`--grab-epoch-witness` требует server-route→grab-entry→Core→querying addon→
focus+telemetry. Отдельный compile variant, прежние mount/device/namespace
capabilities; sharedfile остаётся только в private tmpfs. Нет raw text/ID/
serial/timestamps. Никаких изменений установленных библиотек и конфигурации.

Schema2:28 uint32/112bytes, первые18слов прежние. Добавлены18..27:
oldAfterDone press/release, current press/release, unknown press/release,
press при≥2живыхgrabs, peakLive gauge, registrations, destroyed.
Reader выбирает schema2 явно, требует unique IM requester, положительную
калибровку, bounded/saturation/coverage и сумму old+current+unknown=grab для
press/release. PeakLive — монотонный максимум, его delta не число живых grabs.

Внутренние128 slots монотонные, не переиспользуются. Первый keymap — marker
рождения; повторный keymap не обновляет его. Public resource destroy listener
удаляет active pointer до повторного использования адреса. Release request
сам не обнуляет живость. Сохраняются только opaque pointers для сравнения,
прошедшие lifetime события; они не экспортируются. Client getter связывает
metadata с единственным requester и единственным IM resource. Несколько
requesters/IM ресурсов, неизвестный grab или исчерпание slots снимают coverage.

Done ordinal обновляется по отправленному EVENT, не по client commit/ack.
Pending deactivate завершается первым последующим done. Classified old:
first-keymap doneOrdinal < latest deactivate-done ordinal. Если compositor
задерживает первый keymap после создания объекта, marker не равен реальному
моменту создания; источник/pinned positive control остаются существенны.
Aggregate и classifier меняются в одной seqlock transaction. Destroy callback
проверяет поток и имеет собственную transaction. Callback не читает payload,
не делает allocation/I/O. Public Wayland functions unchanged.

## Авторство и проверки

Claude Opus5.5 написал header ServerGrabEpoch по короткому публичному заданию;
Codex проверил/интегрировал variant, runner, reader и admission. Отдельный
Codex helper с кратким контекстом подготовил closed fake/probe/runner и
независимо проверил lifetime/семантику интеграции. Владение собственным MIT;
внешняя реализация не копируется, protocol XML notices сохраняются.

91 Python guards,24 reader contracts (17 addon+7 server),27 epoch C++ controls,
22 schema1 regression controls,112B filesystem reader checks PASS.
Unit opcodes/signatures выводятся независимо из pinned XML. Проверены
pending deactivate, repeated keymap, release-vs-destroy, ABA, overlap, missing
metadata/client mismatch/overflow, foreign destroy thread и cumulative sums.

Настоящий headless Sway baseline17/focus8 PASS, все5 target rows:
old0/0, current1/1, unknown0/0, multiple-live press0, peakLive1;
VK2/2, server grab1/1, client callback/Core/addon7/7. Это positive control,
не подтверждение причины exact Hyprland. Полный отчёт:
`data/server-grab-epoch-preflight-001.json`.

Нового host разрешения после009 нет. Следующая отдельно согласуемая проба:
одна nested Hyprland до75s, пять синтетических повторов, прежние parent
Wayland/renderD128, без рабочего Slack/IME/settings. Исходный strict FAIL
не восстанавливается поздними observations. Ни старое поколение само по себе,
ни согласие моделей не являются доказанным client discard или исправлением.
