# Подготовленная проба установленного Hyprland

`python3 bin/test-hyprland-nested` — инертный план. `--run-approved` допустим
только после отдельного разрешения владельца на описанный здесь доступ.

Один compositor-only запуск: подключение к собственному родительскому Wayland
через единственный connected FD, отдельное вложенное окно и один render-node
/dev/dri/renderD128. Ожидание процесса ограничено 12 секундами; вместе с
подключением и завершением около 16 секунд. Окно может получить фокус, поэтому
во время пробы не следует печатать. Нет клиента, автоматической печати,
исправления текста, подключения к Slack или изменения host IME.

В namespace нет пути к parent socket, host HOME/session bus, сети, input/KMS
устройств или host конфигурации. Файлы конфигурации и runtime временные.
Не передаются пользовательские env/profile данные. /usr доступен только для
чтения, GPU render-node доступен на запись: это реальное исключение из
software-only reference isolation, а не полная изоляция GPU. Запуск не root.

Проверены синтаксис отдельного конфига через --verify-config без display/GPU,
передача synthetic socketpair через bwrap и четыре новых guard-теста.
Peer UID проверяется через SO_PEERCRED; это не защита от уже скомпрометированного
процесса того же пользователя. PID/group cleanup касается только своего теста.

Ревью указало на возможную необходимость sysfs для GPU discovery. В первой
пробе sysfs не предоставляется: отказ backend фиксируется как отказ, без
автоматического расширения доступа. Непроверенная переменная AQ_BACKEND удалена.
Само создание private display ещё не докажет исправление текста или focus safety.

Первая live-проба разрешена владельцем и выполнена: SIGABRT при старте до
private display, ввод не тестировался. Результат data/hyprland-nested-001-result.json.
Причина не подтверждена: первый runner удалял startup logs, core намеренно
отключён. Исправленный runner сохраняет только в private namespace bounded logs
и возвращает диагностический enum; после завершения logs исчезают. Повтор пока
не разрешён. При успехе следующий этап — закрытый тест ввода
в этом compositor, затем adversarial focus/serial тесты и exact Slack.

Вторая отдельно разрешённая проба выполнена (e530367): тот же SIGABRT,
диагностика подтвердила `backend_creation_failed`. Точная причина всё ещё
не установлена. Обычный host compositor не перезапускался.

Source review установленного Hyprland commit efb50993780079460b0cbed1363e2166a2de1d9f
показывает, что одинаковый throwError применяется к create и start отказам:
https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/Compositor.cpp
Поэтому общий enum не различает точку отказа.
Aquamarine v0.15.1 Wayland backend использует wl_display_connect(nullptr),
dmabuf feedback и drmGetDeviceFromDevId; затем требуется DRM/GBM allocator:
https://github.com/hyprwm/aquamarine/blob/v0.15.1/src/backend/Wayland.cpp
https://github.com/hyprwm/aquamarine/blob/v0.15.1/src/backend/Backend.cpp
Вероятная проблема — обнаружение GPU в урезанной среде, но это гипотеза.
Отдельный read-only libdrm discovery успешен на host и возвращает -22 в
namespace без sysfs И device nodes. Сравнение меняет два условия и не доказывает
исключительную вину sysfs; host GPU не открывался этой проверкой.

Следующий подготовительный шаг: детализировать private log classifier по
точным ошибкам upstream, проверить необходимые metadata paths libdrm, затем
провести ревью минимального mount plan. Без повторного blind запуска и без
автоматического предоставления /sys, /dev/dri целиком или KMS/input устройств.

Последующий разбор и подготовка исправления: HYPRLAND-BACKEND-DIAGNOSIS.md.
Дополнительная опция --gpu-metadata строит собственный минимальный sysfs
snapshot; primary card только NODEV/RO identity, renderD128 остаётся единственным
открываемым устройством. Runtime guard проверяет primary mount flags до запуска.
RO discovery с настоящими metadata и двумя stat-only nodes прошёл без GPU/display.
Применение этого исправления в live nested Hyprland ещё не выполнялось.
