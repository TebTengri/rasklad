# Методика 005: контекстные признаки и совпадение намерений

[Протокол](../data/experiment-005-protocol.json) и [предрегистрация](EXPERIMENT-005-PREREG.md) фиксируют правила до новых context outcomes. [Паспорт](../data/evaluation-005-sources.json), [проверка источников](../data/notices/CONTEXT-005-REVIEW.md). Синтетический механизм не является новым естественным holdout.

## Подготовка и разделение ответов

Словари, .aff, Hunspell, scorer/пороги и артефакты002 прежние, по pinned хешам. Новый внешний corpus не скачивается. Для подготовки нужен уже собранный domain_decisions и прежняя установленная Hunspell; сам новый context_decisions от Hunspell не зависит. Скрипт `prepare_context_evaluation.py` проверяет source/notices/library hashes, exact PFX/SFX ablation, выделяет stem intersection2..32, делит ключи по protocol hash и выбирает контролы по full membership. Контролы **full-unambiguous**: native full accepted, other full rejected; это не обещание отсутствия stem/control ambiguity. Никакой фильтрации по успеху новых политик.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/prepare_context_evaluation.py
```

Только с локальным паспортом features:null и новыми выходными файлами. Для replay в отдельной рабочей копии/ветке восстановить по паспорту approved словари/artifacts, сохранить исходный опубликованный features ref для сравнения и обнулить рабочие features/labels refs. Скрипт не перезаписывает готовые features.tsv/labels.jsonl.

Сначала нейтральные token probes подаются прежнему `domain_decisions`: отдельный временный adapter passport в data/ и read-only overlay нового probe TSV на ожидаемый004 path только в bwrap namespace. Исходный004 TSV на хосте не меняется. Adapter удаляется после вызова. Baseline child без сети, home/соседей; он читает только pinned project/system inputs. Его model actions независимо сверяются с Python int8 scorer и XKB mapping, full actions — с direct Hunspell membership;новая модель не обучается. Подготовка сама вызывает только этот фиксированный CLI в изоляции, не shell с текстом. Установленная библиотека доверенная и проверенная; hash не является sandbox от вредоносного loader.

Затем признаки12колонок (layout,target,4past tokens,field,obs/alt/shared flags,2base actions) пишутся отдельно от labels (ordinal,split,keygroup,scenario,gold,group,collisionID,length). Ни gold, ни split/scenario IDs не входят в input политик. Префиксы — собственные короткие наборы из протокола; слова могут быть редкими/аббревиатурами словарей, грамматическая или семантическая естественность не гарантируется. Разбиение target keygroups и anchor vocabulary между005 dev/holdout disjoint, но targets не гарантированно новые для старого model train.

## C++ и верификация

Claude пишет новый pure context helper и bounded QtCore CLI; Codex готовит data/labels, независимо проверяет routing, aggregates и отчёт. Старое src/ и старые методы не меняются. Новый helper/CLI не требуют Hunspell во время работы: используют offline-проверенные сигналы. Это dependency boundary эксперимента, не готовый runtime extractor.

```sh
bin/build
bin/test -silent
build/tools/context_decisions \
    --protocol data/experiment-005-protocol.json \
    --manifest data/evaluation-005-sources.json \
    > build/experiment-005-actions.tsv
PYTHONDONTWRITEBYTECODE=1 python3 tools/evaluate_context_experiment.py \
    --decisions build/experiment-005-actions.tsv --output build/experiment-005.json
PYTHONDONTWRITEBYTECODE=1 python3 tools/audit_context_aggregates.py \
    --report build/experiment-005.json --decisions build/experiment-005-actions.tsv
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/context_reviewer -p 'test_*.py'
```

CLI проверяет точный hash protocol005, approved-local-synthetic-evaluation passport/scope/release=false, notices и feature hash/bytes/rows. Bounded8MiB/100000rows, strict UTF-8/LF/noCR, strings<=32UTF16; лимиты до декодирования/разбиения. Labels path в паспорте описательный и **не открывается CLI**. Сигналы full baseline проверяются на consistency с target guard и obs/alt flags; model action для short/unsupported должен Abstain, остальное основано на отдельно сверенном старом baseline. На input/integrity failure stdout пуст и собственные diagnostics без текста.

Драйвер выдаёт только ordinal, vote U/R/-,6 action codes K/C/A/S. Перечень методов и causal-script rule в протоколе. Только left3/left4 влияют на голос; left1/left2 не используются этим простым контролем, будущих слов нет. Protected/unknown — oracle field metadata. Suggest не пишет текст и не считается auto recovery. Per-case outputs только build/, не public.

Независимая Python policy reference сверяет каждую output строку и vote до агрегации. Отдельный list/filter recount проверяет первичные counts и все budget verdicts. Токен-only инвариант проверяется при всех контекстах одного target/layout. Для collision пары проверяется exact equality observable bytes и всех policy actions, pair recovery+Keep preservation<=1, jointlySatisfied0. Это следствие конструкции, не статистическая закономерность на людях.

## Метрики и границы

Development/holdout, typed layout/pooled, short2..3/long4+; occurrences и exact feature+gold category types. Проза Keep/protected Keep/unknown Keep/ambiguous Correct/full-unambiguous control Correct отдельно. AmbiguousKeep — пересекающийся subset Keep категорий: его нельзя складывать с ними в общий false total. Бюджет в каждой основной scope: zero false для каждого Keep-среза и >=50% auto recovery для ambiguous/control Correct. Short slice без controlCorrect имеет insufficient, а не доказательство успеха/неуспеха всех требований. Все components показаны даже при insufficient. Proposals/correct proposals/false proposals отдельно.

Target minimum новых context policies2, старых token policies4. Поэтому short gain смешивает расширение guard и новый context feature; long slice показывается отдельно. На full-both relaxed token-full всё равно сохранил бы слово: простое снятие short guard не делает такое намерение различимым. Шаблонный script vote не является семантической моделью и его engineered recall не переносится на natural text. Новые реальные корпуса/методы потребуют отдельного prereg, rights review и независимого holdout.

Публикуются только code/protocol/passports/notices/aggregates, не пары слов, prefixes, features/labels/actions или weights. Нет системного ввода, IPC текста, истории, сети в обработчике, автозапуска и установки.
