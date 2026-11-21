# Подготовка exact Hyprland native IME trial

## Результат согласованной пробы

2026-11-14: один отдельно разрешённый запуск dbd221b прошёл все16checks
на установленном nested Hyprland, Chromium155 и Fcitx с существующим полным
evidence core. data/hyprland-ime-001-result.json содержит агрегаты/source hashes.
Приватный compositor оставался жив. Проверены composition до пробела, два
разных исправления, English Keep, next word, native Undo, Backspace/Escape,
mixed/long token, password passthrough и смена поля без утечки. После SIGKILL
только своего IME дальнейший ввод работал, исходное незавершённое слово
сохранилось, daemon не перезапустился. CDP не печатал, D-Bus input frontend
отсутствовал. Это фиксированные сценарии, не гарантия для всех фокусов/приложений.

Fcitx с ядром: RSS/high-water57728KiB, CPU0.18s за тест со стартом/profile/queries.
Не измерены physical latency, idle CPU или суммарная память browser/compositor.
Slack не подключён, readyForLiveUse=false. Следующий этап — adversarial
focus/serial suite, затем конкретный Slack trial. Согласованная одиночная
live-проба использована; дополнительных окон в этой контрольной точке нет.

Ниже сохранён план подготовки до выполнения.

2026-11-14. После успешного nested startup smoke следующий тест переносит
существующую Chromium/Fcitx native fixture в установленный Hyprland. Это
developer lab, не подключение к обычному Slack/IME и не готовый app adapter.

`python3 bin/test-hyprland-ime` показывает инертный план. Подготовленный запуск:
`python3 bin/test-hyprland-ime --run-approved --input-root /home/mvn/Work/rasklad`.
Live запуск пока не выполнен и требует отдельного согласования. Предыдущая
согласованная серия3/3 относилась только к startup diagnostics и завершена.

План: одно вложенное окно, один parent Wayland FD только у compositor,
renderD128 и primary RO/NODEV identity с обязательным runtime mount guard,
собственный минимальный sysfs snapshot. Parent FD закрывается у Python runner
сразу после старта Hyprland и удаляется из environment до любых других children.
Хостовый runtime socket path и bus не монтируются. Только после этого создаётся
private session bus, Fcitx и Chromium, подключённые к private wayland-lab.
Всем тестовым children виден разрешённый render node внутри namespace;
Chromium запускается с --disable-gpu, но это не отдельный device sandbox.

Нет сети, home/profile пользователя, clipboard/key replay, input/VT/KMS grants,
host sysfs bind или изменений конфигурации. Только20 прежних pinned profile
файлов доступны RO; addon и synthetic VK helper передаются sealed snapshots.
Chromium сохраняет штатный sandbox и использует пустой временный профиль,
CDP pipe вместо порта и только для fixture setup/observations. Все клавиши
фиксированные, отправляются private virtual-keyboard клиентом в nested seat.
Не печатать при live пробе: parent окно может получить фокус. Дедлайны: driver50s,
outer68s + bounded connect/cleanup, ориентир до75s. Cleanup только своих children
и namespace. Рабочие Slack/Fcitx/Hyprland не останавливаются.

Общий tools/native_ime_clients.py обслуживает reference Sway и новый Hyprland
runner; методы проверки не расходятся. Проверки: composition, два исправления,
English Keep, Space, next word, Backspace/Escape, Undo, password, focus, mixed/
long tokens, abrupt private IME death и последующий ввод. Это фиксированная
матрица, не adversarial serial test или общая гарантия сохранности текста.

Подготовка проверена без host display/GPU:

- После выделения common fixture reference Sway повторно прошёл16checks.
- Fake parent новый runner получил registry+sync и ожидаемо отказал при
  отсутствии протоколов, до запуска clients. render bind удалён, primary
  заменён harmless /dev/null RO/NODEV; настоящие sysfs/GPU/profile не нужны.
- Private bus routine действительно запускается и отвечает на NameHasOwner
  в отдельном namespace; parent marker отсутствует, GPU/display не предоставлены.
- 41 Wayland/metadata/debugger/IME guard проходит. Guards отвергают unreleased
  parent capability до старта bus/clients. Ошибка transport D-Bus больше не
  считается подтверждением отсутствия input frontend; UnknownObject/ожидаемый
  missing object или корректная introspection требуются явно.

No-host negative control:
`python3 tools/hyprland_fake_parent.py --run --ime-plan`.
Повторная запрошенная Grok-рецензия не завершилась за90s и не засчитана;
результат подготовки основан на перечисленных проверках, не согласии моделей.
Дальше — получить фактический result на exact compositor и отдельно разбирать
любой отказ, не снимать gates и не расширять доступ ради зелёного отчёта.
