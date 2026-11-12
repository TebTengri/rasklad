# Клиент проверки настоящих Qt-полей

Отдельный developer-клиент, не backend rasklad. Создаёт собственное окно с Qt Quick TextEdit и Password TextInput, посылает синтетические события только своему QQuickWindow и проверяет фактический текст полей. Пользовательский ввод не читается. Нет clipboard API, глобальных hooks и переключения системной раскладки.

Сборка из корня проекта:

```sh
mkdir -p build/native-field-client
cd build/native-field-client
qmake6 ../../tests/native_field_client/native_field_client.pro
make -j2
```

Цель не включена в `rasklad.pro`, `bin/build` или `bin/test`. Зависимости клиента: Qt Core/Gui/Qml/Quick; **Qt Wayland Compositor не линкуется**.

Клиент предназначен только для закрытого fixture: до QGuiApplication проверяет marker `RASKLAD_NATIVE_FIELD_FIXTURE=1`, точные runtime/socket/platform и отсутствие `WAYLAND_SOCKET`, `WAYLAND_DEBUG`, `DISPLAY`. Обычный запуск отказывается с кодом 2 до открытия окна. Это защита от случайного запуска, не аутентификация от процесса того же UID. Не задавайте marker ради подключения к рабочему desktop; fixture должен обеспечивать изоляцию и приватный compositor.

Фиксированные `--scenario`: `baseline`, `prefix`, `reverse`, `typing-race`, `focus-race`, `selection-race`, `post-send-race`. Другие аргументы отклоняются, произвольный текст/путь не принимается. JSON содержит только whitelisted label и booleans, без содержимого полей. Несовпадение ожидаемого текста/выделения/фокуса возвращает 1. Нулевой код клиента не доказывает корректность принимающего compositor — его независимые counters проверяются отдельно.

Полный локальный опыт использует второй процесс — private Qt Wayland Compositor fixture. Его источник и бинарник пока находятся только в ignored `build/native-field-probe/`: Qt Wayland Compositor GPL-3.0-only, распространение этого компонента ещё не согласовано; основной runtime такой зависимости не получает. Поэтому чистый клон содержит клиент и агрегаты, но не является полностью воспроизводимым комплектом server fixture.

Методика и результаты: [INPUT-LAB-003](../../docs/INPUT-LAB-003.md). Это тест text-input-v3 с известными синтетическими сценариями, а не поддержка Hyprland или измерение точности детектора.

`post-send-race` требует stdin FIFO и ограниченного metadata-only handshake `prepare`/`mutate`; ACK содержит только phase. Клиент допечатывает фиксированный символ до обработки уже отправленной Wayland правки. На проверенном fixture критерий сохранности **провален**, клиент штатно возвращает 1. Это сохранённый блокер, не успешный тест безопасности.
