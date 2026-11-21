# Изолированный native Wayland transport

2026-11-14. Настоящий Chromium 155 / Wayland text-input-v3 → private Sway/wlroots
→ Fcitx → существующее локальное evidence core прошёл 16 функциональных
проверок. Отдельно прошли 8 reference guard-тестов и 4 guard-теста нового nested probe. Это reference transport, не готовая
поддержка Hyprland или Slack.

Проверены composition до пробела, два разных исправления, English Keep,
следующее слово, native Undo, Backspace, сохранение оригинала при Escape,
mixed/long token, password passthrough, переход фокуса без утечки и продолжение
ввода после аварийного завершения IME. В этом native reference-тесте текущее
слово сохранилось; в GTK/X11 оно терялось. Разницу нельзя обобщать на Hyprland.

D-Bus input frontend отсутствовал по introspection; X server/DISPLAY отсутствовали.
CDP не печатал текст: фиксированные тестовые клавиши отправлял собственный
virtual-keyboard helper только в закрытый compositor. Использовались private
HOME/runtime/bus, без сети, host seat, GPU и пользовательских профилей.
Границы подтверждаются устройством runner и guard-тестами, не глобальным
аудитом всех изменений ОС.

Последний успешный прогон: Fcitx с ядром RSS/high-water 57492 KiB, CPU 0.26 s
за тест со стартом и проверкой файлов. Это не измерение задержки физического
ввода, idle CPU или общей памяти браузера.

Команды: `python3 bin/prepare-wayland-lab` проверяет закреплённые runtime
артефакты и подписи; `python3 bin/test-wayland-ime` только показывает план.
Явный запуск: `python3 bin/test-wayland-ime --run --input-root /home/mvn/Work/rasklad`.
Пакеты находятся только в ignored build/, в ОС ничего не установлено.
Runtime pins: data/wayland-lab-runtime-lock.json. Неизменённый MIT protocol XML
с notices и provenance находится в tests/wayland_ime/protocol/.

Установленный Hyprland без GPU backend не создал display, включая попытку
вложения в private software Sway. Следующий полезный тест требует отдельно
подготовленного и согласованного nested окна на реальном compositor с одним
render-node. Он пока не разрешён и не выполнен. Обычный Slack не подключён.
