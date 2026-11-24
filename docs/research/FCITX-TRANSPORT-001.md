# SYS-244: настоящий Fcitx/Qt transport в закрытом стенде

Дата: 2026-11-14. Developer-only прототип. Это не подтверждение поддержки Slack
и не подключение к рабочему сеансу владельца.

`bin/test-fcitx-transport` по умолчанию печатает план и ничего не запускает.
`--build` компилирует project-authored addon и клиент посредством qmake6/make в
ignored `build/fcitx_lab`. `--run` допускает только фиксированные собранные файлы
и синтетический ввод. На хосте не устанавливается addon и не запускается второй
IME для рабочего seat.

## Что проверяет настоящий transport

Используются установленный Fcitx5 daemon, его DBus frontend и установленный Qt
Fcitx platform input context. Qt offscreen platform действительно загружает
`QPlatformInputContextFactory`, но не имеет backend физических клавиш. Поэтому
тестовый клиент сам вызывает `inputContext()->filterEvent()` с фиксированными
синтетическими native-key данными, как это делал бы platform backend. Только
неотфильтрованные события доставляются виджету QLineEdit. Клиент НЕ создаёт
QInputMethodEvent; preedit и commit приходят исключительно из настоящего IM
модуля после D-Bus roundtrip через Fcitx.

Это проверка **toolkit/frontend roundtrip**, а не реальных физических клавиш,
композитора, обычного редактора или Slack. Qt private API используется только
в developer fixture и привязано к установленной версии Qt; его нет в продукте.

Первичные источники:
- https://raw.githubusercontent.com/qt/qtbase/6.12/src/plugins/platforms/offscreen/qoffscreenintegration.cpp — initialize()/inputContext().
- https://raw.githubusercontent.com/qt/qtbase/6.12/src/gui/kernel/qguiapplication.cpp — processKeyEvent не заменяет platform IM filter.
- https://fcitx-im.org/wiki/Develop_an_simple_input_method/en — preedit/commit API.

## Изоляция и очистка

`bwrap --unshare-all --die-with-parent --new-session --cap-drop ALL --clearenv`;
сеть, PID, IPC, UTS и mount namespaces отдельные. Read-only системный `/usr`,
минимальные linker/font files, свои `/proc` и `/dev`, tmpfs `/home`, `/run`,
`/tmp`. В `/lab` монтируются только runner, client, addon `.so`, его manifest и
private bus config. Весь проект, домашний каталог, host runtime/display, GPU,
input devices, clipboard и рабочая D-Bus шина не монтируются и не наследуются.

Private D-Bus config слушает только Unix socket внутри `/run/lab`; servicedirs
отсутствуют. Fcitx запускается с `--disable all --enable
keyboard,dbus,dbusfrontend,raskladlab`. Profile содержит только keyboard-us.
Пути addon заданы только для этого subprocess. Qt использует offscreen и
настоящий fcitx IM module; surrounding text export выключен. Правильность
app metadata gates не подменяется названием окна.

Общий deadline 30 секунд, CPU 25 секунд, address-space 1 GiB, core dump=0,
output-file <=1 MiB, JSON <=16 KiB. По timeout останавливается только собственная
процессная группа; если требуется SIGKILL, он относится исключительно к
созданному закрытому стенду. Нет SIGKILL пользовательского Slack или Fcitx.
Temporary run directory удаляется после reap. Runtime/user config создаются
только внутри tmpfs стенда.

## Проверки

Клиент наблюдает preedit без текста в документе, один исправленный commit на
Space, следующее слово, Backspace, отсутствие переноса слова при смене окна и
password/sensitive passthrough. Native Undo фиксируется отдельным наблюдением,
не выдаётся за полную гарантию native history.

Для crash test тот же клиент сначала держит живой preedit, сообщает фиксированную
phase marker по stdout. Runner убивает только свой daemon, подтверждает это по
stdin, а тот же клиент пробует продолжить набор. Отдельно публикуются продолжение
ввода и сохранность незакоммиченного слова. Отсутствие crash не моделируется
простым выключением addon; данные пользователя не участвуют.

Runner guard unit tests отдельно проверяют inert default, allowlist, отсутствие
host env/mounts, Unix-only bus без activation и отказ symlink/world-writable
артефактов. Это тесты границ, не доказательство IME совместимости.

## Текущее состояние

Настоящий roundtrip прошёл; 16 функциональных наблюдений true, 10 guard tests.
Агрегат и source hashes: data/fcitx-transport-001-result.json. Средний синтетический
sync filter RPC около 462 мкс, p95 около 767 мкс (208 calls, без settle waits).
Это не physical/native-app latency. Собственный daemon: RSS около 15.7 MiB,
high-water около 18.0 MiB, CPU 0.06 s в этом прогоне; не idle baseline.

Native Undo изменяет документ в проверенном QLineEdit. Первое отрицательное
наблюдение оказалось ошибкой fixture: nativeModifiers не содержал Control.
После исправления native XKB masks Ctrl4/Shift1/Alt8/Meta64 проверка прошла.
Это не доказательство восстановления исходной латиницы/полной Undo fidelity.

Принудительное падение собственного daemon с live preedit: последующий обычный
ввод работает, незавершённое слово потеряно. Это нерешённое UX-ограничение.
Смешанные/длинные токены не исправляются по суффиксу; capability downgrade
очищает pending preedit. Ни Slack, ни рабочий IME/config не менялись.

## Следующий шаг после восстановления лимита

Не повторять detector research. Нужен настоящий внешний клиент (Chromium с
реальным IM frontend), затем exact Slack trial с отдельным допуском владельца.
Один возможный изолированный display transport — официальный Xvfb, извлечённый
в project-local build/ после проверки пакета/подписи/лицензии; пока не скачан,
не установлен и не запускался. Нет Xvfb cache/dummy Xorg driver на машине.
Нельзя засчитывать CDP imeSetComposition/insertText за Linux IME integration.
