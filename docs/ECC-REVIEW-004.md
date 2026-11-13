# SYS-242 — ревью с новым ECC

2026-11-13. Статически проверен публичный snapshot `01a7d1b`.

- Источник review-профилей: установленный [ECC 2.2.3](https://github.com/affaan-m/ECC).
  В отдельный session-only набор скопированы code-reviewer, security-reviewer,
  architect и verification-loop с сохранением MIT notice самого ECC. В продукт
  эти сторонние prompts не включены. Глобальные настройки не менялись.
- Фактически вызваны все три Agent-профиля и Skill verification-loop. Coordinator
  Claude Opus 5.5, в review также участвовал Claude Sonnet 5.5. Проверено по
  stream-журналу, не по обещанию модели.
- Только Read/Grep/Glob публичного снимка; hooks, shell, edits, MCP, автопамять,
  синхронизация account plugins/skills и persistence выключены для этого запуска.
  Chrome DevTools MCP не запускался: installer metadata сообщает об отсутствующих
  зависимостях. Реальные данные и соседние проекты не открывались.
- Claude выполнил **статическое ревью**, тесты не запускал. Результаты ниже после
  исправлений отдельно воспроизвёл Codex. Это не сертификация безопасности.

## Разбор и выполненные исправления

| Замечание | Решение |
| --- | --- |
| Браузерный loader допускал произвольные конечные twoScore thresholds | Исправлено: допуск только frozen -13/-9/2.5 и enabled=true, как native profile. Lenient-but-finite значения отклоняются |
| 512-state test использовал тот же table index как oracle | Добавлен независимый native oracle: явные full/control/stem флаги, model/profile и результат route; JS не вычисляет expected через таблицу. Добавлены семантические anchors |
| Нет native gold для converter | Добавлены 27 US/RU/invalid случаев: ASCII, регистр, combining marks, keycap, supplementary и orphan UTF16 |
| Повторные чтения accessor-конфигурации evidence | Значения и функции снимаются по одному разу в snapshot, добавлена регрессия |
| Второй вызов converter мог вернуть другую кириллицу той же длины | Router проверяет canonical keys кандидата против входа. Чистота/case-preservation поставляемого converter остаются контрактом доверенного кода |
| MIT в новых заголовках противоречило README/THIRD_PARTY | Метки Codex и ложная фраза «как проект» исправлены на pending. LICENSE не выбран за владельца; правовой смысл ранее опубликованной истории этим не решается |

Наличие credentials вне associated form — ограничение owner-registration,
не найденный обход неизвестного поля: произвольный/secret textarea нельзя
регистрировать как prose. Контракт уточнён. Поиск password по всей странице
не добавлен: он отключил бы самостоятельный composer из-за чужой login-формы.
Показанные пароли без hints требуют авторитетной роли владельца, не эвристики.

## Проверено после исправлений

41 Node tests и 31 Chromium assertions прошли. В Node и закрытом Chromium:
1 176 scores, 2 464 model decisions, 512 явных native router states и 27 native
converter cases совпали. Chromium 155, networkless bwrap, родной browser sandbox,
CDP только дочерние pipes, никакого рабочего профиля. Настоящие .aff/.dic/weights
не читались; числовые данные и словарные callbacks искусственные.

## Что не принимаем за доказанный результат

- «Предразвёрнутый точный wordlist вместо Hunspell» — предложение, не готовая
  совместимая реализация. Особенно нельзя считать control просто raw .dic:
  остальные flags, case и compound/forbidden/need-affix правила не исчезают.
  Проверка только emitted positives и случайных negatives не доказывает полноту.
- Допуск к выпуску данных не выводится автоматически из permissive notice:
  provenance, преобразования, обязательства и release_approved проверяются отдельно.
- `keydown.key/code` не становится универсальной авторизацией раскладки/поля.
- Предложенные размеры/скорости словаря не являются измерениями.
- Старые IME-лаборатории и терминальный путь не проверены этим snapshot-review;
  терминалы остаются milestone, смена shell на zsh не требуется и не выполнялась.

Следующий ограниченный deliverable — словарная реализация/формат с доказанным
паритетом full/control/stem на существующих synthetic Hunspell cases, затем
отдельно разрешённый реальный пакет данных. Браузерная fixture пока не ежедневный
продукт: real editor integration, data admission, permissions/package, terminal и
мост двойного Super остаются открытыми. Новая схема распознавания не вводится.
