# Профиль детектора: откуда он берётся

Автоисправлению нужны словари Hunspell и обученная модель — «профиль детектора».
В Git его нет: это производные данные сторонних словарей, распространение которых
не одобрено (см. [THIRD_PARTY.md](../../THIRD_PARTY.md) и [политику данных](../../docs/DATA_POLICY.md)).

## Сборка пакета из готового профиля

```sh
tools/build_arch_package.sh "/путь/к/профилю"
```

В каталоге профиля должны быть 12 файлов по путям `build/...` из таблицы
ниже. В [profile-files.txt](profile-files.txt) есть ещё строки `data/...`. Эти файлы
скрипт сборки берёт из клона сам, класть их в каталог профиля не нужно. Каталог
профиля не должен лежать внутри `build/arch-package/`: скрипт очищает его перед сборкой.

## Файлы и источники

| Файл | Откуда |
| --- | --- |
| `build/experiment-data/en.dic`, `ru.dic` | LibreOffice dictionaries, коммит `c0865a85eb67`; адреса и хеши — в [data/sources.json](../../data/sources.json) |
| `build/experiment-003-data/en.aff`, `ru.aff` | тот же коммит; адреса и хеши — в [data/morphology-003-sources.json](../../data/morphology-003-sources.json) |
| `build/experiment-003-data/control/en.aff`, `ru.aff` | получаются из `.aff` (шаг 2 ниже) |
| `build/experiment-002-artifacts/detector-model.rskn`, `exact-full-lexicons.rskp`, `artifact-metadata.json` | создаются стендом опыта 002 (шаг 3) |
| `build/experiment-002-artifacts/en_US.NOTICE.txt`, `ru_RU.NOTICE.txt` | байт в байт копии `data/notices/en_US.README.txt` и `ru_RU.README.txt` |
| `build/experiment-002-artifacts/MODIFICATIONS.txt` | пометка о локальных изменениях (шаг 4) |

## Что проверяется

Аддон проверяет профиль при каждой загрузке (`src/profileverify.cpp`):
- словари, `.aff`, контрольные `.aff`, модель и словарь стеблей — по размеру и SHA-256;
- копии уведомлений — по SHA-256 оригиналов из `data/notices/`;
- `artifact-metadata.json` — по полям;
- `MODIFICATIONS.txt` — UTF-8, не пустой, содержит `release_approved=false`.

Если что-то не сходится, автоисправление выключается, а значок показывает
`EN!`/`RU!`. `check()` пакета сам профиль не загружает: он запускает тесты и
проверяет импорты аддона.

## Воспроизведение из чистого клона

Это ручные шаги с загрузкой из сети. Делайте их сами; агенты проекта не скачивают
данные без отдельного разрешения владельца. Шаги 1, 2 и 4 выполнимы всегда.
Шаг 3 сейчас выполним только при наличии старого снимка Tatoeba (см. ниже).

1. **Словари.** Скачайте `.dic` и `.aff` по адресам из двух паспортов выше и
   положите по путям из таблицы. Хеши должны совпасть с паспортами:
   ```sh
   sha256sum build/experiment-data/*.dic build/experiment-003-data/*.aff
   ```
   Адреса указывают на закреплённый коммит, поэтому файлы не меняются.
2. **Контрольные `.aff`** (строки PFX/SFX убраны):
   ```sh
   python3 tools/prepare_affix_control.py --input build/experiment-003-data/en.aff --output build/experiment-003-data/control/en.aff
   python3 tools/prepare_affix_control.py --input build/experiment-003-data/ru.aff --output build/experiment-003-data/control/ru.aff
   ```
3. **Модель и словарь стеблей.** Модель обучается только на словарях. Но стенд,
   который её пишет, требует ещё и проверочные выборки Tatoeba.

   Выборки готовятся из сжатых выгрузок Tatoeba (`*_sentences_CC0.tsv.bz2`, адреса —
   в [data/evaluation-002-sources.json](../../data/evaluation-002-sources.json)).
   Распаковывать их не нужно: скрипт сам читает `.bz2` и убирает столбец времени.
   Хеш результата должен совпасть с полем `sha256` паспорта. Адрес выгрузки не
   закреплён и со временем отдаёт другие байты, поэтому без снимка от 2026-11-07
   этот шаг не пройдёт. Со снимком:
   ```sh
   snapshot="/путь/к/снимку/tatoeba"   # каталог с двумя .tsv.bz2
   mkdir -p build/experiment-002-data
   python3 tools/prepare_cc0_evaluation.py --language eng --input "$snapshot/eng_sentences_CC0.tsv.bz2" --output build/experiment-002-data/en.tsv
   python3 tools/prepare_cc0_evaluation.py --language rus --input "$snapshot/rus_sentences_CC0.tsv.bz2" --output build/experiment-002-data/ru.tsv
   bin/build
   mkdir -p build/experiment-002-artifacts
   build/tools/detector_benchmark \
       --manifest data/sources.json \
       --en build/experiment-data/en.dic \
       --ru build/experiment-data/ru.dic \
       --eval-manifest data/evaluation-002-sources.json \
       --eval-en build/experiment-002-data/en.tsv \
       --eval-ru build/experiment-002-data/ru.tsv \
       --protocol data/experiment-002-protocol.json \
       --artifact-dir build/experiment-002-artifacts \
       --json > build/experiment-002-report.json
   ```
   Подробности — в [методике опыта 002](../../docs/research/EXPERIMENT-002-METHOD.md).
4. **Уведомления и пометка** (та же строка, что в профиле владельца):
   ```sh
   mkdir -p build/experiment-002-artifacts
   cp data/notices/en_US.README.txt build/experiment-002-artifacts/en_US.NOTICE.txt
   cp data/notices/ru_RU.README.txt build/experiment-002-artifacts/ru_RU.NOTICE.txt
   printf '%s\n' 'Modified numerical artifacts derived locally from reviewed LibreOffice dictionaries. Local experiment only, release_approved=false. See data/sources.json and experiment002 protocol.' \
       > build/experiment-002-artifacts/MODIFICATIONS.txt
   ```

После четырёх шагов клон сам служит каталогом профиля:
`tools/build_arch_package.sh .`. Совпадут ли заново созданные модель и словарь
стеблей с закреплёнными хешами, не проверялось. Если не совпадут, автоисправление
не включится (см. ниже).

## Что не проверено

- Полное воспроизведение из чистого клона не выполнялось. Профиль владельца
  подготовлен в ходе опытов 002 и 003 и с тех пор закреплён по хешам.
- Совпадение заново созданных модели и словаря стеблей с закреплёнными хешами не
  проверено. Если они не совпадут, автоисправление будет выключено, пока закрепления
  не обновят отдельным проверенным изменением.
- Автоматизировать шаги одним скриптом можно отдельной задачей. Для этого нужно
  решение о загрузке и хранении снимка Tatoeba.
