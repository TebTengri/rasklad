# Лабораторный свидетель ветки addon

Текущий диагностический контракт — schema2 (описан в конце).
Разделы schema1 и результаты001–005 ниже сохранены как история экспериментов.

2026-11-15, SYS-245. Предпосылка: exact trial003 подтвердил case2 FAIL,
ALL-key/input trace пустой и после500ms event wait. Причина не установлена.
Повторно расширять DOM trace не нужно; следующий вопрос — достиг ли Space
нашего обработчика и какую ветку он выбрал.

## Реализация и границы

Claude Opus5.5 реализовал отдельный compile-time RASKLAD_WITNESS_LAB вариант.
Codex исправил include для установленного API, выбрал rawKey.code и добавил
явный opt-in environment marker; интегрировал readonly reader, oracle и тесты.
fcitx_transport_runner интегрировал Python flags/guards; независимые reviewer
проверили C++ и диагностическую последовательность.

Вариант tools/fcitx_witness_lab.pro включает прежний профиль, добавляет
Fcitx5Module API и witness macro. Файл tools/fcitxlabwitness.h содержит20
насыщаемых255 счётчиков в памяти. В обработчике нет записи файлов, вывода,
IPC, часов или выделения памяти. Сравнивается только фиксированный Space
raw code65; код, symbol, текст, context/app IDs и timestamps не сохраняются.
На private bus один метод /org/rasklad/LabWitness,
org.rasklad.LabWitness1.GetStats без аргументов возвращает schema1, enabled1
и20 uint32. Нет setter/reset, input frontend, текстовых methods/properties.
Сериализация происходит только в getter, не в key callback.

Endpoint требует HOME=/home/lab, runtime=/run/lab, NATIVE_WAYLAND_LAB=1 и
ADDON_WITNESS_LAB=1; DISPLAY/WAYLAND_SOCKET/PARENT_FD должны отсутствовать.
Runtime config raskladwitness.conf явно зависит от dbus для порядка выгрузки.
Guard — проверка fixture, не замена namespaces: режим не устанавливается
в рабочий сеанс. Никаких новых устройств, сети или host bus.

Обычная профильная сборка после препроцессора побайтово совпадает с6d5ae19:
все witness вставки исключены. GetStats отсутствует в её артефакте; отдельный
witness artifact его содержит. По-прежнему ничего не устанавливается.
Используется публичный API Fcitx; код upstream/словарей не копировался.

## Как снять delta

--addon-witness требует --focus-stress --space-telemetry. По умолчанию CLI
только показывает план. Binary/config/module выбираются по фиксированным
путям, бинарь снимается в sealed snapshot перед запуском.

1. Baseline GetStats до case2 word, затем обычный preedit precondition.
2. Неизменная быстрая последовательность focus other→prose→единственныйSpace.
3. Первичный строгий oracle; только при отказе прежний bounded input wait
   и отдельный late snapshot.
4. GetStats после oracle и до cleanup; отдельный post-query DOM snapshot.
5. Cleanup не входит в delta. Исходный FAIL поздним совпадением не исправляется.

Getter не атомарен с DOM и сам меняет timing. Нулевая delta означает отсутствие
наблюдения только ДО getter, не вечную потерю. Счётчики любых lifecycle
событий относятся ко всей закрытой fixture, не к произвольным приложениям.
255 делает соответствующую delta неизвестной (-1), complete=false; endpoint
error/schema mismatch/reset означает invalid, а не ноль.

spaceCodePressSeen означает достижение НАШЕГО PreInputMethod watcher, не
любого frontend callback: ранняя фильтрация может не вызвать watcher.
raw code65 с иным symbol может попасть в letter branch; неполная сумма
decision counters в этом случае не означает passthrough. commitStringCalls
считает все вызовы helper, не только Space; это попытка, не proof доставки.

## Закрытая проверка

Artifact собран; 52 Python guards +19 oracle +19 telemetry +8 witness tests
проходят. Дополнительно C++ negative environment controls без запуска служб
и сравнение обычного preprocessed code. Private Sway baseline17/focus4/4
pass. В case2 delta: Space press/release1/1, normalized1, focused1, eligible1,
passEmpty1, commitSpace0, commitString0, FocusIn2/Out2; getter валиден,
счётчики не насыщены. DOM получил Space; post-query oracle100.
Полные агрегаты: data/addon-witness-preflight-001.json.

## Следующий разрешаемый опыт

Тот же exact Hyprland runner с --addon-witness, до75s и прежними границами:
одно parent Wayland соединение, renderD128, primaryRO/NODEV, synthetic metadata,
без Slack/hostIME/config. Предыдущий допуск003 использован, нового нет.

- press + nonempty + commit: проверять lifecycle/согласование preedit;
- press + empty + pass: исследовать frontend forwarding после addon;
- press + unfocused: исследовать переход активности;
- нулевой press: проверить routing/grab и полноту witness до выводов.

Диагностический PASS не является исправлением: вариант способен менять гонку.
readyForLiveUse=false, normal Slack не подключён.

Независимые fcitx_addon_review (C++) и focus_suite_critic (JS) завершили
статическое ревью без блокеров; самостоятельно тесты и live-пробы не запускали.
Дельта покрывает word→focus→Space→getter целиком. Монотонность обнаруживает
сброс лишь при уменьшении: её отсутствие само по себе не доказывает тот же PID.

## Exact Hyprland trial004: PASS без исправления

Отдельно разрешённая ОДНА пробаb744238 выполнена. Baseline17/focus4/4 pass,
case2 strict primaryPASS, Spacepress/release1/1 normalized/focused/eligible1,
passEmpty1 commit0, FocusIn/Out2/2. Getter и DOM trace валидны, не насыщены;
post-query oracle100. data/hyprland-addon-witness-004-result.json.

Это НЕ установленное исправление: correction logic не менялась. По сравнению
с прежним normal вариантом одновременно изменились diagnostic binary/layout/
инкременты, явная runtime dependency dbus и before-word getter. Late getter
после primary не может объяснять уже случившийся primaryPASS. В этой пробе
нет failed ветки для локализации. Частоту/детерминированность не оценивали.

Следующий минимальный контроль — та же witness binary/config/private markers
и imports, но БЕЗ GetStats queries. Сопоставить с обычным вариантом в отдельной
ограниченной паре. No-query не выключает C++ counters, только их чтение;
никаких новых логов, barrier/sleep/retry/прав. Сам control может также изменить
тайминг; один исход недостаточен для установления причинности. Допуск004
использован, новых host запусков нет.

## Контроль без запросов и ограниченное сравнение

Claude подготовил no-query factory; Codex интегрировал и проверил, что ни
query getter, ни вызов не выполняются. При --witness-no-query сохраняется
вся witness binary/config/private env, модуль импортируется тем же способом,
но объект наблюдения null: GetStats не вызывается. Отчёт отдельно отмечает
instrumentedAddonTrial и addonWitnessQueriesEnabled. Условия oracle прежние.
Флаг требует --addon-witness и не добавляет доступов.

Подготовлена ОДНА последовательность A→B→C, каждое новое закрытое окружение:
обычный вариант; witness без queries; тот же witness с queries. Максимум
3×75s=225s, один parent connection/render node одновременно. Аргументы и
хеши артефактов data/witness-comparison-plan-001.json. Ничего не выполнено.
Нужен отдельный допуск на эту конкретную тройку, а не автоматические повторы.

Это не полный factorial и не доказательство причинности по одному исходу.
A/B меняет весь bundle, включая явную dbus dependency и регистрацию endpoint.
Если AFAIL/BPASS/CPASS, следующий контроль только конфигурационный, не
приписывать эффект инкрементам. Если AFAIL/BFAIL/CPASS, queries становятся
направлением проверки, не найденным механизмом. При всехPASS/нестабильных
результатах воспроизводимость изменилась, фикса нет. Peer.Ping пока не добавлен.

No-query preflight завершён:55Pythonguards и9reader tests проходят. Private
headless Sway baseline17/focus4/4 PASS, instrumentedAddonTrial=true и
addonWitnessQueriesEnabled=false; ни в одном case нет counter sample.
C++/бинарь/config прежние. Агрегат data/witness-noquery-preflight-001.json.
Исходные19oracle/19telemetry tests ранее проходили и в этом изменении не
затронуты. Независимый static JSreview без блокеров, reviewer не запускал тесты.

## Согласованный triple005: три отказа

Выполнены ровно A→B→C по плану, без повторов. Все baseline17 и три прочих
focus cases прошли; case2 FAIL во всех arms, compositor/cleanup валидны.
Серия полностью использовала3 допуска, remaining=0. Агрегат
data/witness-comparison-005-summary.json и три отдельных result JSON.

C full witness поймал отказ: code65press delta0, release delta1; все
press-ветки и commitString delta0. FocusIn/Out2/2, capabilityChange16,
Reset0. Getter валиден/ненасыщен; primary/late/post-query0, ALL DOMtrace[].
Следовательно, зарегистрированный нашим watcher Space press не выполнял
коррекцию/commit. Счётчики не идентифицируют пару событий; release нельзя
безусловно приписать именно этой Space-команде. Raw-code remapping, ранняя
фильтрация и поздний callback остаются возможными. Предыдущий PASS004 не
был устойчивым эффектом getter: C снова FAIL. Частоту не оцениваем.

Exact Fcitx5.1.23 postEvent прекращает handlers/phases после filtered;
InputContext::keyEvent возвращается до dispatch без фокуса. Публичный
watchEvent запрещает ReservedFirst. Поэтому просто переносить watcher
раньше нельзя. Ctrl+Space trigger выполняется после нашего PreInputMethod,
а ранний togglePreedit вызывает Reset, которого delta не показала. Это
source-review, не runtime-исключение всех возможных фильтров.

Минимальный следующий разделитель — два bounded totals всех press/release
в существующей закрытой fixture. Ожидаемый обычный интервал —6letters+Space,
7/7;6/7 поддержит пропущенный handler press,7/7 приSpace65press0 — изменённое
представление. Иные totals неоднозначны. Никаких rawkeys/текста/newrights.
Для этого нужен versioned schema2, чтобы старый бинарь не выдавал ложные нули.
До newprobe — реализацияClaude, ревью и Swaypositivecontrol.

Hyprland duplicate-suppression и отбрасывание события старого grab остаются
гипотезами. Для первого нужен доказанный route transition +stale pressed-state;
в стабильномUSEIME ветка m_pressed не используется. Для второго надо ещё
проверить libwayland proxy-destruction semantics. Не менять систему по догадке.

## Schema2: totals без raw-кодов

По результату005 Claude Opus5.5 предложил минимальное расширение: два
счётчика allKeyPressSeen/allKeyReleaseSeen, инкремент в самом начале
существующего handler до проверкиcode/gates/filter. Только isRelease bool,
никаких code/sym/text/time для этих двух счётчиков. Старые20индексов сохранены,
новые20/21, всего22счётчика. GetStats возвращает24uint32: schema2, enabled1,
22значения. JS отклоняет schema1 и неверную длину; saturating255/-1 прежние.
Read-only/private-bus/runtimeguards и no-query control не меняются.

Codex интегрировал предложение и исправил отрицательный enabled-test по
замечанию двух независимых reviewer: schema2/disabled0 проверяется отдельно
от корректного старого schema1/20counter reply. 9reader tests проходят.
Независимые C++/JSreview без блокеров; reviewer тесты не запускали.
Обычный profile preprocessed code побайтово прежний (SHAf5d53921...).
C++negative environment controls повторно прошли без запуска служб.

В before-word→after-oracle интервале обычный fixed stimulus содержит6букв
иSpace: ожидается7/7 при нормальном потоке без лишних/повторных событий.
6/7 поддерживает отсутствие одного press в handler до getter;7/7 при
Space-codepress0 поддерживает иной code, но не исключает дополнительное
событие вместо недостающего. Это не pairing и не доказательство frontend loss.
Иные числа требуют отдельной трактовки, не разрешать ошибку ради PASS.

Никакого вмешательства в установленные Fcitx/Hyprland, LD_PRELOAD или
переноса watcher в запрещённую ReservedFirst фазу. Если totals подтвердят
пропуск до handler, только затем рассматривать узкую точку наблюдения
frontend, не общий протокольный лог. Новый host допуск ещё не получен.

Закрытый положительный контроль схемы2 завершён: Sway baseline17/focus4 pass,
валидный ненасыщенный getter дал allKeyPress/Release=7/7 и Space-code1/1.
Данные data/addon-witness-schema2-preflight-002.json. Бинарь schema1 сохранён
в ignored build с прежним хешем для воспроизводимости; активная диагностическая
сборка теперь schema2. Новый host опыт ещё не выполнялся.
