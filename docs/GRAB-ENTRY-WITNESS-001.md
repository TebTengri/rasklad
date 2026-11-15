# Избирательный свидетель Wayland grab callback

## Основание

Проба007 показала одинаковые all-press/release6/7 на входе Core и в нашем
обработчике; Space0/1 на обоих уровнях. Остальные три focus-сценария,
baseline17, cleanup и живость compositor прошли. Данные находятся в
`data/hyprland-core-entry-007-result.json`. Это сужает наблюдаемую потерю до
пути перед публичным Core entry, но не доказывает конкретную причину.
Разрешение007 использовано.

## Следующая граница

Отдельная диагностическая сборка перехватывает публичный
`wl_proxy_add_listener` только для `zwp_input_method_keyboard_grab_v2` v1.
Четыре callback копируются по уже закреплённому MIT XML; меняется только key.
Оригинальные registration-data и фактические callback-data сохраняются.
Оригинальный callback вызывается один раз; после него proxy/data/slot не
читаются. Другие классы делегируются без изменений. Это код Codex;
независимый агент проверил ABI/lifetime, другой проверил Python admission.
Сгенерированный header остаётся в build/. Тела LGPL библиотек не копируются.

128 монотонных slots ограничивают память и поддерживают повторное использование
адреса proxy. Только успешная регистрация становится активной. Неизвестная
версия, нехватка slots, отсутствующий callback или ошибка регистрации сохраняют
исходный вызов, но снимают coverage. Неподдерживаемый поток завершает только
лабораторный Fcitx; глобальный preload не используется.

Schema4 содержит43 uint32: markers,22 addon counters,10 Core counters,
coverage и6 grab counters. Payload, адреса, timestamps и произвольные keycodes
не экспортируются. Счётчики saturate255; saturation, потеря coverage или
неизвестный state делают интервал неполным. До интерпретации нулей требуется
положительная калибровка own/Core/grab и успешная регистрация.

## Повторы и пределы

`--focus-repeats 1..5` повторяет только word→other→prose→Space. Каждый повтор
имеет свежий одноразовый witness, строгий oracle и cleanup. Остальные три
сценария выполняются однажды. Ошибка текста остаётся FAIL независимо от
следующего повтора; неполная диагностика останавливает зависимые строки.
Повторы внутри одного сеанса коррелированы и не дают IID оценку вероятности.
Прежний внешний предел75s не увеличивается.

Новый режим не добавляет доступов к host. Headless reference проверяется
отдельно без host display/GPU. Новое подключение к parent Wayland/renderD128
требует отдельного разрешения после завершения preflight. Рабочие Slack,
IME и настройки не изменяются. Ни диагностический PASS, ни согласие ревьюеров
не считаются исправлением Hyprland.

## Завершённый preflight

77 Python admission checks,17 reader/repeat tests,12 legacy Core controls и
19 новых закрытых grab forwarding/admission controls проходят. Headless Sway
с настоящим Fcitx/Chromium: baseline17 и focus8 PASS. Все пять целевых строк
дали own/Core/grab7/7, Space1/1; coverage полная, unknown-state0, максимум
счётчика к концу последнего измерения216<255. Это положительная проверка
реальной привязки wrapper, а не только synthetic fake registration unit.
Полные ограниченные результаты и source hashes:
`data/grab-entry-witness-preflight-001.json`.
