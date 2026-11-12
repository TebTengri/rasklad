# INPUT-LAB-001: изолированный стенд input-method-v2 (SYS-223)

Дата: 2026-11-12. Задача: SYS-223. Статус: developer-инструмент и статическая ревизия; SYS-223 не завершена, автоматическое исправление выключено.

## Что это и чего это не доказывает

Изолированный Qt Test (`tests/input_lab`), где настоящие libwayland-server и libwayland-client обмениваются сообщениями `zwp_input_method_v2` внутри одного тестового процесса. Цель — сравнить две модели обработки serial на настоящем wire-формате. Strict — консервативная политика стенда, не доказательство атомарной защиты принимающего приложения. Это нужно будущему автоматическому исправлению, а не выделению текста.

Лаборатория **не** устанавливает и **не** проверяет:

- корректность назначения поля (purpose/hint) в реальных приложениях, защиту паролей и блокировку экрана;
- совместимость с Hyprland или другим композитором: код Hyprland не запускался и не копировался;
- поведение реальных клиентов text-input-v3 (GTK, Qt, Chromium и др.);
- безопасность production: backend, контроллер и интерфейс политики не созданы, приложение не изменено и остаётся только ручным предпросмотром.

## Воспроизведение

```
bin/test-input-lab          # сборка + все тесты
bin/test-input-lab -v2      # подробный вывод Qt Test
bin/test-input-lab staleSerialAfterFocusChange
```

Скрипт: проверяет SHA-256 вендорного XML (отказ при несовпадении) → `wayland-scanner server-header|client-header|private-code` в `build/input_lab/generated` → `qmake6` + `make` в `build/input_lab` → запуск `build/input_lab/input_lab_tests` с удалёнными `WAYLAND_DISPLAY`, `WAYLAND_SOCKET`, `WAYLAND_DEBUG`, `XDG_RUNTIME_DIR`, `DISPLAY`. Тест сам падает в `initTestCase`, если первые три заданы. Нужны qmake6, Qt 6 Core/Test, pkg-config `wayland-server`, `wayland-client`, `wayland-scanner`. Root, сеть, установка и изменение настроек не требуются. `bin/build` и `bin/test` лабораторию не собирают.

Независимая проверка Codex: 30 тестов стенда прошли; те же 30 прошли с ASan/UBSan/LSan без диагностик. Регрессионный прогон существующего приложения: 602 passed, 0 failed, 1 skipped (необязательный локальный профиль). Итоги и полные хеши источников: [input-lab-001-review.json](results/input-lab-001-review.json). Первоначальный запуск внутри инструментального sandbox не смог создать socketpair; успешные прогоны выполнены с разрешением на собственную socketpair и удалёнными переменными display, без root и доступа к рабочему сеансу.

При ревизии исправлены: освобождение обоих концов socketpair при неудачном `wl_client_create` (API передаёт владение только при успехе), отказ от NUL в строках модели и повторный `poll` после EINTR только через ограниченный внешний цикл. Сборка отделена от приложения; его исходники и зависимости не менялись.

## Изоляция

- Сервер: `wl_display_create()` без `wl_display_add_socket*`; клиент создаётся только через `socketpair(AF_UNIX)` + `wl_client_create`. Клиент: только `wl_display_connect_to_fd`; `wl_display_connect` не вызывается.
- Глобалы: `wl_seat` v1 с `capabilities = 0` и `zwp_input_method_manager_v2` v1. Нет `wl_compositor`, поверхностей, устройств, клавиатуры, clipboard, evdev, uinput.
- Неподдерживаемые запросы (`set_preedit_string`, `get_input_popup_surface`, `grab_keyboard`, `wl_seat.get_pointer/get_keyboard/get_touch`) завершают клиента ошибкой протокола. Клиент лаборатории отправляет `grab_keyboard` и `get_keyboard` только в тесте отказа фейкового сервера; захвата клавиатуры нет.
- Однопоточная прокачка: каждый round trip ограничен 256 итерациями и 2 с; `poll` по 5 мс. Завершение: клиент отключается до сервера; сервер вызывает `wl_display_destroy_clients` и `wl_display_destroy`.
- Текст — вымышленные строки в коде теста; ввод пользователя, файлы и сеанс не читаются.

## Протокол: правило serial

[`input-method-unstable-v2.xml`](https://raw.githubusercontent.com/swaywm/wlroots/0855cdacb2eeeff35849e2e9c4db0aa996d78d10/protocol/input-method-unstable-v2.xml) (wlroots `0855cdac…`, SHA-256 `99414dba…0703`):

- строки 302–307: serial в `commit` должен равняться числу уже выданных этим объектом `done`; при несовпадении композитор «must proceed as normal, except it should not change the current state»;
- строки 85–90: `activate` сбрасывает состояние, связанное с `commit_string`, `delete_surrounding_text` и `set_preedit_string`;
- строки 66–68: запросы в неактивном состоянии принимаются, но благодаря serial и сбросу при `activate` не влияют на следующий text input;
- строки 38, 129, 266: смещения и длины — в байтах UTF-8; строки 125–127 и 218–220: текст не длиннее 4000 байт;
- строки 348–357: `unavailable` — единственное событие для второго IM на seat; после него запросы игнорируются.

## Статическое чтение исходников Hyprland

Читались только файлы из `build/input-lab-source/sources.json`; хеши — оттуда. Это чтение кода, а не воспроизведённый сбой композитора.

| Файл | Закрепление | SHA-256 |
| --- | --- | --- |
| [InputMethodV2.cpp](https://raw.githubusercontent.com/hyprwm/Hyprland/v0.56.2/src/protocols/InputMethodV2.cpp) | тег `v0.56.2` | `a1f5f782…677f` |
| [TextInputV3.cpp](https://raw.githubusercontent.com/hyprwm/Hyprland/v0.56.2/src/protocols/TextInputV3.cpp) | тег `v0.56.2` | `1ba5ab9b…d5bf` |
| [InputMethodRelay.cpp](https://raw.githubusercontent.com/hyprwm/Hyprland/v0.56.2/src/managers/input/InputMethodRelay.cpp) | тег `v0.56.2` | `f3abdcb4…570b` |
| [InputMethodRelay.hpp](https://raw.githubusercontent.com/hyprwm/Hyprland/efb50993780079460b0cbed1363e2166a2de1d9f/src/managers/input/InputMethodRelay.hpp) | commit `efb50993…` | `c5067735…24ac` |
| [TextInput.cpp](https://raw.githubusercontent.com/hyprwm/Hyprland/efb50993780079460b0cbed1363e2166a2de1d9f/src/managers/input/TextInput.cpp) | commit `efb50993…` | `bd732ac7…b869` |

Наблюдения (номера строк — по этим снимкам):

1. **Serial не проверяется.** `InputMethodV2.cpp:181–185`: обработчик `setCommit` получает `uint32_t serial`, но не использует его: `m_current = m_pending; m_pending.reset(); m_events.onCommit.emit();`. В файле нет счётчика `done`: `done()` (`:254–256`) только отправляет событие.
2. **Правка уходит в поле, сфокусированное в момент обработки.** `InputMethodRelay.cpp:28–37`: по `onCommit` вызывается `getFocusedTextInput()` и `PTI->updateIMEState(...)`. `getFocusedTextInput()` (`:73–88`) сначала ищет включённый text input на текущей поверхности фокуса, затем возвращает **любой** text input этой поверхности, включая отключённый.
3. **Устаревшая правка получает свежий serial text-input-v3.** `TextInput.cpp:259–272`: для v3 пересылаются `preeditString`, `commitString`, `deleteSurroundingText`, затем `INPUT->sendDone()`. `TextInputV3.cpp:96–98`: `sendDone()` передаёт `m_serial`, который увеличивается при каждом `commit` клиента text-input (`:21–25`). Поэтому проверка serial на стороне приложения, по прочитанному коду, видит текущее значение и не отличает правку, вычисленную IM по старому состоянию.
4. **`activate` не сбрасывает отложенные запросы IM.** `InputMethodV2.cpp:228–233` только отправляет `activate`; `m_pending` сбрасывается лишь в `commit` (`:144–148`, `:183`). Спецификация (строки 85–90) требует сброса.
5. Согласуется со спецификацией: второй IM получает `unavailable` (`InputMethodRelay.cpp:17–24`).

Ограничения ревизии:

- `TextInput.cpp` и `InputMethodRelay.hpp` закреплены на commit `efb50993…`, а не на теге `v0.56.2`; их совпадение с `v0.56.2` не проверялось.
- Не читались `InputMethodV2.hpp`, `TextInputV3.hpp`, сгенерированные hyprwayland-scanner обёртки, `InputManager`, обработка фокуса и `TextInputV1`. Проверка serial где-то ещё не исключена, хотя в прочитанном пути её нет.
- Сбой в работающем Hyprland не воспроизводился; что делают реальные приложения с такой правкой, неизвестно. Это кандидат на гонку для проверки во вложенном композиторе, не подтверждённая уязвимость. До воспроизведения upstream не уведомлялся.
- Реализации wlroots/sway, KWin, Mutter не сравнивались.

## Модели лаборатории

Оба режима используют одну модель поля: смещения в байтах UTF-8, проверка границ символов, корректности UTF-8, предела 4000 байт и «всё или ничего». Порядок применения — удаление, затем вставка (строки 292–299 спецификации; preedit не поддерживается).

| Поведение | `Strict` (эталон по спецификации) | `UncheckedModel` (модель прочитанного пути) |
| --- | --- | --- |
| serial ≠ число `done` | отклонить, состояние не менять | применить; счётчик `staleForwarded` (набл. 1, 3) |
| IM неактивен, поле ещё в фокусе | игнорировать | применить к сфокусированному полю (набл. 2) |
| отложенные запросы при `activate` | сбросить | сохранить (набл. 4) |
| объект после `unavailable` | игнорировать | игнорировать |

`UncheckedModel` — явно названная **модель**, а не Hyprland runtime: код Hyprland не исполняется, в него не входят ни text-input-v3, ни сеть клиентов, ни фокус поверхностей.

## Тесты

| Тест | Что проверяется |
| --- | --- |
| `utf8Model` | валидатор UTF-8 (обрезанные, overlong, суррогаты, > U+10FFFF), границы символов, переполнение `before/after`, предел размера |
| `embeddedNulRejected` | NUL отклоняется в исходном поле, синтетическом вводе и правке до обрезания на wire; текст не меняется |
| `wireOrderingOnActivateAndRefocus` | порядок `activate, surrounding_text, text_change_cause, content_type, done`; double buffering на `done`; смена фокуса `deactivate, done, activate…`; передача purpose; `capabilities = 0` |
| `validReplacement` (оба режима) | `ghbdtn` → `привет`: удаление 6 байт, курсор 12 байт при 6 единицах UTF-16; сервер повторно отправляет состояние |
| `staleSerialAfterNewTyping` | правка по `done #1` приходит после `done #2` (пользователь набрал « c»): Strict сохраняет новый ввод; модель удаляет его (`alpha brbravo`) |
| `staleSerialAfterFocusChange` | правка для поля A приходит после перевода фокуса на B: Strict ничего не меняет; модель портит B (`thrtwo`); правка со свежим serial принимается в обоих режимах |
| `pendingAcrossActivation` | `commit_string` без `commit` до смены фокуса: Strict сбрасывает; модель вставляет его в B |
| `commitAfterDeactivate` | свежий serial после `deactivate`: Strict игнорирует; модель меняет поле |
| `invalidEditsRejected` | разрез кодовой точки, выход за начало/конец, некорректный UTF-8 и превышение 4000 байт: поле и `done` не меняются |
| `unsupportedRequestsRejected` | `grab_keyboard`, `set_preedit_string`, `wl_seat.get_keyboard` → ошибка протокола; объект IM удалён, поле не изменено |
| `secondInputMethodUnavailable` | второй IM получает только `unavailable`; его `commit` игнорируется |
| `serverUnavailable` | после `unavailable` правки игнорируются, новые события не отправляются |
| `disconnectCleansUp` | отключение с отложенной правкой: объект удалён, ничего не применено, round trip без клиента завершается ограниченно |

## Выводы для SYS-223 (гипотезы, не разрешения)

- Будущий IM-клиент rasklad не может полагаться на то, что композитор отбросит устаревший `commit`. Перед отправкой правки он должен сам сверять ожидаемый текст и курсор с последним `done` и отказываться при любом новом `done` после вычисления правки; это уменьшает окно, но не закрывает гонку на стороне композитора.
- `commit_string` и `delete_surrounding_text` не следует отправлять без немедленного `commit`, чтобы отложенное состояние не пережило смену фокуса.
- Строгий сервер тоже не различает отложенный запрос, отправленный клиентом до получения `activate`, но обработанный после него: serial защищает только `commit`.

## Следующий шаг: вложенный композитор

1. Отдельная VM или пользовательский сеанс без рабочего стола владельца; Hyprland 0.56.2 в headless/nested режиме с собственными `XDG_RUNTIME_DIR` и `WAYLAND_DISPLAY`, без физического seat-а, если это возможно.
2. Синтетический клиент text-input-v3 (без реальных приложений) и IM-клиент лаборатории, подключённые к этому экземпляру; вымышленный текст.
3. Повторить сценарии `staleSerialAfterNewTyping` и `staleSerialAfterFocusChange` с управляемыми задержками; записать фактический порядок событий без payload.
4. Только после воспроизведения — сверка с текущим upstream и решение владельца об отчёте.
5. Отдельно: прочитать недостающие файлы (см. ограничения) и сравнить с wlroots.

## Уточнение после INPUT-LAB-003

Реальный Qt text-input-v3 клиент применяет устаревшую правку после нового локального ввода. Несовпадение `done(serial)` не требует отклонения изменений по спецификации. Поэтому даже корректный serial input-method-v2 и проверка до отправки не доказывают сохранность текста на принимающей стороне. Успешные Strict-тесты выше остаются проверками модели; исходное предположение об их достаточности отозвано. [Воспроизведение и первичные источники](INPUT-LAB-003.md).
