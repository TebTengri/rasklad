# SYS-244: существующее evidence core в настоящем Chromium/Fcitx

2026-11-14. В отдельной developer variant synthetic rule полностью исключён
preprocessor-ом. Вместо него вызывается прежний LocalEvidenceProvider,
productionPins и SafeConfirmed router; только Correct может менять слово.
Suggest/Keep/Abstain сохраняют оригинал. Нет новых словарей, обучения, corpora,
телеметрии, сети или изменения допуска распространения данных.

Claude написал pimpl bridge и qmake variant; Codex подключил её к addon,
независимо прочитал код и проверил сборку/положительный и отрицательный прогоны.
Init один раз на Fcitx main thread читает проверенный /lab/profile; запросы
на той же нити. Нет QCoreApplication/background thread. Если профиль не загрузился,
addon вообще не регистрирует обработчики и не перехватывает ввод. При runtime
exception provider отключается; последующие клавиши идут обычным путём.

В bwrap только 20 фиксированных source/profile/notice файлов привязаны read-only
с сохранением относительных путей. Полный рабочий каталог, HOME и corpus не
монтируются. Старый loader сверяет bytes/SHA-256/metadata/notices/control affix,
версию/хеш загруженной Hunspell, model/stem format и повторно aff/dic после чтения
движками. Это trusted local test, не sandbox против всех багов Hunspell и не
защита от модификации проекта злоумышленником того же UID.

## Наблюдения

Настоящий native-key input из private XTest в установленный Chromium/GTK IM
через Fcitx исправляет ghbdtn в привет и ghbvth в пример; hello остаётся hello.
Два разных слова плюс compile-time отсутствие fixed rule исключают прежнюю
hardcoded demonstration. Это функциональные примеры, не новая оценка точности.

16 проверок: composition lifecycle, два исправления, английский Keep, следующее
слово, exact Undo к предыдущему commit, Backspace, Escape, mixed/long token
bypass, password, отсутствие переноса слова между полями, обычный ввод после
crash и отсутствие самопроизвольного перезапуска daemon. Дополнительно исходное
слово сохраняется в старом поле при blur. 8 guard tests.

Escape теперь явно коммитит исходное слово без исправления и проходит приложению:
просто очистить preedit недостаточно для одинаковой cancel-семантики Qt/Chromium.
Это выбор сохранности текста/обычных app shortcuts, а не скрытие ошибки теста.

Отрицательный контроль подменяет только виртуальный mount data/sources.json на
временный заведомо неверный файл. Оригиналы не меняются. Профиль отвергнут,
composition не начинается, ghbdtn + Space вводится без изменения. Нет fallback
на демонстрационное правило или ослабленную проверку.

Собственный Fcitx с ядром: RSS/high-water 56036 KiB (около54.7 MiB), CPU0.15s
в этом прогоне, включая startup/profile verification/запросы. Это не idle baseline,
не latency физических клавиш и не полная память Chromium.

**Готовность к рабочему использованию НЕ подтверждена.** При SIGKILL собственного
Fcitx в private PID namespace дальнейший ввод работает, но текущие незавершённые
шесть символов теряются. Этот дефект/ограничение остаётся открытым. Никакого
пользовательского процесса не завершали. Native Wayland/Slack и произвольные
code/mention поля не проверены; лабораторный Prose — явный контракт fixture.

Агрегат/hash: data/chromium-ime-profile-001-result.json. Распространение профиля
и сборки по-прежнему не допущено. Working IME/Slack/profile/config не менялись.

## Воспроизведение подготовленной variant

```sh
mkdir -p build/chromium-ime/profile-addon
(cd build/chromium-ime/profile-addon && qmake6 ../../../tools/fcitx_profile_lab.pro && make -j2)
bin/test-chromium-ime --run-profile --input-root /path/to/rasklad-inputs
bin/test-chromium-ime --test-profile-rejection --input-root /path/to/rasklad-inputs
```

Доступ к ранее подготовленному локальному профилю выбирается явно; default
launcher не читает его и остаётся plan-only. Не использовать чужой project/HOME
или произвольные словари. Следующий этап: native Wayland transport и exact Slack
без downgrade рабочего приложения ради обхода и без обещания универсальности.
