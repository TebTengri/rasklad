# Лабораторный свидетель ветки addon

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
