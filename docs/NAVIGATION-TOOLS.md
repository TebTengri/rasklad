# Навигация без повторного чтения истории

`bin/project-map find <тема>` возвращает короткий срез компонентов, файлов и
команд проверок. `file <путь>` добавляет определения Python из AST и явные
include/import edges. `check` проверяет freshness по SHA исходников и заметок;
`build` обновляет индекс и Markdown. Ни одна команда не запускает приложение,
тесты, сеть или системную настройку. Это не полный call graph: Qt signals,
QML bindings и динамические загрузки требуют проверки исходников.

Семантический граф имеет16 узлов с проверенными путями,17 связями и отдельными
статусами/основаниями. Файловый индекс отражает собственные исходники/тесты/
документацию; ignored build/, корпуса и профили не индексируются. Широкий поиск
в соседних проектах не выполняется. Генератор не читает содержимое JSON данных.
Не загружайте весь project-graph.json в контекст — используйте короткий запрос.
Проверка freshness не заменяет ревью актуальности смысловых утверждений.

Общий навык `rasklad-navigation` находится в project-local `.agents/skills/`,
`.claude/skills/` и `.cursor/skills/`. Копии SKILL.md одинаковы; содержание
карты едино. Инструкции AGENTS/CLAUDE/Cursor направляют туда без hooks и MCP.
Новые сессии должны открывать именно этот репозиторий/worktree. Уже работающий
клиент может перечитать инструкции; автоматическое обнаружение навыка зависит
от поддержки project skills самим клиентом.

## Obsidian

Откройте папку `docs/knowledge` через Open folder as vault, затем `Start.md`.
Установка Obsidian, sync, community plugins и публикация не выполнялись.
Vault — отдельная папка обычных Markdown-заметок, а встроенный Graph view
показывает их внутренние ссылки. Источники:
[Vault](https://obsidian.md/help/vault),
[Graph view](https://obsidian.md/help/plugins/graph).
Заметки производные: правьте docs/project-map.json и обновляйте build.
Корень репозитория/build или домашний каталог в vault не добавлять ради карты.

## Аудит Graphify (не установлен)

Рассмотрен выпуск0.9.109, commit d4a5b323ef8501518b82c58a05fe21b35ce24992;
не утверждается, что это текущий HEAD. Закреплённый источник:
[Release](https://github.com/Graphify-Labs/graphify/releases/tag/v0.9.109).
Лицензия Apache-2.0 с retained MIT portions: при распространении нужны LICENSE,
LICENSE-MIT и NOTICE. Собственная карта написана для rasklad и код Graphify не
копирует. C++/Python/JS поддерживаются, QML в discovery отсутствует.

`extract --code-only` позволяет локальный AST. Но штатный installer/skill
может менять agent instructions/hooks, писать вне проекта, устанавливать или
обновлять зависимости; смешанный extraction может выбрать облачный backend.
Поэтому он не запускался. Если понадобится более глубокий AST index, следующий
шаг — отдельный pinned project-local venv, read-only allowlist кода, no-network
sandbox, результат в build/. Полный skill в агенты автоматически не переносим.
Источники закреплённой версии:
[CLI](https://github.com/Graphify-Labs/graphify/blob/d4a5b323ef8501518b82c58a05fe21b35ce24992/graphify/cli.py),
[Installer](https://github.com/Graphify-Labs/graphify/blob/d4a5b323ef8501518b82c58a05fe21b35ce24992/graphify/install.py),
[Manifest](https://github.com/Graphify-Labs/graphify/blob/d4a5b323ef8501518b82c58a05fe21b35ce24992/pyproject.toml).

Экономия токенов пока не измерена. Практическая цель — сначала один короткий
запрос и2–4 нужных файла вместо полного лога и всех отчётов экспериментов.
