# SYS-244: настоящий Chromium через GTK/Fcitx

2026-11-14. Проверен установленный Chromium155.0.8059.84 с обычным оконным
X11 backend, GTK3 IM module и реальным Fcitx5. Пользовательские данные и рабочий
стол не подключались. Это подтверждение конкретного browser/IM transport,
не готовая поддержка Slack или native Wayland.

## Проверка и результат

Подписанный официальный Arch Xvfb21.1.26-1 извлечён в ignored build/chromium-ime,
не установлен в систему. Подпись gpgv проверена по установленному публичному
Arch keyring, ожидаемый fingerprint ADC8A1FCC15E01D45310419E94657AB20F2A092B.
ASCII keyring предварительно dearmor только в project-local временный каталог.
Package/binary/license SHA/size: data/xvfb-lab-lock.json. Полный COPYING сохраняется
рядом с бинарным тестовым артефактом. Пакет содержит MIT/GPL-2.0-only компоненты;
это отдельный runtime developer tool, не изменение MIT собственного кода и не
одобрение распространения пакета/его зависимостей с приложением.

В private bwrap запускаются Xvfb:77 (-nolisten tcp), Unix-only private D-Bus,
Fcitx с четырьмя allowlisted addons и одноразовый Chromium. Нет host HOME,
runtime, system/session D-Bus, display/Xauthority, GPU/input devices или сети.
Passwd/group/machine-id синтетические. Chromium sandbox сохранён. Флаг -ac
относится только к частному X-серверу: его filesystem и abstract socket изолированы
mount/net namespaces, посторонние host клиенты не получают доступ.

Фиксированные XTest-клавиши посылаются только в видимое окно собственного
Chromium, найденное по _NET_WM_PID, проверенному executable и private lab marker.
Это тестовый источник ввода, не host replay fallback. CDP работает через pipe;
он используется только для загрузки/фокуса/наблюдения fixture, очистки тестовых
полей и закрытия. Все Input.* вызовы запрещены в helper. Нет имитации
QInputMethodEvent, CDP insertText или imeSetComposition.

Сначала ghbdtn даёт настоящие compositionstart/update. В Chromium DOM значение
при этом уже содержит шесть provisional символов: требовать пустой DOM было бы
ошибкой. По Space composition завершается и получается ровно привет + пробел.
Следующее x + Space сохраняется. Ctrl+Z возвращает точное предыдущее состояние
привет + пробел, а не просто произвольно изменяет документ. Смешанный/длинный
токены сохраняются; password ввод не буферизуется; при смене поля слово остаётся
в исходном поле как исходная латиница и не переносится в новое.

Агрегат/source hashes: data/chromium-ime-001-result.json. 10 required checks и
отдельное наблюдение сохранности оригинала при blur; 6 guard tests. Первые
неудачи были в стенде: RLIMIT_FSIZE2MiB вызвал SIGXFSZ на browser shared-memory
файле; выбиралось невидимое Chromium окно. Лимит файла128MiB и фильтр viewable
window исправили запуск. Не отключали sandbox и не подключали host services.

## Воспроизведение

```sh
bin/prepare-xvfb-lab --fetch  # явно: фиксированный официальный пакет/подпись
bin/prepare-xvfb-lab          # только проверка уже подготовленного cache
mkdir -p build/chromium-ime/addon
(cd build/chromium-ime/addon && qmake6 ../../../tools/fcitx_chromium_lab.pro && make -j2)
bin/test-chromium-ime         # план, без запуска
bin/test-chromium-ime --run   # только закрытый синтетический прогон
python3 -m unittest discover -s tests/chromium_ime
```

Отдельная compile-time variant принимает только program=chromium. Основной Qt
lab addon по-прежнему принимает только rasklad-ime-lab. Эти имена не являются
аутентификацией/семантической защитой и не разрешают host installation.

Код и transport Codex; Claude Opus независимо нашёл проблемы streaming UTF-8,
обработки CDP frame errors, timeout budgets и слабых Undo/focus assertions.
Код исправлен по ревизии. Проверен только фиксированный lab профиль, не detector.
Следующий шаг — присоединить имеющееся локальное evidence core к этому пути,
затем подготовить отдельно согласуемую exact Slack пробу. Никаких новых словарей,
обучения или заявлений о качестве распознавания этот тест не добавляет.
