# Агенты и инструменты

Claude Code — исполнитель; Codex — независимая проверка. Доступ на машине у них не изолирован друг от друга; правила проекта не заменяют sandbox.

## Claude Code

На уровне проекта включены официальные плагины Anthropic:

- feature-dev — исследование, архитектура и реализация;
- frontend-design — проработка интерфейса (применяем к Qt Quick с учётом стиля Omarchy);
- code-review — дополнительный обзор PR по запросу;
- security-guidance — проверки опасных шаблонов и облачные проверки изменений.

Владелец отдельно разрешил облачные проверки rasklad. Для приватных проектов это разрешение не действует. Плагины не гарантируют отсутствие уязвимостей, а code-review не заменяет анализ гонок и контекстных ошибок.

Источники установок различаются: feature-dev, frontend-design и security-guidance установлены из anthropic-plugin-directory; code-review — из claude-plugins-official. Идентификаторы сохранены в .claude/settings.json. По полю author.name метаданных выбранных плагинов на момент установки автор — Anthropic. Для воспроизводимой проверки авторства и источников: [официальный манифест каталога, снимок dceb15d](https://github.com/anthropics/claude-plugins-official/blob/dceb15d50832e6adf380bedc26dde03d251324b9/.claude-plugin/marketplace.json); искать четыре имени и их author/source. Два идентификатора источника не объявляются одним репозиторием. Наличие произвольного плагина в каталоге не означает авторство Anthropic.

clangd-lsp пока не установлен: исполняемый clangd не найден. Для C++ его имеет смысл добавить после отдельной установки зависимости; наличие плагина не заменяет language server.

## Context7

В Claude Code и Codex подключён официальный удалённый MCP: `https://mcp.context7.com/mcp`. Локальный npm-сервер не устанавливался. В Claude использовано прямое MCP-подключение вместо оболочки-плагина, установка которой не завершилась.

Подключение пользовательское и доступно другим проектам. Оно предоставляет документацию; в запросах разрешены только публичные имена библиотек, версии и обезличенные вопросы. Не отправлять код, секреты, пользовательский ввод или сведения о приватных проектах.

MCP initialize и tools/list проверены: HTTP 200, доступны resolve-library-id и query-docs. OAuth завершён отдельно в обоих клиентах: codex mcp list показывает OAuth, claude mcp login подтвердил Authenticated. Учётные данные хранятся штатными клиентами вне репозитория.

Версии при установке 2026-11-10: feature-dev 1.0.1-dceb15d50832, frontend-design 1.1.1-dceb15d50832, code-review 1.0.1, security-guidance 2.0.17-fa4eb54397ee. Это снимок окружения, не закрепление будущих обновлений. security-guidance при запуске может установить Agent SDK в собственное пользовательское Python-окружение; его автоматические проверки расходуют дополнительные лимиты. Политика конкретного проекта задана в .claude/claude-security-guidance.md.

После установки начать новые сеансы агентов, чтобы загрузились плагины и MCP. Проверка: `claude plugin list`, `claude mcp get context7`, `codex mcp get context7`.

Документация: [Claude plugins](https://code.claude.com/docs/en/discover-plugins), [Context7](https://github.com/upstash/context7), [Codex MCP](https://developers.openai.com/codex/mcp).
