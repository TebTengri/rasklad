# Manual-preview MVP: реализация и проверка

SYS-239. Проверки 11–12 ноября 2026, Qt 6.12.0 / Hunspell 1.7.5 / Omarchy.
Реализован собственный Qt Quick редактор; системный ввод остаётся вне MVP.

## Что работает

- Направление US↔RU выбирается явно; Preview преобразует текст без словарей.
- Apply требует актуального снимка и явной команды. Undo возвращает последнюю Apply
  или массовую правку. Обычный одиночный ввод сбрасывает запись; новая массовая
  правка заменяет её. Старая Apply не может затереть новую редакцию. Clear/закрытие непустого окна требуют
  подтверждения. Нативная история QTextDocument отключена; ошибка этого guard
  оставляет редактор read-only.
- Дополнительная Check word использует точный локальный профиль ADR-001:
  full/control Hunspell пары, stem-index и существующую модель. Все данные
  проверяются; при ошибке ручное преобразование остаётся доступным.
- Один worker владеет provider, очередь ограничена одним ожидающим запросом.
  Изменение документа очищает очередь и инвалидирует ответ. Protected/unknown,
  неизвестная раскладка и неподходящие токены не попадают в очередь.
- Моноширинный интерфейс с прямыми углами, акцентом текущей темы Omarchy,
  EN/RU, клавиатурой и двумя панелями; при узком окне панели расположены сверху
  вниз и доступны с вертикальной прокруткой при высоте 480 px. Тема/язык следуют файловым событиям, включая atomic replacement.

## Подтверждённые проверки

| Проверка | Результат |
| --- | --- |
| `bin/test -silent` | 602 passed, 0 failed; один намеренный skip optionalRealProfile |
| Явный optionalRealProfile с локальным одобренным cache | 3 passed, 0 failed |
| Сборка приложения без Hunspell и app_tests | Собрана; 46 passed, 0 failed, один optional skip |
| ASan + UBSan + LeakSanitizer, app_tests | 46 passed, 0 failed, один optional skip; диагностик sanitizer нет |
| Production router против 006, оба профиля | 221 090 строк / 442 180 действий, 0 расхождений |
| Production provider против признаков 006 | 140 259 подходящих строк / 14 189 уникальных layout-token пар, 0 расхождений |
| Реальный QML offscreen | EN/RU, смена языка, узкое окно, жёлтый акцент, mouse/keyboard flow, oversize reset |
| Клавиатура | Q/?/пробел вводятся; Ctrl+Enter/Shift+Enter/Z/F1/Q выполняют ожидаемые действия |
| Native Wayland `--self-check` | PRIMARY false, windowCreated false, profileLoaded false |
| ELF/сборка Linux | PIE, NX stack, full RELRO/BIND_NOW; stack protector strong, release FORTIFY_SOURCE=3 |

LSan первоначально не мог работать внутри ptrace-ограничения sandbox; финальный
санитизированный прогон выполнен вне этого ограничения, только на искусственных
фикстурах и публичных файлах. Словари/корпус для обычных тестов не требуются.
Скриншоты вымышленного текста остаются в ignored `build/tests/ui/screenshots/`.

## Clipboard: найденная граница Qt

Сам `ClipboardWriter` не читает буфер, однако стандартный Qt TextEdit способен
публиковать выделение в PRIMARY. Это подтверждено по [исходникам Qt TextControl](https://raw.githubusercontent.com/qt/qtdeclarative/dev/src/quick/items/qquicktextcontrol.cpp).
В процессе rasklad до подключения Wayland отключены primary-selection и оба
известных data-control manager interfaces; механизм blacklist есть в
[Qt Wayland display](https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/wayland/qwaylanddisplay.cpp).
Существующие ограничения окружения сохраняются. Если capability PRIMARY всё
ещё доступен, окно не открывается. Проверка выполнена с фактически установленным
backend, без окна и доступа к содержимому clipboard. X11 с PRIMARY не поддержан.
Обычный clipboard доступен по явным Copy/Cut/Paste, включая стандартные сочетания.

## Ресурсы

Один свежий процесс на режим, пустой документ, RU UI, offscreen/software renderer,
5 секунд; `/proc` actual app PID с шагом 20 мс, OS page cache не очищался.

| Режим | Peak RSS | RSS к концу | CPU за последние ≈2 с |
| --- | ---: | ---: | ---: |
| Ручной | 69 768 KiB ≈68,1 MiB | 69 768 KiB | 0,01 с |
| Локальный профиль | 102 532 KiB ≈100,1 MiB | 97 156 KiB | 0,01 с |

Это наблюдения с точностью kernel ticks, не гарантия постоянного нулевого CPU,
не GPU/native-desktop benchmark и не добавочная RAM: RSS включает shared pages.
Процессы измерения остановлены SIGTERM после отсчёта; lifecycle отдельно проверен
тестами. Бинарник этой Linux-сборки — 340 584 B, без Qt/system библиотек и данных;
дистрибутив не собирался. [Агрегаты ресурсов](results/mvp-001-resources.json) и
[агрегаты проверки](results/mvp-001-review.json) не содержат слов или весов.

## Пределы результата

Нет глобальных hooks, replay/uinput, автозапуска, установки, телеметрии и обучения
на пользовательском тексте. Данные/weights остаются local-experiment-only,
release_approved=false. Фиксированная ANSI us-basic/ru-default пара не учитывает
кастомные keymaps. Секретное стирание RAM не обещается; same-UID/root, IME,
accessibility, compositor и история внешнего clipboard manager вне изоляции MVP.
Безопасность замены в чужих приложениях и реальные end-to-end задержки не доказаны;
это отдельный SYS-223. См. [модель угроз](MVP-THREAT-MODEL.md).

Первоначальные C++/QML и тесты написал Claude. Codex независимо сверил с 006,
собрал и проверил программу, затем исправил найденные ошибки Qt API, lifecycle,
очереди, UI/clipboard и дополнил тесты после исчерпания лимита Claude.

Повторное read-only ревью Claude не нашло high-severity проблем и выявило одну
medium и три low: невозможность восстановить массовое удаление, тихий отказ
editor guard, минимальную высоту узкого окна и cleanup worker при неудачном
старте потока. Все четыре устранены; Codex дополнительно устранил цикл размеров
QML и повторил 602 Qt, 46 no-Hunspell и 46 sanitizer проверок. Массовая правка
получает свою одну Undo; старая Apply после одиночной новой правки недоступна.
При отказе native-undo guard печать блокируется. Окно 560×480 прокручивается до
footer, действия доступны; QML diagnostics отсутствуют. Ошибка старта потока
покрыта cleanup через QPointer, но OS fault injection не выполнялся.
