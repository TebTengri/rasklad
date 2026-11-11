# Методика опыта 003 (SYS-235)

[Протокол](../data/experiment-003-protocol.json) и [критерии](EXPERIMENT-003-PREREG.md) зафиксированы до оценки. [Результат](EXPERIMENT-003.md) опубликован отдельно. Это явный offline CLI, не фоновая функция приложения.

## Зависимости и сборка

`bin/build`, `bin/test -silent`. Старые 420 тестов не требуют Hunspell. Если `pkg-config --exists hunspell` успешен, qmake дополнительно собирает `morphology_benchmark` и десять reviewer-тестов `tests/morphology`; иначе сборка и bin/test явно сообщают о пропуске. Ничего не устанавливается и не скачивается. Для воспроизведения именно этого опыта нужны уже установленная Hunspell 1.7.5 и совпадающий binary hash; обновление библиотеки требует нового проверенного паспорта, а не отключения контроля.

Функциональный C++ написал Claude: `hunspellengine` (RAII C API), `morphologydata` (протокол/паспорт/control), `morphologyeval` (девять методов/live пути), `morphology_benchmark` (оценка и probes). Переиспользуются helpers опыта 002. Codex написал независимые тесты, скрипты подготовки/наблюдения и документацию. Чистое ядро `src/` не изменялось.

## Данные и подготовка

Нужны прежние `.dic` из [sources.json](../data/sources.json), подготовленные `en.tsv`/`ru.tsv` из [evaluation-002-sources.json](../data/evaluation-002-sources.json), а также `.aff` того же pinned LibreOffice commit. Первичные условия .aff и Hunspell проверены отдельно в [review](../data/notices/MORPHOLOGY-003-REVIEW.md). Все размеры, хеши, библиотека и notices — в [morphology-003-sources.json](../data/morphology-003-sources.json). Release approval отсутствует. Получение файлов — отдельное явное действие; этот документ не является командой фонового скачивания. Mutable Tatoeba export URL не гарантирует старый snapshot: нужны байты с указанными хешами.

Control готовится только в новый файл внутри `build/`:

```sh
python3 tools/prepare_affix_control.py \
    --input build/experiment-003-data/en.aff \
    --output build/experiment-003-data/control/en.aff
python3 tools/prepare_affix_control.py \
    --input build/experiment-003-data/ru.aff \
    --output build/experiment-003-data/control/ru.aff
```

Скрипт сохраняет все байты строк кроме строк с первым полем PFX/SFX. Исходник не изменяется. Уже существующий output — отказ. Рядом с исходниками и производными должны сохраняться полные notices и пометка MODIFICATIONS; их копирование — явная подготовка данных. Стенд независимо воспроизводит control из исходного aff, проверяя равенство байтов, а не только hash предоставленного control.

## Оценка

Из корня проекта:

```sh
build/tools/morphology_benchmark \
    --manifest data/sources.json \
    --en build/experiment-data/en.dic \
    --ru build/experiment-data/ru.dic \
    --eval-manifest data/evaluation-002-sources.json \
    --eval-en build/experiment-002-data/en.tsv \
    --eval-ru build/experiment-002-data/ru.tsv \
    --protocol data/experiment-003-protocol.json \
    --morph-manifest data/morphology-003-sources.json \
    --json > build/experiment-003.json
```

Ровно эти опции, без повторов/неизвестных аргументов. Нет режима сохранения модели или изменения данных. Passport roots совпадают. Проверяются протокол структурно целиком, статусы local-only/release=false, размеры, SHA-256 исходников и notices. Дополнительные .aff/.dic пути только внутри canonical `build/` проекта; control внутри `build/experiment-003-data/control/`; загруженная библиотека совпадает с canonical `/usr/lib/` паспортом. `.dic` проверяются потоково с пределами до Hunspell_create; UTF-8/размер/число строк ограничены. После создания engines исходники проверяются повторно. Это контроль целостности доверенных локальных файлов, не защита от произвольного процесса того же UID или вредоносного dynamic loader.

Коды: 0 — успех, 1 — неверные данные/паспорт, 2 — неверные аргументы, 3 — нарушение целостности/контрольного инварианта, 4 — ошибка stdout. JSON печатается после полного успеха; на отказе stdout пуст. Собственные сообщения не выводят текст/ID/пути. Диагностика сторонней библиотеки не считается гарантированно очищенной; в изолированном одобренном прогоне stderr был пуст.

Проверка selection/model/protocol hashes выполняется до анализа качества. Выбор нового блока — ranks 5000..9999 старого ранжирования, не новый seed. Хеши ID-последовательностей (decimal ID + LF в порядке отбора): EN `5bdb19f6c3425329cc87180edf5db3824302f304051633eb91ea8487ffb58c33`, RU `523357a3d47b6f5f38865b93340dbfd40da572878c93ef09aa279ff8384473d8`. Хеш протокола `cc4093434e258639b0c9b2c8eafcc6db76b73b6560d5f54e17872a130d633643`. Сами IDs и предложения не публикуются.

## Семантика и отчёт

Все методы описаны в протоколе. Exact: observed hit → Keep, иначе alternative hit → Correct, иначе Abstain. Fallback вызывает twoScore только после Abstain. Confirmed: observed hit → Keep, иначе Correct только при alternative hit и согласии модели. Guard: supported и минимум четыре ключа; alwaysKeep оставляет всё. Hunspell получает точный case-preserving текст, не lowercase вариант старого stem индекса. У него есть собственные правила case/нормализации, поэтому контроль PFX/SFX сравнивается именно с тем же движком.

Общая union ambiguity применяется ко всем девяти методам и обоим способам счёта. Метрики occurrences и exact-case types, raw change totals, paired deltas, срезы длины/языка/stem/control/full membership и acceptance находятся в JSON. `neither` включает guard/unsupported, это не автоматически pure OOV. `expectedPrecision` — аналитические сценарии с частотой wrong 0,1%/1%/5%, не оценка эксплуатации. Вердикты M1–M4 делает отчёт, программа их не подгоняет.

Для скорости оценки membership кешируется по (typed layout, exact text). Warm latency использует отдельные live пути без этого кеша и сверяет их действия с оценщиком. Не более 2000 равномерно взятых natural случаев, прогрев, 32 повтора; percentiles — по средним повторов одного случая. Отдельно lookup без нормализации. Это не системная задержка клавиатуры.

## Пробы ресурсов

Режим разбирается раньше оценки и не читает corpus/train/model. Full либо control пара удерживается до выхода:

```sh
python3 tools/measure_probe_resources.py \
    build/tools/morphology_benchmark \
    --probe hunspell --morph-manifest data/morphology-003-sources.json \
    --iterations 2000 --json > build/experiment-003-full-probe.json
```

Для control заменить `hunspell` на `control`. Для fresh-process загрузки `--iterations 0`; в отчёте семь запусков каждого режима. `loadNs` — только два Hunspell_create; полное внешнее время включает проверки, загрузчик и наблюдатель. Warm probe: 64 фиксированные синтетические строки, два прямых spell на строку за итерацию; `nsPerSpellCall` не равен decision latency на natural.

Наблюдатель запускает argv напрямую без shell, читает /proc фактического PID ребёнка, 2ms sampling VmRSS/VmHWM. Late-loop median — последние до 100 samples, peak — максимум наблюдавшегося VmHWM. RSS включает shared pages, не является добавочной памятью системы. OS cache не очищается. Модель+морфология вместе отдельным memory probe не измерялись.

Публикуемый прогон выполнен через `bwrap --unshare-all --clearenv`: `/usr` и проект read-only, отдельные proc/dev/tmp, HOME=/tmp, без сети и home/соседних проектов. bwrap не запускается стендом автоматически. Воспроизведение без этой оболочки сохраняет алгоритм, но не эту границу изоляции.

## Публикация

Только код, паспорта, полные notices и агрегаты без токенов, предложений, ID, абсолютных пользовательских путей и weights. Исходники, control и производные данные остаются в игнорируемом build/. Отчёты 001/002 не переписываются. Системный ввод и история пользователя не используются.
