# Методика006: естественные префиксы и выбор архитектуры

[Протокол](../data/experiment-006-protocol.json), [предрегистрация](EXPERIMENT-006-PREREG.md), [паспорт](../data/evaluation-006-sources.json), [review](../data/notices/NATURAL-CONTEXT-006-REVIEW.md). Итог — [ADR-001](ADR-001-DETECTOR-ROUTING.md), а не включение глобального ввода.

## Источники и подготовка

Тот же pinned candidate pool Rust Book004, hash-ranking путей неизменен. EN/RU ranks20..29 development,30..39 holdout; прежние0..19 исключены. Новые40 Markdown blobs получены после отдельного local-only review006; точные URLs/Git blobs/SHA-256/bytes в паспорте. Первичные полные notices сохранены и рядом с локальными файлами. Ни includes, ни код/HTML/ссылки не выполняются.

```sh
bin/build
PYTHONDONTWRITEBYTECODE=1 python3 tools/prepare_natural_context.py
```

Нужны прежние pinned .dic/.aff/control/model/stem artifacts и уже установленная Hunspell. Повторная подготовка отказывается от готовых shards/staging; replay — в отдельной рабочей копии с исходными source bytes, features:[]/labels:null в локальном паспорте и новым build/experiment-006-data, затем сравнение опубликованных refs. Никаких автоматических скачиваний из inference/build/test.

Source-level parser использует прежние FENCE/inline_spans/TRIM, оставляет внутреннюю пунктуацию/case. Последние4prose tokens сохраняются causal; paragraph blank line и protected transition очищают префикс. Неподдерживаемый/длинный prefix — barrier, не пропуск. Английские термины в RUprose получают исходный Keep-intent, layout — script oracle, не context vote. Gold не выводится из тестируемой политики.

Kinds: original valid/protected Keep; single wrong native-letter-only target; burst — тот же target и prefix native tokens его языка перенесены в противоположную раскладку; unknown — отдельный no-action stress на wrong-looking single target. Burst/unknown — искусственные стресс-сценарии, не пользовательская частота. Targets>32UTF16 заменяются U+FFFD в features, но исходный exact-word hash остаётся в labels/wordtype totals. Они не влияют на eligible recovery, не исчезают из общих counts. Prefix strings<=32; no control characters. Exact ASCII/RU alphabet не принимает Unicode aliases (например Kelvin sign как ASCII K); после исправления этого check сверено, что ни один generated target/prefix текущего корпуса не менялся.

Подготовка читает bounded pinned sources, проверяет library/version и exact control derivation. Уникальные16227token probes подаются старому domain_decisions в networkless bwrap с read-only overlay на ожидаемый004cases path. Host004 TSV не меняется; временный adapter удаляется. Результаты model/full независимо сверены Python int8 scorer/XKB/direct Hunspell. Затем membership/base-action signals записаны в12field005-schema features, labels отдельно. Никакой новой модели или настройки порогов.

## Sharded C++ driver

```sh
build/tools/router_decisions \
    --protocol data/experiment-006-protocol.json \
    --manifest data/evaluation-006-sources.json --shard 0 \
    > build/experiment-006-data/actions-00.tsv
```

Повторить для1/2 (manifest содержит3shards). Только выбранный shard<=8MiB/100000rows читается; manifest schema/scope/release/protocol/notice hashes и safe canonical paths проверяются. Raw limits/strictUTF8/12columns/baseline consistency переиспользуются неизменённым parser005. Прочие shard refs проверяются синтаксически, их содержимое не открывается. Labels/raw documents/model/dictionaries новым CLI не читаются. Output local ordinal, voteU/R/-,8action codes; only aftersuccess, без слов/IDs/paths. Сети/процессов/дисковой записи в CLI нет. Golden labels не входят в policy inputs.

## Независимый расчёт и метрики

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/evaluate_natural_router.py --output build/experiment-006.json
PYTHONDONTWRITEBYTECODE=1 python3 tools/audit_natural_router_metrics.py \
    --report build/experiment-006.json
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s tests/natural_context_reviewer -p 'test_*.py'
```

Reference отдельно вычисляет strict Unicode script vote и все8frozen policies, сравнивает221090rows перед агрегацией. Source split/lang/kind/gold остаются только в evaluator. Units: occurrences; model-visible inputTypes(inside split/sourceLang/kind/gold); wordTypes(sourceLang/kind/layout/originalExactWordHash). У wordTypes событие считается при **ANY occurrence** этого слова, поэтому rare bad context не теряется из-за хорошего первого. Action-event counts wordTypes могут пересекаться (слово в разных контекстах иKeep иAbstain); их нельзя суммировать как взаимно исключающие частоты. Independent audit использует отдельные event sets вместо bit-mask агрегатора и проверяет primary counts/screens.

Screen auto: original eligible valid <=1false/10000поoccurrences иwordTypes,0protected/unknown/ambiguousValidauto,>=50%ordinary eligible single иburst recovery поoccurrences/wordtypes; EN/RU/pooled. Proposals имеют отдельные budgets/coverage, не становятся auto recovery. AmbiguousCorrect можетabstain. Short/unsupported slices с пустыми denominator даютinsufficient. Подмножества borrowedValid/ambiguousValid не складываются сvalid. Legacy scriptContext>=2 противнового>=4 показывается явно.

Architecture selection использует только holdout и заранее заданнуюJSONтаблицу. Recall penalty/mandatory-context loss проверяются пообоим primaryunits. Proactive-proposal pass — исследовательское разрешение на дальнейшее opt-in испытание, не включение UI/desktop. Actual MVP boundary иdefault вADR.

## Профиль ресурсов

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/profile_verified_provider.py \
    > build/experiment-006-provider-resource-repeat.json
```

Это developer-only probe одного нейтрального токена через старый verified provider driver. Parent создаёт отдельный временный adapter/overlay в bwrap; свежий supervisor запускает ровноодинactual worker. KernelRUSAGE_CHILDREN относится только к нему, не кsupervisor/bwrap. Peak включает full+control pairs, full stem index, model, source verification/loader/QtCore; не включает большойcorpus/QtQuickGUI. OS cache не очищен. Не выдавать его заsteady-state иливсёприложение; данные одного измерения опубликованы отдельно. Script не принимает user text/команды, не являетсяruntime app.

Все generated source/staging/features/labels/actions вignored build/, release_approved=false. Публикуются code/passports/notices/aggregates/ADR. Нет клавиатурного ввода/истории/установки/служб/автозапуска. Основные прогоны без сети/home/соседей, project+/usr read-only. Library hash — integrity доверенной system dependency, не hostile-loader sandbox.
