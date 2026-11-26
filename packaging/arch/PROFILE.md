# Профиль детектора: откуда он берётся

Автоисправлению нужны словари Hunspell и обученная модель — «профиль детектора».
В Git его нет: это производные данные сторонних словарей. Распространение одобрено
владельцем 2026-11-26 после [ревью](../../data/notices/DISTRIBUTION-REVIEW-001.md)
(см. также [THIRD_PARTY.md](../../THIRD_PARTY.md) и [политику данных](../../docs/DATA_POLICY.md)).

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
| `build/experiment-003-data/en.aff`, `ru.aff` | тот же коммит; адреса и хеши — в [data/morphology-003-sources.json](../../data/morphology-003-sources.json), закрепления продукта — в [data/product-profile-sources.json](../../data/product-profile-sources.json) |
| `build/experiment-003-data/control/en.aff`, `ru.aff` | получаются из `.aff` (шаг 2), затем получают пометку об изменении (шаг 5) |
| `build/experiment-002-artifacts/detector-model.rskn`, `exact-full-lexicons.rskp` | создаются стендом опыта 002 (шаг 3), затем получают уведомления внутри файла (шаг 5) |
| `build/experiment-002-artifacts/artifact-metadata.json` | создаётся стендом опыта 002 (шаг 3); на шаге 5 в нём обновляются размеры, хеши и формат модели и списка слов |
| `build/experiment-002-artifacts/en_US.NOTICE.txt`, `ru_RU.NOTICE.txt` | байт в байт копии `data/notices/en_US.README.txt` и `ru_RU.README.txt` |
| `build/experiment-002-artifacts/MODIFICATIONS.txt` | пометка о локальных изменениях (шаг 4) |

## Что проверяется

Аддон проверяет профиль при каждой загрузке (`src/profileverify.cpp`):
- словари, `.aff`, контрольные `.aff`, модель и словарь стеблей — по размеру и SHA-256;
- контрольные `.aff` — что это исходный `.aff` без строк PFX/SFX с заголовком-пометкой
  Rasklad в начале;
- модель и словарь стеблей — что в начале файла есть уведомления (контейнер RSKX);
- копии уведомлений — по SHA-256 оригиналов из `data/notices/`;
- `artifact-metadata.json` — по полям;
- `MODIFICATIONS.txt` и поле `scope` метаданных — дословно тексты одобрения из
  `src/profileverify.h` (`kReleaseModifications`, `kReleaseScope`);
- продуктовый паспорт `data/product-profile-sources.json` — `release_approved: true`
  и `distribution_review` с путём к ревью; учебный паспорт `data/sources.json` —
  неизменная запись опыта (`release_approved: false`), его байты закреплены.

Обновление словарей (`tools/update_dictionaries.py`) одобрение не наследует: оно
пишет `release_approved: false`, и аддон с пакетом не примут новый профиль, пока
владелец не одобрит новое ревью распространения.

Если что-то не сходится, автоисправление выключается, а значок показывает
`EN!`/`RU!`.

Те же проверки с теми же закреплениями выполняются при сборке пакета: `check()` в
PKGBUILD запускает `tests/profile_gate` на собранном профиле, а
`tools/build_arch_package.sh` вызывает `makepkg --check`, даже если проверки
выключены в `makepkg.conf`. Старый, непомеченный или изменённый профиль в пакет не
попадёт. Кто вызывает `makepkg` сам с `--nocheck`, эту проверку пропускает; тогда
профиль проверит только аддон при загрузке. Кроме того, `check()` запускает тесты и
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

5. **Пометки и уведомления внутри файлов** (SYS-256). Инструмент сверяет исходный
   профиль с закреплениями, вписывает заголовок в контрольные `.aff` и уведомления
   в модель и список слов, записывает одобрение распространения в
   `MODIFICATIONS.txt` и метаданные (исходный профиль остаётся «локальным опытом»)
   и пишет новый каталог профиля:
   ```sh
   python3 tools/make_release_profile.py --from . --out build/release-profile
   ```

После пяти шагов каталог профиля — `build/release-profile`:
`tools/build_arch_package.sh build/release-profile`. Совпадут ли заново созданные модель и словарь
стеблей с закреплёнными хешами, не проверялось. Если не совпадут, шаг 5
(`make_release_profile.py`) остановится: он принимает только проверенный исходный
профиль.

## Что не проверено

- Полное воспроизведение из чистого клона не выполнялось. Профиль владельца
  подготовлен в ходе опытов 002 и 003 и с тех пор закреплён по хешам.
- Совпадение заново созданных модели и словаря стеблей с закреплёнными хешами не
  проверено. Если они не совпадут, шаг 5 не создаст профиль, а пакет не соберётся,
  пока закрепления не обновят отдельным проверенным изменением.
- Полное обновление словарей одной командой не выполнялось: `tools/update_dictionaries.py`
  проверен без сети (тесты `tests/packaging/test_update_dictionaries.py`), шаги
  `fetch` и `build` с настоящими данными ещё не запускались.

## Обновление словарей

Новый коммит LibreOffice/dictionaries закрепляется инструментом
`tools/update_dictionaries.py` (для сопровождающих, SYS-272). Шаги:

```sh
c=<40-значный коммит LibreOffice/dictionaries>
python3 tools/update_dictionaries.py plan    --commit $c   # что скачается; без сети
python3 tools/update_dictionaries.py fetch   --commit $c --network   # только с разрешения владельца
python3 tools/update_dictionaries.py build   --commit $c   # контрольные .aff, выборки, модель, профиль
python3 tools/update_dictionaries.py compare --commit $c   # путь решения продукта: прежний профиль против нового
python3 tools/update_dictionaries.py pin     --commit $c   # записывает ровно то, что проверил compare
```

Проверочные выборки — свежая выгрузка Tatoeba (CC0), её хеши закрепляются заново.
`build` останавливается, если README словаря (его лицензия) изменился: новый текст
нужно прочесть и принять флагом `--accept-notices`. `compare` вписывает новые
закрепления в копию HEAD, собирает из неё и из HEAD `tools/product_eval` (загрузчик
профиля и маршрутизатор аддона, закрепления вкомпилированы) и прогоняет прежний
профиль (`build/release-profile`, иначе `--baseline-profile`) и новый на одной и той же
свежей выгрузке. Отказ, если аддон не принял бы новый профиль, выросла доля ложных
исправлений (сюда входят и неоднозначные слова), упала доля исправленных опечаток или
появилось исправление не в то слово. `pin` пишет только после успешного `compare` на
том же HEAD и при неизменных рабочих файлах; все замены сначала применяются к чистой
копии, затем файлы записываются. После
`pin`: `bin/build`, тесты, коммит изменений (сборщик пакета берёт исходники и манифесты
из HEAD и отказывает при незакоммиченных правках), пакет из
`build/dict-update/<коммит>/release`, стенд, затем обычное независимое ревью. Опыты 001–006 остаются привязаны к прежним словарям:
их инструменты новый `data/sources.json` не примут, это история исследований. Новый
обзор распространения (как `data/notices/DISTRIBUTION-REVIEW-001.md`) записывается
отдельно.
