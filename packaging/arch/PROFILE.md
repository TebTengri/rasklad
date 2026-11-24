# Профиль детектора: откуда он берётся

Автоисправлению нужны словари Hunspell и обученная модель — «профиль детектора».
В Git его нет: это производные данные сторонних словарей, распространение которых
не одобрено (см. [THIRD_PARTY.md](../../THIRD_PARTY.md) и [политику данных](../../docs/DATA_POLICY.md)).
Поэтому `tools/build_arch_package.sh` принимает каталог, где профиль уже подготовлен:

```sh
tools/build_arch_package.sh <каталог>
```

В `<каталог>` должны лежать 12 файлов по путям `build/...`. Список задан в
[profile-files.txt](profile-files.txt). Каждый файл закреплён по размеру и SHA-256 в
`src/profileverify.cpp`. Аддон проверяет закрепления при загрузке, `check()` пакета —
при сборке. Если хоть один файл не совпадает, автоисправление выключается, и значок
показывает `EN!`/`RU!`.

## Файлы и источники

| Файл | Откуда |
| --- | --- |
| `build/experiment-data/en.dic`, `ru.dic` | LibreOffice dictionaries, коммит `c0865a85eb67`; адреса и хеши — в [data/sources.json](../../data/sources.json) |
| `build/experiment-003-data/en.aff`, `ru.aff` | тот же коммит; адреса и хеши — в [data/morphology-003-sources.json](../../data/morphology-003-sources.json) |
| `build/experiment-003-data/control/en.aff`, `ru.aff` | получаются из `.aff` командой `tools/prepare_affix_control.py` (строки PFX/SFX убраны) |
| `build/experiment-002-artifacts/detector-model.rskn`, `exact-full-lexicons.rskp`, `artifact-metadata.json` | создаются стендом опыта 002 (`detector_benchmark --artifact-dir`) |
| `build/experiment-002-artifacts/en_US.NOTICE.txt`, `ru_RU.NOTICE.txt` | байт в байт копии `data/notices/en_US.README.txt` и `ru_RU.README.txt` |
| `build/experiment-002-artifacts/MODIFICATIONS.txt` | текстовая пометка о локальных изменениях; должна содержать `release_approved=false` |

Файлы `data/...` из `profile-files.txt` берутся из коммита сами.

## Как воспроизвести из чистого клона

Это явные ручные шаги с загрузкой из сети. Делайте их сами; агенты проекта не
скачивают данные без отдельного разрешения владельца.

1. **Словари.** Скачайте `.dic` и `.aff` по адресам из двух паспортов выше и
   положите по путям из таблицы. Проверьте:
   ```sh
   sha256sum build/experiment-data/*.dic build/experiment-003-data/*.aff
   ```
   Хеши должны совпасть с паспортами. Адреса указывают на закреплённый коммит,
   поэтому файлы не меняются.
2. **Контрольные `.aff`:**
   ```sh
   python3 tools/prepare_affix_control.py --input build/experiment-003-data/en.aff --output build/experiment-003-data/control/en.aff
   python3 tools/prepare_affix_control.py --input build/experiment-003-data/ru.aff --output build/experiment-003-data/control/ru.aff
   ```
3. **Модель и словарь стеблей.** Соберите стенд (`bin/build`) и запустите оценку
   опыта 002 с `--artifact-dir build/experiment-002-artifacts`; команда — в
   [EXPERIMENT-002-METHOD](../../docs/research/EXPERIMENT-002-METHOD.md).
   Модель обучается только на словарях. Однако стенд требует ещё и проверочные
   выборки Tatoeba с хешами из [data/evaluation-002-sources.json](../../data/evaluation-002-sources.json).
   Адрес выгрузки Tatoeba не закреплён и со временем отдаёт другие байты. Без
   снимка от 2026-11-07 этот шаг сейчас не повторить.
4. **Уведомления и пометка.** Скопируйте два `README.txt` из `data/notices/` под
   именами из таблицы. Запишите `MODIFICATIONS.txt` с пометкой `release_approved=false`
   и ссылкой на паспорта.
5. Соберите пакет: `tools/build_arch_package.sh .` из корня клона.

## Что не проверено

- Полное воспроизведение из чистого клона не выполнялось. Профиль владельца
  подготовлен в ходе опытов 002 и 003 и с тех пор закреплён по хешам.
- Совпадение модели и словаря стеблей, созданных заново, с закреплёнными хешами
  не проверено. Детерминизм стенда описан в методике, но на новой машине не
  подтверждён.
- Автоматизировать шаги 1–4 одним скриптом можно отдельной задачей. Для этого
  нужно решение о загрузке и сохранении снимка Tatoeba.
