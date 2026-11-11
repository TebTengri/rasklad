# Методика опыта 004 (SYS-236)

Offline-проверка зафиксированных политик на другом жанре. [Протокол](../data/experiment-004-protocol.json), [предрегистрация](EXPERIMENT-004-PREREG.md), [паспорт](../data/evaluation-004-sources.json), [проверка условий](../data/notices/TECHNICAL-004-REVIEW.md). Системный ввод не используется.

## Подготовка

Источники — только 40 pinned Markdown blobs двух публичных Rust Book репозиториев. Получение исходников отдельно от сборки/оценки; в паспорте точные URLs, commits, SHA-256, Git blob SHA-1 и размеры. Candidate paths берутся из отдельно сохранённого Git tree metadata, выбираются первые 20 на язык по SHA-256(seed + NUL + path). Префикс RU уточнён по дереву путей до получения chapter-текстов. Никакой контентной фильтрации по качеству детектора. Full notices и MODIFICATIONS сохраняются рядом с локальными source/derived files. Сами главы не входят в Git и не разрешены к выпуску с приложением.

На новых локальных копиях с паспортом `cases:null`:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/prepare_technical_evaluation.py \
    --manifest /absolute/project/path/data/evaluation-004-sources.json
```

Скрипт принимает только паспорт этого проекта, проверяет точный hash протокола, tree selection, все source hashes/Git blobs; создаёт только новый `build/experiment-004-data/cases.tsv`, обновляет в паспорте только cases ref. В уже подготовленном публичном паспорте ref заполнен: для replay восстановить исходные байты по хешам и создать локальную рабочую копию паспорта с `cases:null` на отдельной ветке, затем проверить полученный ref с опубликованным. Подготовка повторно не перезаписывает существующие cases.

Bounded source <=1MiB, cases <=64MiB/500000 rows. Шесть TSV полей: document index0..39, document language en/ru, role valid/protected/wrong, typed layout us/ru, text, expected. Строки LF, strict UTF-8; text/expected <=4096UTF16. Expected равен text для valid/protected; wrong обратным преобразованием обязан восстановить expected, native-letter-only длиной1..32. Сам TSV содержит слова и остаётся только в build/.

## Ограниченный разбор Markdown

Скрипт распознаёт fenced code с <=3 начальными ASCII пробелами и >=3 одинаковыми backticks/tildes; closing marker того же типа не короче opening. Строки маркеров исключены. Inline code — совпадающие по длине backtick runs на одной строке. Unmatched backticks остаются прозой. Всё остальное Markdown остаётся буквально; ссылки, HTML, mdBook includes, шаблоны, вложенные файлы и программы не выполняются и не загружаются. Это не CommonMark renderer. Поэтому измеряется именно этот source-level контекст, а не вся семантика отрисованной книги.

Токены — Unicode whitespace split, strip фиксированного набора внешней пунктуации протокола; регистр и внутренняя пунктуация сохраняются. Oracle typed layout: есть RU33 letter → ru, иначе us. Поэтому английские термины в русском тексте не считаются неправильной раскладкой. Mixed/unsupported остаются valid-intent. Wrong-копии делаются только из prose native-letter-only; code имеет только Keep-intent. Не оцениваются реальная сегментация по клавиатурным событиям, текущая раскладка и безопасное поле.

## Драйвер решений

`bin/build` собирает optional Hunspell target. Функциональный C++ пишет Claude, независимую подготовку/агрегацию/сверку — Codex. Прежние ядро, scorer, пороги и стенды 001–003 не меняются.

```sh
build/tools/domain_decisions \
    --protocol data/experiment-004-protocol.json \
    --cases-manifest data/evaluation-004-sources.json \
    --morph-manifest data/morphology-003-sources.json \
    > build/experiment-004-actions.tsv
```

Модель и stem index загружаются только из pinned артефактов опыта002 с исходными notices/metadata. Пороги прежние: gap2.5, minAlternative−13, maxObserved−9. Полный protocol004 закреплён точным hash; корпус новый, обучения нет. Morphology inputs/library/control проверяются по паспорту003. Library hashing — integrity доверенных системных зависимостей, не защита от hostile dynamic loader. Файлы источников под build, symlink escape запрещён; notices проверяются отдельно. Данные сначала валидируются целиком; на отказе нет частичного output и слов в собственных diagnostics.

Выход — developer-only TSV: ordinal, eligible0/1, ambiguous0/1, roundTrip0/1, девять action codes K/C/A. Он не публикуется: source words отсутствуют, но это всё равно per-case данные исследования. Eligible = supported && keyCount>=4. Ambiguity = union stem/control/full both, применяется только к wrong recovery scoring. Valid/protected всегда Keep-intent.

## Независимая сверка и агрегаты

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/audit_domain_decisions.py \
    --decisions build/experiment-004-actions.tsv
PYTHONDONTWRITEBYTECODE=1 python3 tools/summarize_technical_evaluation.py \
    --manifest data/evaluation-004-sources.json \
    --decisions build/experiment-004-actions.tsv \
    --output build/experiment-004.json
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/technical -p 'test_*.py'
```

Audit получает ANSI base/Shift mapping из установленного XKB us/ru через libxkbcommon, отдельно вычисляет canonical keys, ngram scores, routing и code gate, сверяет каждую строку и обратимость. Hunspell backend тот же через ctypes: это независимая проверка драйвера, не независимый лингвистический движок. Ни audit, ни report generator не подбирают параметры.

Агрегаты: occurrences/exact-case types для языка документа, pooled и каждого из40 документов без имён/текстов; code/prose отдельно, false changes на подходящих словах, wrong recovery/ambiguity, парные добавленные и потерянные действия, document macro rates и все screen outcomes. Тип = (document language, role, typed layout, exact text); global types не складываются по документам, per-document types считаются локально. Срез языка относится к источнику, не гарантированному языку отдельного термина. Overlap с Tatoeba002/003 только диагностический, никогда не меняет набор или решения. Старый snapshot отдельно проверяется по старому паспорту.

Исследовательский budget применён ко всем9 вариантам без выбора лучшего порога; отсутствие ошибок не доказывает безопасность. Нет IID confidence и оценки deployment precision. Для изолированного прогона используется внешняя bwrap оболочка с `--unshare-all`, read-only `/usr` и проектом, отдельными proc/dev/tmp и HOME=/tmp; home/соседние проекты не монтируются. CLI сам namespace не создаёт и сеть не использует.
