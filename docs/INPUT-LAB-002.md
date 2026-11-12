# INPUT-LAB-002: изолированная проверка запуска headless Hyprland (SYS-223)

Дата: 2026-11-12. Задача: SYS-223. Статус: Claude реализовал developer-инструмент; Codex независимо проверил и исправил. 22 mocked test passed; host preflight успешен; config verify в sandbox вернул 0. Единственный smoke: `sandboxStarted=true`, `status=exited`, `exitCode=134`, ≈102 мс, до наблюдения сокета. SYS-223 не завершена, автоматическое исправление выключено.

## Назначение и границы

`tools/nested_compositor_lab.py` — первый шаг из раздела «Следующий шаг» [INPUT-LAB-001](INPUT-LAB-001.md): проверить, можно ли запустить уже установленный Hyprland без GPU, устройств ввода и доступа к сеансу владельца. Гонку serial здесь **не** воспроизводят: клиенты text-input-v3/input-method-v2 не подключаются, текст не передаётся.

Результат `ready` означает только одно: в приватном `XDG_RUNTIME_DIR` появился сокет `wayland-*`. Это не доказательство безопасности sandbox, защиты парольных полей, совместимости приложений или завершения SYS-223. Отрицательный результат (`sandbox_failed`, `exited`, `timeout`) — допустимые данные; ограничения изоляции ради него не ослабляются.

## Режимы и коды выхода

```
python3 tools/nested_compositor_lab.py            # = --check
python3 tools/nested_compositor_lab.py --check
python3 tools/nested_compositor_lab.py --smoke    # только после ревизии Codex
python3 -m unittest discover -s tests/nested_compositor   # или: python3 tests/nested_compositor/test_runner.py
```

| Код | Режим | Статус | Смысл |
| --- | --- | --- | --- |
| 0 | check | `plan_only` | План построен, **ничего не запускалось** (`tested: false`) |
| 2 | оба | `blocked` | Нет/небезопасен исполняемый файл, запуск от root, symlink или чужой каталог в `build/` |
| 10 | smoke | `ready` | Сокет Wayland появился в пределах 10 с |
| 11 | smoke | `timeout` | За 10 с сокета нет |
| 12 | smoke | `exited` | Sandbox поднялся (bwrap `--info-fd` сообщил child-pid), композитор завершился до готовности |
| 13 | smoke | `sandbox_failed` / `launch_failed` | bwrap не создал sandbox или не запустился |
| 14 | smoke | `cleanup_failed` | Reap или удаление private run directory не подтверждены; успешная очистка не заявляется |

`--check` читает только метаданные `lstat` файлов `/usr/bin/bwrap` и `/usr/bin/Hyprland` (обычный файл, не symlink, владелец root, нет записи для группы/всех, нет setuid/setgid, исполняемый) и печатает JSON с argv, где пути хоста заменены метками `<private-runtime-dir>`, `<generated-config>`. Ничего не создаёт. setuid-bwrap отклоняется: у него другая модель прав.

`--smoke` не принимает путей, исполняемых файлов, конфигураций и переменных извне. Отчёт содержит только агрегаты: статус, признак старта sandbox, код возврата при раннем выходе, время. stdout/stderr композитора направлены в `/dev/null`; журналы не сохраняются.

## Изоляция (фиксированный план)

- Корень проекта — из `__file__`. Запись только в `build/nested_compositor` (`build/` в `.gitignore`), каталог 0700. Каталоги проверяются на symlink, владельца и отсутствие записи для группы/всех. Каждый запуск создаёт уникальный private run directory; config создаётся через `O_EXCL | O_NOFOLLOW`, существующий inode не перезаписывается. Тесты проверяют symlink и hardlink, отказ до изменения чужого файла.
- Конфигурация генерируется заново, без `source`, `exec`, `bind`, `env`, плагинов: только `xwayland { enabled = false }`. Пользовательская конфигурация не читается и не включается; передаётся `--config` и монтируется read-only отдельным файлом.
- Приватный runtime — отдельный подкаталог 0700 в уникальном run directory; config лежит рядом, вне writable bind. Runtime монтируется как `/run/lab`; это единственный записываемый bind. Весь run directory удаляется после подтверждённого reap.
- bwrap: `--unshare-all` (в том числе сеть, IPC, PID, UTS), `--die-with-parent`, `--new-session`, `--cap-drop ALL`, `--clearenv`. Read-only `/usr`, символические ссылки Arch `/bin`, `/sbin`, `/lib`, `/lib64`; при наличии `/etc/ld.so.cache` и `/etc/fonts` (не весь `/etc`). Свои `/proc`, минимальный `--dev` (без `/dev/dri`, `/dev/input`, `/dev/uinput`), tmpfs `/tmp`, `/run`, `/home`. Не монтируются `/`, репозиторий целиком, `/sys`, `/run` и `/run/user` хоста, домашний каталог, сокеты Wayland/X11/DBus.
- Окружение в sandbox только: `PATH LANG HOME XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME`, `LIBGL_ALWAYS_SOFTWARE=1`, `LP_NUM_THREADS=1`, `HYPRLAND_NO_RT=1`, `HYPRLAND_NO_CRASHREPORTER=1`. Имя NO_RT подтверждено в `main.cpp`, crash reporter — в `Compositor.cpp`. Сам bwrap получает только `PATH=/usr/bin`.
- Ресурсы (наследуются bwrap и композитором): `nice 10`, `RLIMIT_CPU` 20 с, `RLIMIT_AS` 1 GiB, `RLIMIT_NOFILE` 256, `RLIMIT_CORE` 0, `RLIMIT_FSIZE` 16 MiB. `RLIMIT_NPROC` не используется (общий UID). Ожидание готовности ≤ 10 с, опрос 100 мс.
- Остановка на любом пути: `SIGTERM`, затем `SIGKILL` только собственной группе процессов (pgid = pid bwrap, `start_new_session`), ожидание до 1 с на каждый сигнал, затем reap. Выход bwrap проверяется через `waitid(WNOWAIT)` без reap, поэтому pgid не может быть переиспользован до сигнала. `killall` и поиск процессов по имени не применяются. Обхода через запуск Hyprland без bwrap нет.

## Исходник композитора

Читались [Compositor.cpp](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/Compositor.cpp), [main.cpp](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/main.cpp) и [ConfigManager.cpp](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/config/ConfigManager.cpp) точного commit установленной версии. `initServer` запрашивает Aquamarine: headless — `MANDATORY`, DRM — `IF_AVAILABLE`, Wayland — `FALLBACK`, с комментарием `// TODO: headless only`. Неизвестная переменная выбора backend не используется. `.conf` с явным путём выбирает legacy config manager; `--verify-config` проверяет конфигурацию до запуска compositor loop.

## Ограничения и неопределённости

- Без `/dev/dri` и `/sys` Aquamarine/рендерер может не стартовать. Фактический `exited` говорит только о неудачном запуске с текущими ограничениями, не устанавливает его точную причину. В единственной попытке был SIGABRT без сохранённого core; stdout/stderr по политике runner не сохранялись. Ограничения ради повторного запуска не ослаблялись.
- `RLIMIT_AS` 1 GiB может оказаться мал для llvmpipe; превышение даёт `exited`, не обход лимита.
- Синтаксис конфигурации подтверждён отдельным `--verify-config` внутри того же плана sandbox (return code 0). Работа реального XWayland/клиентов не проверялась.
- `--new-session` выводит процесс внутри sandbox из нашей группы; его завершение обеспечивают `--die-with-parent` и PID namespace (смерть init namespace), а не `killpg`.
- Проверки symlink и создания каталогов не исключают TOCTOU-подмену процессом того же пользователя; это инструмент разработчика, не граница доверия.
- Тесты (`tests/nested_compositor/test_runner.py`) используют mock `Popen`, `killpg`, часов и `lstat`; они проверяют построение плана, отказы и логику остановки, но **не** доказывают реальную изоляцию bwrap и поведение Hyprland.
- Исправления Codex: exclusive config creation, раздельные runtime/config, проверка writable каталога, stale socket/info JSON, отдельный cleanup failure, cleanup после pipe/preexec setup errors, отключение попытки realtime priority. Модульные тесты не запускают реальный bwrap/Hyprland.

## Следующие шаги

Первый допуск и попытка завершены; агрегаты — [project-audit-001.json](results/project-audit-001.json). Следующий шаг — установить причину отказа в отдельной VM/тестовом окружении без доступа к рабочему seat. Синтетические IM/text-input клиенты и гонка serial на настоящем композиторе остаются непроверенными. Готовность текущего рабочего Hyprland не используется как fallback.
