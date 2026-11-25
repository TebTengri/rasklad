# Границы безопасности

## Кратко (продукт 0.5.29)

**Аддон Fcitx** (`src/raskladproduct.cpp`):
- получает от Fcitx все нажатия клавиш, как любой метод ввода. Слово он
  собирает и исправляет только в полях приложений, включённых в настройках
  (по умолчанию Slack и Chromium); в остальных нажатия проходят без изменений и
  не запоминаются;
- не трогает поля, которые приложение пометило как пароль, скрытый ввод,
  терминал, e-mail, URL, число, телефон, дату или время, и терминалы из
  встроенного списка известных (даже вписанные вручную); терминал, которого в
  списке нет, он не отличит. Поле без такой пометки — в том числе поле
  пароля — выглядит для него как обычный текст;
- держит в памяти, отдельно для каждого поля, текущее слово (пока оно
  набирается) и последнее завершённое (обе его формы). Последнее слово
  забывается при следующей клавише (кроме Shift — он нужен для жеста), уходе
  фокуса или сбросе поля, а если оно уже подтверждено — и при несовпадении текста
  поля. Через 15 секунд его уже нельзя сменить двойным Shift; стирает его таймер,
  когда Fcitx обработает его после этого срока (один таймер на все поля, на
  самый ранний срок; время сна компьютера не считается). Стираются хранимые
  формы слова и строки, в которых аддон собирает слово и исправление. Это не
  полная очистка памяти: стандартная библиотека C++ и Qt (в том числе при
  проверке слова по словарю и при возврате строк из функций) делают свои
  промежуточные копии, а Fcitx и
  приложение — свои, получая текст. Такие копии аддон не затирает, и они могут
  оставаться в освобождённой памяти и в стеке, пока их не займут снова. Коды
  нажатых клавиш, чьи отпускания он поглощает, стираются при отпускании и при
  уходе фокуса;
- окружающий текст, который приложение передаёт Fcitx, сверяет только пока
  хранится последнее слово: при завершении слова, при обновлениях текста и перед
  двойным Shift. Если текст не совпадает у уже подтверждённого слова, слово
  забывается; текст длиннее
  64 КиБ считается неизвестным, и смены нет;
- пишет на диск только свою конфигурацию (`~/.config/fcitx5/conf/rasklad.conf`).
  Запрос SetConfig, в котором после разбора Fcitx больше 1024 узлов или
  64 КиБ имён и значений, отклоняет целиком; файл с диска при запуске и
  перезагрузке читается без этой проверки; GetConfig отдаёт известные аддону
  параметры из него целиком, а в работе аддон использует не больше 64
  приложений и 256 исключений;
- в сеть не обращается, текст в журналы и файлы не пишет (память процесса,
  как у любой программы, может попасть на диск через swap или дамп);
- короткий звук при смене раскладки (включён по умолчанию) играет через ALSA в
  отдельном потоке процесса Fcitx. Звук не связан с текстом, но libasound
  читает настройки ALSA пользователя и системы (`~/.asoundrc`,
  `/etc/asound.conf`) и загружает указанные в них модули прямо в процесс,
  который видит все нажатия;
- пока работает Fcitx, помнит имена программ, в которых стоял курсор (не больше,
  чем помещается в список приложений), без текста. Их, вместе с флагами
  состояния, отдаёт объект состояния аддона на сессионной шине
  (`fcitx://config/addon/rasklad/status`).

**Окно настроек**:
- читает и пишет конфигурацию аддона и читает группу методов ввода и объект
  состояния (с именами программ) через D-Bus Fcitx;
- читает список установленных приложений (`.desktop`), тему и язык Omarchy;
- через сокет Hyprland узнаёт скругление углов и выводит своё окно на передний
  план;
- запускает `rasklad-setup --check`, а `--install` и `--revert` — в отдельном
  терминале, где их видно.

Перед записью окно перечитывает конфигурацию у Fcitx. Если другая программа
успела изменить тот же список через Fcitx, окно ничего не пишет и показывает
текущий список. Это сужает окно гонки, но не закрывает его: у Fcitx нет
атомарной записи, а правку `rasklad.conf` прямо в файле, которую Fcitx ещё не
перечитал, окно не видит и перезапишет.

**Виджет панели** держит один процесс `rasklad-layout-watch`. Тот спрашивает у
Fcitx имя текущего метода ввода раз в полсекунды (3 секунды после смены — раз в
четверть секунды), а раз в 2 секунды читает объект состояния аддона целиком и
берёт из него два флага; текста там нет. Сигнал `Poke`, который может послать
любая программа на сессионной шине, вызывает внеочередное чтение и на 3 секунды включает частый
опрос; повторяя сигнал, программа может держать его частым. По щелчкам виджет запускает
`rasklad-layout-toggle`, `rasklad-pause-toggle` и `rasklad-settings`.

**`rasklad-setup`** меняет только `~/.config/hypr/bindings.lua`,
`~/.config/hypr/input.lua`, `~/.config/omarchy/shell.json`, ссылку на виджет и
группу методов ввода Fcitx, а также пишет свой журнал
`~/.local/state/rasklad-setup/state.json` и файл блокировки и вызывает
`hyprctl reload`:
- `--install` показывает разницу и спрашивает согласие; с `--yes` не
  спрашивает;
- `--revert` возвращает без вопросов, но только файлы, содержимое которых не
  менялось после установки (сверяется только содержимое: права доступа и ACL,
  изменённые позже, при замене не сохраняются, кроме битов режима). Правку файла или группы Fcitx, сделанную в миг между его проверкой
  и заменой, он не заметит и перезапишет: атомарного «сравнить и заменить» нет ни
  у файлов, ни у Fcitx. Группу он сверяет с тем, что знает Fcitx: правку файла
  `fcitx5/profile`, которую Fcitx ещё не перечитал, он не видит;
- из группы Fcitx откат и автооткат убирают только строки (метод и его
  раскладку), которые по последней сохранённой записи добавил сам
  `rasklad-setup`, и только если они ещё такие же и встречаются в группе один
  раз; остальное в группе, её порядок и раскладка по умолчанию остаются. Пустую
  группу он не пишет никогда, а пока панель использует виджет, раскладки
  остаются. Свои строки он записывает до записи в Fcitx, а снимает с записи,
  когда видит их удалёнными и сохраняет это. Строку, совпадающую с записью,
  откат уберёт, даже если её добавил пользователь: если запуск записал строку, но
  не дождался подтверждения, или если запись не узнала об удалении (не было `--install` или `--revert` — `--check` только читает; запуск
  прервался, Fcitx не ответил, диск полон; группу удалили и создали с тем же
  именем), а пользователь потом добавил такую же строку сам.
  Запись версии до 0.5.28 при первом чтении группы сверяется с ней: если группа
  ровно такая, какой та версия её оставила, добавленные тогда строки становятся
  обычной записью (если запись той версии в Fcitx когда-то не дошла, а
  пользователь добавил раскладку сам, это не различить); если группа вернулась
  к виду до той установки, записывать нечего; иначе группа остаётся как есть;
- копии кладёт рядом с файлом и в своём каталоге состояния, файлы заменяет
  атомарно;
- одновременно работает только один `--install` или `--revert` с тем же
  каталогом состояния (`--check`
  только читает и может идти параллельно);
- не заменяет настроечные файлы рабочего стола, которые сами являются
  символическими ссылками при проверке (например, из репозитория dotfiles;
  ссылку, подставленную между проверкой и заменой, он не заметит), и не читает файлы нерегулярного типа. Каталоги на
  пути, в том числе каталог плагинов Omarchy, могут быть ссылками: ссылку на
  виджет он создаёт и удаляет там, куда они ведут, а при `--revert`, проверяя,
  используются ли ещё виджет и блок `input.lua`, читает файлы и через ссылки.

**Чего Rasklad не защищает:**
- поле без пометки приложения (пароль, e-mail, URL, число и т. п.) неотличимо
  от обычного текста;
- веб-терминал или редактор кода на странице браузера не распознаётся;
- если страница переводит фокус в ту же миллисекунду, когда нажат пробел,
  исправленное слово может оказаться в новом поле
  ([docs/SYS-271-FOCUS-RACE.md](docs/SYS-271-FOCUS-RACE.md));
- двойной Alt Hyprland не перехватывает: приложение тоже видит оба нажатия Alt
  (в Slack и других приложениях Electron может мелькнуть меню);
- процесс того же пользователя может читать и менять всё, что доступно и
  Rasklad: его конфигурацию, файлы Hyprland, сессионную шину.

**Как сообщить об уязвимости.** Через приватный отчёт GitHub в репозитории
(Security → Report a vulnerability). Если он недоступен, напишите владельцу
репозитория. Не открывайте публичный issue и не прикладывайте набранный текст.

## История границ по этапам

Ниже — границы, которые добавлялись по мере работы, от первых опытов до
продукта, в том виде, как они были записаны тогда. Слова «сейчас» в них относятся
к своему этапу; текущее состояние продукта описано выше. Они остаются в силе для
соответствующих инструментов.

Сейчас проект содержит документацию, чистое ядро преобразования раскладки, экспериментальный n-gram scorer, production-маршрутизатор решений и окно ручного предпросмотра (SYS-239) с модульными тестами. Ядро не выполняет I/O, не логирует текст и не делает сетевых или системных вызовов. Отдельный offline-стенд читает явно переданные словари и паспорт данных, проверяет SHA-256 и выводит агрегированный JSON; он не подключён к пользовательскому вводу. Проект не устанавливает службы, не перехватывает глобальный ввод и не меняет систему.

Стенд опыта 002 дополнительно читает явно переданные тестовые TSV и их паспорт. По отдельному аргументу он сохраняет производные артефакты только внутри `build/`; режим измерения загружает один явно переданный артефакт без корпуса и обучения. Это инструменты разработки, не фоновые функции приложения. Корпус и артефакты не входят в Git и не допущены к выпуску; агрегаты без слов и весов проверяются отдельно.

Стенд опыта 003 опционально связывается с уже установленной Hunspell и читает только явно проверенные локальные .aff/.dic. Он проверяет паспорт, notices, контроль без PFX/SFX, версию и хеш загруженной библиотеки. Это проверка воспроизводимости доверенных зависимостей, не sandbox для вредоносной библиотеки; dynamic loader работает до этих проверок. CLI не устанавливает зависимости и не распространяет данные. Ресурсный Python-наблюдатель — отдельный developer tool: запускает явно заданный probe без shell и измеряет его PID; не является частью приложения. Проверочный прогон выполнялся отдельно без сети и с проектом read-only.

Опыт004 использует отдельный developer CLI с pinned моделью и локальными проверенными case TSV. Per-case output не содержит слов, но остаётся в ignored build/; публичны только агрегаты. Oracle markup-защита кода является условием эксперимента, не доказательством безопасного контекста системного ввода. Подготовка не выполняет Markdown/includes/команды или внешние ссылки.

Опыт005 добавляет только pure context policy и developer CLI над локальными синтетическими признаками. Gold labels хранятся отдельно и не открываются policy CLI. Protected/unknown — заранее предоставленные признаки исследования; они не проверяют реальный фокус или парольное поле. Suggest означает отсутствие автоматического изменения; runtime UI/ввод не реализованы. Чтения пользовательского контекста и накопления истории нет.

Опыт006 добавляет pure router policy и developer CLI router_decisions над одним проверенным шардом006 в формате признаков005 за запуск. CLI не открывает labels, исходные документы, модель и словари; per-row вывод без слов остаётся в ignored build/. Router-решения — материал исследования, не разрешение на автоматический ввод: Correct не проверяет фокус, парольное поле и доверенный контекст.

SYS-239 добавляет собственное окно Qt Quick (manual preview) и production-ядро `DecisionRouter`. Окно обрабатывает только текст, который пользователь сам ввёл или вставил в его plain-text поле; другие окна, устройства ввода и содержимое буфера обмена не читаются ни при запуске, ни в фоне. Собственная кнопка «Копировать» только записывает clipboard; стандартные команды редактора Ctrl+C/Ctrl+X/Ctrl+V выполняются по явному действию пользователя. Фонового чтения clipboard нет. Нет перехвата ввода, перепечатки клавиш, сетевых функций приложения, дочерних процессов, собственных IPC-интерфейсов, записи настроек, истории, автосохранения и логирования текста; кэш скомпилированного QML отключён. Текст не передаётся через argv. Ввод ограничен 65 536 UTF-16 единицами (превышение отклоняется целиком), проверка слова — 64 единицами, router — 32 каноническими клавишами. Preview применяется только явным «Применить» и только к тому снимку текста/направления/провайдера, для которого вычислен; отмена одноуровневая: массовая правка заменяет запись, одиночный ввод/удаление её сбрасывает; отмена старой Apply не затирает новую редакцию. Если native undo не удалось отключить, редактор остаётся read-only с сообщением об ошибке.

Тема Omarchy, выбранный язык `omarchy/language` и fallback `locale.conf` читаются только на чтение, обычными файлами до 64 KiB, по событиям QFileSystemWatcher без опроса. Флаг разработчика `--local-profile <корень>` читает только закреплённые по SHA-256 и размеру файлы под `<корень>/data` и `<корень>/build` без symlink-компонентов, сверяет паспорта, notices, отметки модификаций и загруженную системную Hunspell. Это проверка воспроизводимости доверенных локальных данных, не sandbox: Hunspell (C-код) разбирает локальные .aff/.dic, dynamic loader работает до проверки, а TOCTOU-подмена файлов процессом того же пользователя между проверкой и чтением не исключена (после загрузки файлы перепроверяются). Провайдер создаётся, опрашивается и уничтожается одним рабочим потоком; при завершении поток дожидается окончания запроса.

Ограничения: текст остаётся в памяти процесса (без гарантированного затирания) до очистки или выхода; скопированный текст попадает в системный буфер обмена, где его могут сохранить менеджеры истории буфера; средства доступности рабочего стола видят содержимое окна как у любого приложения. Это не аудит всей ОС. Первый offscreen-замер ресурсов опубликован в docs/research/MVP-001-REVIEW.md; реальные задержки desktop ввода не измерены. Решение маршрутизатора — подсказка в собственном окне, не разрешение на автоматический системный ввод (SYS-223).

В Qt TextEdit выделение обычно может автоматически публиковаться в PRIMARY. До QGuiApplication приложение добавляет primary-selection/data-control Wayland interfaces в собственный `QT_WAYLAND_DISABLED_INTERFACES`, сохраняя ранее заданные ограничения. Системные настройки не меняются. Если после инициализации `QClipboard::supportsSelection()` остаётся true, приложение завершается до открытия редактора. `--self-check` возвращает только backend/capability JSON, не создаёт окно, не загружает профиль и не читает содержимое clipboard. Проверено с установленным Qt 6.12/Wayland; изменение реализации Qt требует повторной проверки. Qt IME получает hints SensitiveData/NoPredictiveText; это подсказки, а не изоляция от IME, accessibility или других процессов пользователя.

SYS-223 добавляет отдельную developer-лабораторию `tests/input_lab` (`bin/test-input-lab`, не входит в `bin/build`/`bin/test` и не связана с приложением). Фейковый сервер libwayland и IM-клиент работают в одном тестовом процессе через socketpair: без слушающего сокета, `wl_display_connect` и сеанса рабочего стола; `wl_seat` без устройств; захвата клавиатуры, popup, виртуальной клавиатуры, clipboard, evdev и uinput нет, такие запросы отклоняются. Только синтетическое состояние и вымышленный текст. Результаты не являются доказательством безопасности реального композитора или приложений (docs/research/INPUT-LAB-001.md).

INPUT-LAB-002 — отдельный developer runner для уже установленного Hyprland. По умолчанию только проверяет план, явный `--smoke` использует закрытые namespaces bwrap, фиксированные окружение/config, без host HOME/runtime, GPU/input/DBus/сети, с лимитами и остановкой собственных процессов. Это не backend приложения и не sandbox для произвольного кода. Первый изолированный запуск не дошёл до наблюдения сокета; автоматический ввод не допущен. См. docs/research/INPUT-LAB-002.md.

Общая ревизия и границы доказательств: docs/research/PROJECT-AUDIT-001.md; лицензии: THIRD_PARTY.md. 65 536 UTF-16 — предел принятого документа после передачи текста из Qt editor, не жёсткий предел пика памяти при огромной вставке. Само обычное приложение не изолирует себя от остальных файлов пользователя; память может попасть в swap/core dump по политике ОС. Проверка символов ключей в Git не доказывает отсутствие всех секретов. Старые developer profiling helpers не являются supervisor для недоверенных команд и могут не иметь общего timeout.

## Требования к реализации

- Сборка и тесты работают в каталоге проекта без root, sudo, автозапуска, сетевых запросов и изменения настроек рабочего стола.
- Запуск без аргументов не должен запускать глобальный перехват ввода. Исправление системного ввода включается явно.
- Преобразование и определение языка выполняются локально. Не сохранять и не отправлять набранные слова, содержимое буфера обмена или историю ввода.
- Детектор не открывает сетевые соединения и не загружает словари/weights при запуске или в фоне. Применяется docs/DATA_POLICY.md: происхождение данных проверяется до их использования, обновления отделены от распознавания.
- Не открывать сетевые порты. Любое локальное управление ограничивать текущим пользователем, проверять входные данные.
- Не передавать набранные или исправленные слова через собственные IPC-интерфейсы или широковещательные сигналы. Исключение — явная пользовательская команда Copy/Paste через стандартный системный clipboard; это ожидаемый обмен с окружением, не фоновый сбор. Одинаковый UID не изолирует приложение от прочих процессов пользователя; управляющие вызовы требуют отдельного решения об авторизации.
- Не запускать оболочку с подставленным пользовательским текстом. Внешние процессы запускать с отдельными аргументами и фиксированными исполняемыми файлами.
- Не запрашивать членство в группе input или универсальный доступ к устройствам. Прежде чем выбрать механизм ввода, документировать минимальные права и возможность их отзыва.
- Не использовать setuid и не запускать приложение от root. Если минимально необходимый доступ нельзя обеспечить, остановить реализацию соответствующей функции и описать ограничение.
- При блокировке сеанса, неизвестном фокусе, смене приложения или ошибке прекращать исправление и очищать временный буфер.
- Не считать имя окна достаточной защитой парольных полей. До проверки надёжного контекста автоматическое исправление отключено.
- При остановке или падении приложения обычный ввод должен продолжать работать. Не захватывать устройство эксклюзивно.
- Системная установка, автозапуск и права доступа — отдельные явные действия с документированным удалением.

## Ограничения

Программа исправления ввода потенциально обрабатывает чувствительный текст. Ни отсутствие сети, ни открытые исходники сами по себе не гарантируют безопасность. До выпуска требуются анализ зависимостей, проверка прав и ручные проверки в реальных приложениях.

Автоисправление в разрешённых приложениях опирается на признаки поля, которые
сообщает само приложение. Поле пароля или веб-терминал без пометки «пароль» /
«чувствительное» Fcitx не отличает от обычного текста: в таком поле Rasklad
может исправить слово. Не добавляйте в список приложения с такими полями.
Любая программа того же пользователя может читать и менять настройки Rasklad
через D-Bus Fcitx (`SetConfig`), включая список исключений; это граница
доверия самого Fcitx.

Не публикуйте секреты и воспроизводящие их журналы в публичных issues. Для несекретных ошибок допустим минимальный пример с вымышленным текстом.

SYS-242 browser adapter — отдельный opt-in developer fixture, не устанавливаемое
расширение и не backend рабочего сеанса. Обрабатывается только textarea,
зарегистрированный доверенным владельцем как US prose; hints/фокус проверяются
до value/evidence, snapshots — после. Решение синхронное, текст не передаётся по
IPC и не логируется. Реальные profile/model/dictionary файлы не читаются.
Встроенная тестовая evidence живёт только в fixture, не является языковым
детектором. Native execCommand deprecated; unexpected result отключает коррекцию
без guessed rollback. Страница остаётся владельцем поля; адаптер не изолирует её
от её скриптов. Лимит textarea применяется после получения value. Системные
клавиши/раскладка, clipboard, настройки и host browser profile не меняются.
Тест Chromium — closed bwrap с временным HOME/profile, network namespace, без
host bus/input/GPU; Chromium sandbox включён, CDP только дочерние pipes. См.
[Browser adapter](docs/research/BROWSER-ADAPTER-001.md) и [границы](browser/README.md).

Browser core 002 добавляет build-time exporter: он исполняет существующий native
DecisionRouter на всех 512 состояниях синтетических признаков и canonicalizeToken
на BMP, не загружая словари/модель. В браузере применяется его конечная таблица,
а не IPC к desktop-процессу. Runtime binding проверяет gates до evidence, строгие
boolean/model поля, синхронность результата и bounds; Suggest не auto-edit.
Таблица и карты копируются в приватный snapshot factory. Это не изоляция от
скриптов того же JS realm и не проверка реальной языковой точности. Provenance
фиксирует хеши исходников developer build, но не заменяет подпись дистрибутива.
Exporter требует только уже установленный Qt/qmake/C++ toolchain и не устанавливает
пакеты. Детали: [Browser core 002](docs/research/BROWSER-CORE-002.md).

Browser evidence 003: RSKN loader принимает только фиксированный v1 Uint8Array
с собственным ограниченным снимком, отвергает shared/detached buffers, неверный
header/размер/веса. Исключения нормализуются без входных данных. Арифметический
JS scorer проверен по native gold, ключи не кэшируются; dispose зануляет только
собственную копию best effort, не гарантируя стирание swap/исходного буфера.
Проверка формата не доказывает provenance/SHA-256/лицензию. Runtime evidence binding
проверяет token/layout/canonical keys до локальных lookup и использует только
строгие bool ответы точных full/control/stem интерфейсов; неизвестный/ошибочный/
асинхронный lookup даёт отсутствие evidence, а не false membership. В fixture
только сгенерированные числовые веса и искусственные Set, не замена Hunspell.
Реальные артефакты не открывались/не допущены к browser distribution, сети/IPC/
истории/host config нет. См. [Browser evidence 003](docs/research/BROWSER-EVIDENCE-003.md).

ECC review 004: browser loader теперь допускает только frozen профиль порогов,
router повторно проверяет canonical соответствие кандидата, evidence снимает
конфигурацию одним чтением. Независимый native oracle передаёт явные evidence
поля, а не ожидаемый индекс browser table. Контракт owner-registration не
заменяется поиском password по всей странице. Статическое ECC-review и отдельно
исполненные Codex тесты разделены: [ECC review 004](docs/research/ECC-REVIEW-004.md).

RSKF browser stem-reader: фиксированный ограниченный снимок Uint8Array, проверка
всех смещений/префиксов/сортировки и точной длины файла до использования; shared,
detached и несовместимые buffers отклоняются. Держит compact payload и bounded
first-key index, не историю query. Временные копии при загрузке возможны; dispose
best-effort зануляет только собственную память. Это reader canonical stem-ключей,
не full/control Hunspell. Данные текущей fixture целиком искусственные.

2026-11-13 владелец явно выбрал MIT для собственного кода/документации, добавлен
LICENSE. Предыдущий pending-статус в исторических отчётах не является текущим;
правила допуска/лицензии сторонних файлов и данных остаются отдельными.

Опциональная browser full/control-обвязка получает уже созданный trusted module,
не выполняет fetch/код-загрузку. Проверяет UTF8/NUL/size до создания четырёх
handles в одном heap, удаляет временные metadata-only MEMFS файлы, не сохраняет
запросы. Query UTF16 1..32, without Cc/unpaired surrogates; временные UTF8/Cstring
копии зануляются перед free best effort, не внутренние allocator buffers engine.
Trap отключает профиль; dispose/ошибки constructor чистят зарегистрированные
handles. Provenance/version, source offer/notices и ограничение максимального
WASM heap ещё не допущены к выпуску. Candidate lock release_approved=false;
никаких бинарников/чужого кода в Git или default runtime.

Закрытый browser fixture теперь монтирует только build/browser-fixture, не весь
проект/build; никаких auth/cache/реальных артефактов ему не открывается. Общее
CDP управление выделено в isolated-chromium.mjs и ожидает loadEventFired до
использования документа. Подробнее: browser/README.md (opt-in full/control).

## Language package preparation

`tools/language_pack.py` is an explicit offline developer CLI. Its reviewer-owned
catalog is the trust anchor; a downloaded package cannot approve itself. Validation
checks exact bytes/hash, full notice files and a mandatory modification record.
Distribution and local-test grants are independent and distribution remains denied
for the current RU/EN records. There are no executable hooks, downloads or automatic
language activation. File access uses bounded regular-file reads and descriptor-based
no-follow traversal. Linux publication uses renameat2 NOREPLACE; fallback uses exclusive
output mkdir. Failed preparation rolls back only entries it created, not arbitrary
recursive directories. This is not a sandbox against hostile code with the same UID.
Standalone verification is a snapshot check, not a lock against later modification:
a future runtime consumer must use the verified bytes, not reopen mutable paths.
No real dictionaries/model/user text were read by the package tests.

## Prepared dictionary compatibility smoke

`bin/test-reviewed-dictionaries` defaults to project-authored fictional data.
Its explicit `--real --input-root` mode is prepared for a separately authorized
one-shot read of the four catalog-pinned .aff/.dic files, not an implicit reuse of
an old test permission. The launcher mounts exactly those files read-only and
selected code/notices into an outer networkless bwrap; no project tree/HOME/model
is mounted. Raw source bytes are hash/size checked before package assembly.
The native oracle receives pinned immutable snapshots via stdin in its explicit
`--stdin-profiles` mode; default mode remains synthetic. The browser has a nested
sandbox and only an empty fixture directory. Developer CDP carries public dictionary
bytes and fixed smoke cases, never user input. Network and host session access
remain absent. Only aggregate results are printed. Unique ignored output directories
hold raw local packages and their notices, never overwrite old runs, and are not
release-approved. The resource report measures WASM heap capacity, not process RSS;
batch lookup means are not physical keystroke latency. A separately authorized real-data run completed on 2026-11-13; aggregate results
and consumed one-shot scope are recorded in data/language-pack-smoke-001.json.
Default remains fictional and further raw-input runs are not automatically authorized.

The native stdin oracle now reads bounded chunks to EOF instead of assuming one
pipe read contains the entire input. Error reports expose only enumerated stage
codes (pins/package/native/browser), never dictionary words, stderr or paths.
A large fictional-input regression covers the multi-chunk case. The completed
real run kept the same four read-only inputs and no model/seat/network access.

## Verified session / scoped local editor integration

The separately confirmed editor step reads only the four catalog-pinned raw files,
existing pinned RSKN model/RSKP pair and their metadata/notices in isolated bwrap.
It does not reuse the earlier raw-smoke authorization, train, access corpora/user
text or install anything. The review and consumed operation/result are recorded
in data/browser-profile.json and data/browser-editor-001-result.json.

Session inputs are bounded owned snapshots before async hashing. Every listed file,
including notices, must match the trusted manifest before any component initializes.
Model/stem formats must pass before verified third-party engine code executes.
Dependency callbacks and the manifest/digest implementation are trusted; a malicious
same-realm script or attacker-owned catalog is outside this boundary. No downloaded
manifest self-approval or implicit distribution permission. The page owner remains
responsible for the registered prose field and protected spans.

Atomic lifecycle cleanup removes adapter hooks and disposes engines/model/stems on
initialization failure or runtime engine failure; there is no fallback evidence.
Exactly four handles and valid heap capacity <=128 MiB are required. Temporary byte
copies are wiped best effort, without claims about allocator copies or swap.
Registration occurs only after ready; queries remain synchronous and unlogged.
The owner-side test uses fixed fictional text, no messaging account or host profile.
The browser receives only minimal editor HTML plus explicitly verified in-memory
assets; outer mounts do not expose model/seat/home directories as a tree.

## ProseMirror owner adapter

The owner passes public editor/history APIs and an explicit range classifier.
No registered context is inferred from a window title or DOM appearance. Metadata
(code/link/current stored marks, role, credential form, focus, composition) gates
all bounded prefix extraction. Exact EditorState identity and registration are
rechecked after synchronous evidence, including revocation/reconfigure. Per-run
marks are retained and native Space marks applied separately. Native history
boundaries provide Undo/Redo without execCommand/replay. WeakMap records contain
only busy/disabled flags, no queries/documents. Stale/unexpected owner changes are
not rolled back; an exceptional Space may be consumed rather than misapplied.
The verified third-party editor is executed only in the closed fixture; it is not
installed into Slack. Details: docs/research/PROSEMIRROR-ADAPTER-005.md.

## Native Slack target

The owner chose native desktop Slack. The metadata-only developer probe scopes
accessibility peers to that executable and whitelists four non-text methods. It
cannot request text/names/attributes/values, edit/focus/actions or register event
listeners through its guarded reader. Only counts/enums/booleans leave the probe;
peer IDs and object paths do not. The current snapshot had no matching Slack a11y
connection and queried zero UI nodes. Native correction is NOT enabled.

A separately reviewable launch helper defaults to a plan and never kills a running
Slack. Its optional explicit run requests only force-renderer-accessibility, no
config/app patch or debugging port, and starts no corrector. This exposes UI to
local assistive clients and uses Slack's normal app profile; it requires owner
approval before launch. The probe still reads metadata only. Trial/rollback and
unresolved buffer/Undo/code-role gates: docs/research/SLACK-DESKTOP-001.md.

## Fcitx transport developer lab (SYS-244)

This separate optional lab is not loaded by bin/build, bin/run or the host IME.
The addon is a project-authored synthetic preedit rule, scoped to the fixed named
lab client; program-name matching is not authentication and is not a production
security boundary. It must never be presented as generic prose/password detection.
The launcher defaults to a plan; explicit --build compiles local sources, and
--run requires bwrap with no unsandboxed fallback. The private namespace has no
host HOME, runtime, D-Bus, display, GPU/input devices or network. Only fixed addon,
client, runner and private session-bus configuration files are bound read-only,
alongside installed system runtime. Fcitx enables only keyboard, dbus,
dbusfrontend and the lab addon; no cloud/dictionary/notification/autostart addons.

The offscreen Qt fixture explicitly invokes the real installed platform input
context filter, performing the responsibility of a native keyboard backend. Its
synthetic key events travel through the real Qt Fcitx module and private D-Bus
to a real Fcitx daemon; QInputMethodEvent/preedit/commit are received from that
module, not fabricated. This can demonstrate transport but not ordinary physical
input, compositor routing, native Slack compatibility or arbitrary editor safety.
Qt QPA private API usage is version-specific developer code.

Only fixed project-authored test strings are processed; no model/raw dictionaries
or user content is read. Public reports contain aggregate booleans/counts and
source/environment hashes, not real text. Shutdown targets only child processes
in the lab's private namespace; no existing Slack/Fcitx process is signalled.
Focus/capability loss clears lab state. Crash and incomplete-preedit behavior are
measured rather than assumed lossless. Host integration, permissions, installation
and rollback require a later concrete review; no release approval is implied.

## Real Chromium IM transport lab

The optional chromium-ime lab uses an official signed/hash-pinned project-local
Xvfb binary, private X server, private D-Bus/Fcitx and disposable Chromium profile
in bwrap --unshare-all. No host display/profile/seat/bus/input/GPU/network. Fixed
synthetic XTest events target only the owned, viewable private browser PID; this
is not a permitted system replay fallback. CDP Input.* is forbidden. The browser
keeps its sandbox; its shared-memory files need a larger128MiB file cap.

The separate addon compile variant gates program=chromium, not the default Qt
fixture. It is synthetic developer code, not a generic context classifier or a
production app allowlist. Provisional composition text can already exist in DOM.
No raw dictionaries/model/profile/account/message data are read by this test.
See docs/research/CHROMIUM-IME-001.md and data/chromium-ime-001-result.json.

## Optional native evidence bridge in Chromium lab

The separate --run-profile action requires an explicit input root and binds
exactly20 known profile/metadata/notice files read-only; default synthetic run
never reads these inputs. Existing productionPins/loader validate all hashes,
formats, provenance notices/control pairs and loaded Hunspell before queries.
No new data/training or distribution approval. The bridge has no synthetic
fallback; a deliberately invalid virtual manifest leaves ordinary input intact.
Only the explicitly owned lab prose context supplies Prose; program name is not
a semantic classifier. CPU/RSS and crash-loss limitations are recorded in
docs/research/CHROMIUM-IME-PROFILE-001.md. Current composition is lost on abrupt daemon
death; live use is not approved by passing the transport scenarios.

## Native focus stress developer checks

`--focus-stress` is an explicit lab option; it is never enabled by normal app
startup. Reference Sway remains no-host-display/GPU. The separately approved
nested Hyprland runner exposes one render node, a RO/NODEV primary identity and
a reconstructed minimal sysfs, not host HOME/bus/input/KMS or a whole sysfs bind.
No normal IME or Slack is connected. Reports contain fixed case IDs, booleans,
bounded counts and fixed value-class enums; no field text or raw event data.

The strict baseline now requires original-word preservation on blur. Four
additional owned-DOM scenarios use captured-node/state identity, bounded event
barriers, atomic final predicates and verified cleanup. Cleanup failure marks
dependent cases not run. All runtime typing uses fixed private VK commands;
JavaScript never fabricates composition/keyboard/input events.

Chromium155's observed native compositionend has isTrusted=false. The suite
defaults to trusted_only; its scoped-end exception requires reviewed source
SHA pins, exact browser version and a real VK positive control. This is closed
fixture provenance, NOT authentication of arbitrary DOM events or permission
to weaken production input gates. Text/focus/count predicates stay strict.

The suite exposed our early empty-preedit notification allowing a reentrant
hook to move a payload into another field. Claude's fix stages local state,
commits payload before notifying preedit end and guards the remaining call
with tracked lifetime/focus. Sway, Qt and GTK regression pass; this is not an
atomic multi-message transaction, stale-serial proof, or generic app guarantee.
Exact Hyprland revalidation after this change remains pending. See
docs/research/FOCUS-STRESS-PLAN.md and data/focus-stress-reference-001.json.


SYS-245 addon witness — отдельный compile-time developer вариант, не рабочая
функция. Обычный preprocessed profile code не меняется. В закрытой fixture
readonly GetStats на уже private bus возвращает только fixed bounded counters;
key callback не делает I/O/логирование и не сохраняет payload. Нет setter/reset
или input API. Тестовый вариант не устанавливается, требует явных private
markers и отдельного бинаря. Ограничения корреляции/насыщения и позитивный
Sway контроль: [Addon witness](docs/research/ADDON-WITNESS-001.md).


Core-entry witness — следующий явно выбранный developer вариант с собственным
ELF wrapper только в закрытом Fcitx child. ABI закреплён хешами; constructor
удаляет preload env для потомков. Строгая thread confinement защищает простые
counters. Default/profile не содержат bridge; runner/Node/Chromium не получают
preload. Контракт, guards, калибровка и ограничения: [Core entry](docs/research/CORE-ENTRY-WITNESS-001.md).

## Program names on offer in the settings app (SYS-253)

The product addon remembers the program names (`InputContext::program()`, as the
client claims them) of input contexts that got focus since Fcitx started. It
keeps names only, in memory only: they are never saved, logged or sent over the
network, and no text is read. Other processes of the same user can read them over
the session bus, as described below. Terminals and the settings app itself are refused; at most
64 names are kept, and the least recently focused one goes when the list is full.
The list is exposed as `SeenApplications` in the read-only status sub-config, so
any process of the same user on the session bus can read which applications got
a focused field. That includes apps where only a password field had focus: the
name, never the field or its text. This stays inside the same-user boundary
above; Fcitx `DebugInfo` already lists the programs of live input contexts. The
names only populate the offer list. Correction is enabled only for applications
the user switches on, and a client can claim any name, as before.

`tools/program_name_probe.py` is a developer probe, not part of the package. It
runs a headless Sway from the pinned lab runtime, a private `dbus-run-session`,
its own Fcitx and a throwaway HOME, with an environment reduced to `PATH` and
`LANG`. It launches installed system applications (`foot`, `omawrite`,
`nautilus`) and reads only program names, frontend, capability flags and Sway
app ids. It does not use bwrap, so those applications can read what the user can
read. Do not add applications that open the owner's data.

## Programs the settings app starts (SYS-254, SYS-260)

The settings window starts no shell of its own and passes no text the user typed to a program.

**`/usr/bin/rasklad-setup --check`** (absolute path) runs on each refresh.
- A refresh happens on window focus, at most once every 3 s while Fcitx answers. While Fcitx is down, a refresh can happen on every focus.
- The check runs asynchronously and never blocks the window. Only one runs at a time.
- stdin and stderr go to `/dev/null`.
- The tool is killed after 60 s (more than its explicit per-call limits add up to), or as soon as its output passes 8 KiB. A killed or crashed check counts as no answer: the status rows go; the section hides unless a setup record exists, and then only the Restore button stays.
- The output is reduced to known step and state words; notes are dropped.
- The tool reads the user's own Hyprland, bar and Fcitx settings, reads no input, and changes nothing.
- Only the direct child is killed. A helper the tool starts, such as `hyprctl`, is not killed and can outlive the timeout.

**«Подключить…» and «Вернуть как было…»** start `/usr/bin/omarchy-launch-floating-terminal-with-presentation` with one of two fixed strings, `/usr/bin/rasklad-setup --install` or `/usr/bin/rasklad-setup --revert`. That Omarchy script runs the string with `bash -c`, and it finds its own helpers (`omarchy-show-logo`, `omarchy-restart-gum`) through PATH.
- Without that script, `/usr/bin/xdg-terminal-exec /usr/bin/rasklad-setup --install` (or `--revert`) is used.
- Clicks less than 2 s apart open one terminal, not several.
- The changes happen in that terminal. `rasklad-setup` (SYS-252) shows the diff and asks before applying. It applies nothing when stdin is not a terminal.
- The window itself writes no Hyprland, bar or Fcitx group file.

**Hyprland's `decoration:rounding`** is read once, over the Hyprland request socket. It does not run `hyprctl`. The read is non-blocking and bounded by an 800 ms deadline.

**One window (SYS-261).** Only when `$XDG_RUNTIME_DIR` is an absolute directory owned by this user with mode 0700; otherwise every start is a separate window.
- The first window holds `rasklad-settings.lock` there for its whole life (`QLockFile`, which detects a lock left by a crashed process) and listens on `rasklad-settings.socket` (`UserAccessOption`).
- Only the lock holder removes a leftover socket, and only when it is a socket owned by this user.
- A later start finds the lock taken and connects; the connection itself is the request. It never opens a second window while the first holds the lock.
- The first window ignores what it receives and asks Hyprland, over the request socket, to focus its own pid: `hl.dsp.focus({ window = "pid:<n>" })`, then the classic `focuswindow pid:<n>`, as Omarchy's own scripts do. The pid is a number the window gets from its own process.
- Nothing received on the socket reaches a command or a setting.
- The first window answers `ok` and stops listening as it closes. A later start that gets no answer keeps trying for 5 seconds: once the closing window releases the lock, it takes it and opens its own window. If the lock is still held after 5 seconds, it reports an error and exits with status 1.
- Residual risk: between the check of the socket path and Qt publishing the socket (by `rename`), a file created there could be replaced. Only this user can create files in the 0700 runtime directory, so only the user's own processes can race it.

## License texts in the settings window (SYS-256)

The «Лицензии» page reads only four fixed file names (`LICENSE`, `en_US.README.txt`,
`ru_RU.README.txt`, `DATA-NOTICE.txt`) from `/usr/share/licenses/rasklad-fcitx5/`.
QML passes a key, never a path. Before opening, the named file is checked: a
symlink, a non-regular file or a file larger than 64 KiB is not read, and at most
64 KiB + 1 bytes are read. The texts are shown as plain text, never as rich text or links.
