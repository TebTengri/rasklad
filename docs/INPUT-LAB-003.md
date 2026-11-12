# INPUT-LAB-003: автоматическая замена в настоящем Qt-поле

SYS-223, 2026-11-12. Claude написал исходный probe; Codex независимо проверил, исправил Qt API/focus, защиту fixture и запустил реальные клиентские сценарии. Агрегаты: [input-lab-003.json](results/input-lab-003.json).

## Практический результат

В отдельном Qt Quick клиенте текст `ghbdtn ` автоматически стал `привет ` без выделения и команды Apply. Изменение пришло через настоящее Wayland `text-input-v3`, после чего сам клиент проверил содержимое своего TextEdit. Источник замены — существующая `rasklad::convertLayout`, а не прямая установка нужного текста в клиенте.

Клиент синтетически печатает в собственное окно; это не перехват физической клавиатуры. Процесс fixture принимает роль compositor и отправляет протокольные события. Поэтому результат **не** означает, что обычный процесс rasklad уже может безопасно менять текст в других программах рабочего сеанса.

## Проверенные сценарии

| Сценарий | Проверка настоящего поля и протокола | Результат |
| --- | --- | --- |
| baseline | `ghbdtn ` → `привет ` без выделения; ровно одна замена | Passed |
| prefix | `hello ghbdtn ` → `hello привет `; предыдущее слово сохранено | Passed |
| reverse | `привет ` → `ghbdtn `; 7 единиц UTF-16 удаляются как 13 байт UTF-8 | Passed |
| typing-race | После подготовки плана допечатан `x`; исходный новый текст сохранён, замена отменена | Passed |
| focus-race | До отправки фокус переведён в protected поле с marker; оба поля сохранены, план отклонён | Passed |
| selection-race | До отправки появились выделение и новый курсор; текст/выделение сохранены, план отклонён | Passed |
| post-send-race | После отправки клиент допечатал `x` до обработки протокольной правки; новый текст повреждён | **FAILED — блокер безопасности** |

В каждом запуске отдельно проверен Password TextInput: synthetic text не меняется; protected hints замечены сервером, policy не запрашивает его surrounding text и не отправляет исправление. `protected_text_queries=0` — счётчик только нашей policy; сам Qt compositor может уже хранить полученный от клиента текст. На предварительном диагностическом прогоне protected surrounding text был доступен через Qt API. Поэтому наличие Password UI не является гарантией отсутствия текста на стороне compositor/IME.

`reverse` — механический тест смещений, не пример распознавания неверного языка: корректное русское слово не объявляется ошибкой. Выбор фиксированного trigger — условие fixture, не доказательство качества детектора. После явного разрешения владельца выполнен один baseline с ранее подготовленными локальными словарями и моделью: SHA-256 проверены загрузчиком, `profile_loaded`, `router_called`, `router_correct`, `overall_passed` — true. Настоящий safe-confirmed router выбрал исправление, клиент подтвердил результат. Это один синтетический пример, не оценка точности модели; данные не опубликованы и не передавались Claude.

## Что именно реализовано в опыте

- Два процесса, реальный Qt Wayland client и CPU-only Qt Wayland Compositor. Только приватный socket `rasklad-native-test`; нет связи с рабочим compositor.
- XDG toplevel активируется только в fixture. `QWaylandQuickItem::takeFocus` задаёт не только keyboard focus, но и text-input focus; одного `QWaylandSeat::setKeyboardFocus` оказалось недостаточно.
- Неэкспортируемый `QWaylandInputMethodControl` не вызывается напрямую: используется экспортируемый Qt Quick proxy. Его query/event API передаёт текст через протокол, не через clipboard или Backspace replay.
- Снимок known synthetic normal-field контекста проверяется повторно после отложенных 300 мс, до отправки. При invalid/protected hints policy не запрашивает surrounding text. Размер контекста ограничен для fixture, одновременно допускается один план.
- Отправляются `delete_surrounding_text`, `commit_string`, `done`; имена сообщений и числовые byte lengths считаются без чтения строковых аргументов логгером. Client assertions независимы от этих counters и сравнивают реальное поле с фиксированным ожидаемым текстом.
- Fixture опрашивает Qt query API раз в 25 мс только во время ограниченного теста. Это не production scheduling design и не новый фоновый процесс приложения.

## Изоляция и лицензия

Каждый прогон — закрытый bwrap: unshare namespaces, readonly `/usr` и два собственных бинарника, private `/proc`, HOME/runtime/tmp, минимальный `/dev`; без GPU/input, host runtime/DBus, соседних проектов и сети. Клиент получает фиксированное чистое окружение. Внешний предел 10 с, внутренний timeout, nice +10, лимиты CPU/address space/core/file size, stop/reap собственных процессов. Все строки вымышленные; сырой протокольный payload не логируется. Qt сообщил о недоступном EGL, затем использовал working shared-memory path; приложение не требовало GPU.

В Git добавлен только [assertion client](../tests/native_field_client/README.md), зависящий от тех же Qt Core/Gui/Qml/Quick, что и приложение. Он не линкуется с Qt Wayland Compositor. Server fixture и его бинарник остаются локальными в ignored `build/`; его GPL-3.0-only зависимость не добавлена в runtime или пакет rasklad. Источник/бинарник fixture не распространяются этим PR; их fingerprints и source snapshot сохраняются локально. Полная воспроизводимость server fixture из чистого клона пока не заявляется.

Первичные сведения: [Qt Wayland Compositor: shared-memory fallback и лицензии](https://doc.qt.io/qt-6/qtwaylandcompositor-index.html), установленный текст GPL-3.0 section 2 проверен для локального использования. Свои и сторонние release-обязанности остаются в [THIRD_PARTY](../THIRD_PARTY.md).

## Почему остановлен GPU-less Hyprland

Новый ограниченный diagnostic capture показал `CBackend::create() failed!` — отказ пути инициализации Aquamarine backend до ввода. В Hyprland эта строка используется и при неудачном `create`, и при неудачном `start`; она не доказывает, что factory вернула nullptr.

В [Aquamarine v0.15.1 Backend.cpp](https://github.com/hyprwm/aquamarine/blob/v0.15.1/src/backend/Backend.cpp) `start()` создаёт GBM allocator из DRM fd и отказывает без allocator для non-NULL backend. Прочитанный Hyprland выбирает headless/DRM/Wayland, а не NULL. Это объясняет архитектурное ограничение выбранного GPU-less плана; полный runtime trace до конкретной строки allocator не получен. Устройства или доступ к рабочему seat ради запуска не добавлялись. Переход к Qt fixture позволил испытать настоящий client protocol без этого расширения прав.

## Выявленная гонка после отправки: критерий не пройден

Metadata-only handshake через private stdin/stdout обеспечивает порядок: клиент приостанавливает обработку Wayland событий; сервер отправляет и flush'ит delete/commit/done; затем клиент допечатывает `x`, подтверждает локальное `ghbdtn x` и возобновляет обработку событий. Устаревшая правка превращает поле в `gпривет `, теряя новый символ. Handshake успешен, `post_send_local_before_dispatch_matches=true`, `stale_edit_corrupted_new_text=true`. Клиент возвращает 1 из-за провала проверки, а не аварийного завершения.

Прежнее предположение о защите одним serial было неверным. [Спецификация text-input-v3, done](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/text-input/text-input-unstable-v3.xml) требует применять изменения и при несовпавшем serial; отличается синхронизация состояния. [Qt 6.12.0, qwaylandtextinputv3.cpp](https://github.com/qt/qtbase/blob/v6.12.0/src/plugins/platforms/wayland/qwaylandtextinputv3.cpp) отправляет QInputMethodEvent текущему focus object до сравнения serial. Это согласуется со спецификацией, а не заявление об уязвимости Qt.

Повторная проверка контекста перед отправкой уменьшает окно гонки, но не делает удаление атомарным на принимающей стороне. Исправления проверки serial в compositor тоже недостаточно для доказательства этого свойства. Strict-сервер INPUT-LAB-001 моделирует более строгую политику; его успешные тесты не доказывают её наличие в приложениях.

## Граница результата

Получены шесть успешных механических сценариев, один успешный baseline настоящего детектора и **один провал критерия сохранности нового ввода**. Универсальный backend с ретроспективным удалением не допущен. Нужен механизм, в котором принимающая сторона атомарно проверяет версию/диапазон правки, либо другой отдельно доказанный способ композиции ввода. Безопасность preedit этим опытом не доказана.

Остаются непроверенными field epoch для одинаковых normal полей, Hyprland relay, GTK/Chromium/terminal/XWayland, блокировка/сон/crash, переключение раскладки и natural typing latency. Автоисправление рабочего сеанса выключено; SYS-223 открыта.
