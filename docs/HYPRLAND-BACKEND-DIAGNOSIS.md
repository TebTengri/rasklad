# Разбор отказа nested Hyprland

2026-11-14. Разбор выполнялся двумя независимыми агентами; Codex повторил
контрольные эксперименты. Обычный compositor, Slack, host IME и конфигурация
не менялись. Дополнительных соединений с рабочим display/GPU в этом разборе нет.

## Что доказано

1. Подключение через FD работает в установленном Hyprland. Искусственный
Wayland parent на socketpair получил get_registry и sync после передачи FD
через bwrap → Python → Hyprland. Не объявлялись никакие globals. Это проверяет
использование WAYLAND_SOCKET, а не совместимость с настоящим родителем.
2. Первоначальная диагностика была неполной: enable_stdout_logs=true без
disable_logs=false оставлял подробные логи выключенными. При одном и том же
искусственном parent после явного включения журнала вместо общей ошибки
backend классифицируется ожидаемая parent_protocols_missing. Исправление
затрагивает только временный private config.
3. libdrm 2.4.134 требует metadata в /sys до обхода устройств. Собственная
фикстура использует одну и ту же stat-only identity /dev/null через NODEV bind
во всех трёх случаях: без /sys return=-22; с пятью собственными PCI metadata
файлами return=0; с удалённым vendor return=-19. Настоящие GPU/sysfs/display
не предоставлялись. Это изолирует зависимость discovery от metadata.
4. Обычный --ro-bind character node в установленном bwrap сохраняет stat/rdev,
но запрещает open: контроль с /dev/null получил EACCES. Это позволяет отдельно
рассматривать primary card identity для metadata, без разрешения KMS open.

## Минимальные требования, установленные по исходникам

У libdrm сначала drmNodeIsDRM проверяет
/sys/dev/char/MAJ:MIN/device/drm. Для PCI нужны subsystem→pci, PCI_SLOT_NAME
из uevent и vendor/device/subsystem_vendor/subsystem_device. При flags=0
не требуются revision и PCI config. Обход /dev/dri сопоставляет rdev; dmabuf
main_device может ссылаться на primary card identity, а не render node.

Следующая подготовка: ограниченно прочитать и валидировать только эти значения
одной GPU; построить собственное дерево в private build/tmp; предоставить его
read-only как /sys. Оставить единственный открываемый renderD128. Primary card,
если нужен для сопоставления, только NODEV/stat-only с обязательной проверкой
mount flags до запуска compositor. Не давать целиком /sys, GPU PCI branch,
PCI config/ROM/resources, KMS, input/VT или host bus.

Aquamarine после discovery всё ещё требует render-node open/reopen, PRIME
export и GBM allocator. Поэтому успешная синтетическая фикстура не доказывает
успех allocator или точную первую причину предыдущих live-падений. Нужна одна
целевая проверка подготовленного исправления с подробной private диагностикой.
При новой ошибке — остановиться и разбирать её, не расширять доступ автоматически.

## Воспроизведение и источники

По умолчанию оба tools только показывают план:
`python3 tools/hyprland_fake_parent.py --run` — no-host negative controls;
`python3 tools/drm_discovery_lab.py --run` — synthetic metadata controls.
Это developer tests, не runtime приложения.

## Подготовленное исправление

`tools/drm_metadata_snapshot.py` явно собирает только выбранные пять значений
и stat/rdev выбранной пары узлов. Ничего не собирает при обычном CLI запуске.
Новая схема `bin/test-hyprland-nested --gpu-metadata` также только показывает
план; `--run-approved --gpu-metadata` открывает согласуемое вложенное окно.
Primary node передаётся обычным --ro-bind, renderD128 единственным --dev-bind.
До старта Hyprland runtime guard требует отдельный primary mount с ro,nodev.
В /sys монтируется только собственный временный snapshot, не host sysfs.

Сборщик проверен на реальных metadata без открытия GPU и display. Затем
installed libdrm вызван в namespace с тем же snapshot; оба настоящих node
identity предоставлялись только RO/NODEV. Оба discovery вернули0. Это уже
проверка именно собранного снимка, не только искусственной PCI фикстуры.

31 guard-тест проходит. Независимый reviewer нашёл lexical '..' escape из
build; исправлено запретом таких компонентов до записи, regression test
проверяет отсутствие файла вне build. Ревью также проверило symlink mapping
внутри snapshot. Не заявляем защиту от уже вредоносного процесса того же UID.
Агрегаты: data/hyprland-backend-diagnosis-001.json. Следующая live-проба с
snapshot пока не выполнена: allocator/полный compositor ещё не проверены.

Исходники читались, upstream код реализации в rasklad не переносился:

- [Точный Hyprland Compositor.cpp](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/Compositor.cpp)
- [Aquamarine v0.15.1 Wayland](https://github.com/hyprwm/aquamarine/blob/v0.15.1/src/backend/Wayland.cpp)
- [Aquamarine backend allocator](https://github.com/hyprwm/aquamarine/blob/v0.15.1/src/backend/Backend.cpp)
- [Aquamarine GBM](https://github.com/hyprwm/aquamarine/blob/v0.15.1/src/allocator/GBM.cpp)
- [Официальный libdrm 2.4.134 release](https://dri.freedesktop.org/libdrm/libdrm-2.4.134.tar.xz), xf86drm.c; просмотрен in-memory.

Omarchy skill и hyprland guide проверены: пользовательские конфиги не
редактируются и refresh/reload/restart не нужны. Context7 не экспортирован
среди tools текущей сессии; использованы точные первичные upstream исходники.
Новая рецензия Claude Opus не завершилась за90s и не засчитана как проверка.

## Завершённая диагностическая серия

Владелец отдельно разрешил до трёх bounded GDB запусков с теми же доступами.
Все три использованы; новых live запусков это разрешение больше не покрывает.

Первый SIGSEGV stack report был неполным: regex отбрасывал длинные C++ signatures
и смешивал worker stacks. Это не свидетельство ошибки Mesa. Исправлено structured
GDB Python: только остановленный поток, frame.name и basename system library,
24frames максимум; никаких args/locals/адресов. No-host control прошёл.

Вторая проба локализовала SIGSEGV в CGBMAllocator constructor через libc.
Aquamarine v0.15.1 присваивает строке результат drmGetDeviceNameFromFd2 без
проверки null. libdrm читает DEVNAME из /sys/dev/char/MAJ:MIN/uevent: node-level
metadata, которого в snapshot не было. PCI-level uevent недостаточно.
No-GPU фикстура подтвердила null при успешном discovery, затем ожидаемое имя
после добавления node-level DEVNAME. Поле выводится из проверенного basename,
без новых host metadata reads или прав.

Третья проба с этим metadata исправлением прошла: private Wayland socket и
1s liveness, wrapper exit0 с уничтожением namespace. Это подтверждает устранение
наблюдавшегося startup failure в данной среде, не transport/serial/focus/ввод.
Установленные библиотеки не менялись, upstream код не копировался.
Aggregate: data/hyprland-debugger-001-result.json. Исправление касается developer
lab, не добавляет GPU/sysfs logic в runtime ядро rasklad.

Следующий этап — existing Chromium/Fcitx fixture в запускаемом Hyprland,
затем adversarial focus/serial. Обычный Slack/IME пока не подключать.
