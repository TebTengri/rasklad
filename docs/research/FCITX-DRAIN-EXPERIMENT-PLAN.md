# Следующий repair experiment: Fcitx frontend, только private prefix

## Доказательства и остающийся разрыв

Exact010 наблюдает старый grab для Space press после deactivate+done и новый
для release, clientcallback0/1. Closed real-libwayland wire test на собственном
toy protocol воспроизводит0/1 с обычным destructor и1/1 при release без
локального destroy до async sync barrier. В обоих случаях server освобождает
старое владение ДО нового grab. Данные:
`data/wayland-lifecycle-001-result.json`, `data/hyprland-grab-epoch-010-result.json`.

Оба режима passed означают успешные negative/positive КОНТРОЛИ. Drop не означает
сохранение ввода. Тест НЕ загружает Fcitx/Hyprland и не является их исправлением.
Он проверяет object lifetime на реальном wire, а не input-method/focus semantics.

## Кандидат и обязательные gates

1. Найти и закрепить реальные generated C++ wrappers Fcitx5.1.23. Сохранить
   целый wrapper/listener userdata, а не подменять wl_proxy_destroy после
   освобождения C++ объекта. Подтвердить destructor и release API.
2. Проверить очередь dispatch. Sync callback должен использовать ту же очередь,
   что retired grab, либо иметь явный queue-drain контракт: wire FIFO сам по
   себе не гарантирует dispatch между разными wl_event_queue.
3. Послать release ровно один раз, без local-destroy flag, затем новый grab и
   async sync. После barrier — только локальный proxy destroy, без второго
   release. Не ждать нового grab, продолжая держать старое SERVER владение.
4. Прототипировать retirement в project-local сборке LGPL frontend, не в
   установленном Fcitx. Сохранить upstream notices; патч под исходной лицензией.
   Существующее собственное MIT ядро/recognizer не менять ради обхода.
5. Решить raw-key forwarding до запуска: retired physical keys не должны
   преобразовываться по старому preedit или применяться к чужому text context.
   На activate Fcitx может пересоздать VK и временно vkReady=false: простое
   сохранение grab само не обеспечивает доставку. Проверить возможность уже
   существующего persistent-VK режима ТОЛЬКО в private config; не считать его
   исправлением по наличию флага. Не вводить clipboard/текстовый replay/sleeps.
6. Парность press/release через старый/новый grab, modifiers/held keys/repeat,
   context purpose/password/focus serial checks остаются критериями. Shutdown,
   seat removal, failed barrier и limit retired objects должны отменять callbacks
   без UAF и без неограниченного накопления. Ошибка должна делать опыт FAIL,
   не подменять доказательство generic fallback.
7. Unit protocol/lifetime controls, настоящая headless Fcitx/Chromium цепочка,
   прежние password/focus/no-duplicate/crash проверки. Отдельно подтвердить,
   какой libwaylandim реально загружен; новый frontend sealed RO в private
   addon directory, без подмены системного файла. Source/ABI pins обязательны.
8. Только после успешного preflight обсуждать одну exact nested Hyprland пробу
   с новым явным допуском. Scope010 использован; working IME/Slack/settings не
   трогаются. Native Slack подготовка не объявляется завершённой.

Ни raw passthrough текущему seat после произвольной задержки, ни compositor
activation gate не принимаются автоматически: первый может иметь неверный
focus, второй меняет контракт persistent IMEs. Если корректный frontend fix
требует кооперации compositor, готовить нейтральный upstream interoperability
report/draft вместо скрытого app workaround. Внешние issue/messages не отправлять
без отдельного указания владельца.

## Авторство и проверка wire control

Первый combined C++ запрос Claude превысил240s, результата нет. Меньший запрос
Claude Opus5.5 дал client source. Codex написал server/launcher, проверил client,
добавил allocation/listener error checks, явный display, process cleanup и
согласовал состояния. Отдельный helper независимо проверил шесть файлов,
блокеров не нашёл; runtime он повторно не запускал.

Сервер использует только private socketpair, fork и публичный libwayland,
без compositor/socket-by-name/window/input/GPU/network/profile. Begin ставит
retire затем press в один connection; release освобождает resource до второго
get_grab, который ставит release. Client userdata живёт до конца dispatch.
Server5s deadline, private child PDEATHSIG, launcher8s timeout и sealed binary.
Default launcher инертен. Обобщение ограничено одним event queue/two-object
контролем; перенос в реальные Fcitx wrappers ещё не реализован.
