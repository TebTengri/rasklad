# Избирательный серверный свидетель Space

## Вопрос опыта

Проба008 обнаружила отсутствие Space press уже на входе перехваченного grab
callback Fcitx во всех пяти коррелированных повторах. Не установлено, отправлял
ли compositor событие и какому ресурсу. Следующий developer-only вариант
наблюдает публичные server request/event boundaries, не меняет маршрутизацию.

## Ограниченный контракт

Отдельный shim только в дочернем приватном compositor перехватывает публичный
wl_display_create, вызывает оригинал и подключает wl_display_add_protocol_logger.
Установленные Hyprland/Wayland не патчатся. Parent Wayland FD остаётся только
у compositor, headless reference не получает host display/GPU. LD_PRELOAD не
попадает в runner/Fcitx/Chromium и удаляется из окружения потомков compositor.
Новый флаг требует всю цепочку GRAB→CORE→querying addon→focus+telemetry.

Public logger получает все сообщения: это фильтр кода, не ограниченная
подписка на уровне capabilities. До доступа к arguments проверяются class,
message name, opcode, signature и argc. Для трёх key-сообщений читаются только
код и, только при Space57, state. Остальные payload не читаются. Метаданные
IM deactivate/done/grab release/IM request считаются без аргументов.
Нет текста, serial, timestamps, ID ресурсов, произвольных keycodes или общего
протокольного лога. callback не делает I/O, allocation и форматирование строк.

Входной VK счётчик включает ВСЕ private virtual keyboards, в том числе
Fcitx forwarding. Он не идентифицирует helper. Выходные wl_keyboard события
могут дублироваться для нескольких ресурсов. IM lifecycle totals не являются
идентификатором поколения grab и не доказывают destroyed-proxy cause.

Фиксированный80-byte файл создаётся только в private tmpfs /run/lab, O_EXCL,
O_NOFOLLOW,0600; callback обновляет shared mapping с generation seqlock.
Reader требует два одинаковых снимка, even generation, ready/coverage,
ровно один display/logger, positive VK/grab/keyboard calibration, отсутствие
unknown-state, saturation и counter-reset. Каждый case2 имеет свежий reader;
оригинальный строгий oracle не восстанавливается поздним snapshot.

## Интерпретация

- Server grab emission attempt есть, callback press отсутствует: разрыв после
  server emission attempt и до callback, не доказательство socket delivery.
- VK request наблюдался, grab emission отсутствует: исследовать server routing,
  обычный keyboard и источник VK. Не объявлять request исключительно helper.
- Callback есть, Core нет: граница callback→Core (не наблюдалась в008).

Public API документирует отложенную отправку событий клиенту:
https://wayland.freedesktop.org/docs/html/apc.html .
Tag1.26.0 в mirror показывает logger перед server dispatch для request и перед
send/queue для event; canonical GitLab в этой сессии недоступен, provenance
mirror не скрывается:
https://github.com/wayland-mirror/wayland/blob/1.26.0/src/wayland-server.c .
Ни request logger, ни event logger сами не доказывают завершённую обработку
compositor handler или доставку Fcitx. Protocol definitions закреплены ранее
в MIT XML проекта; сгенерированные файлы остаются в build/. Чужие реализации
не копируются. Runtime зависимости сохраняют свои лицензии.

## Статус

Подготовка/ревью и закрытые проверки. Нового разрешения на parent Wayland/GPU
после008 нет. До успешного ABI/unit/headless preflight host не запускать.
Claude поручена реализация shim, Codex интегрирует и проверяет. Ошибку API/DNS
или timeout не считать завершённой реализацией. Фактические результаты и
исправления ревью будут зафиксированы после получения кода.

## Завершённый preflight

Claude CLI003 вернул C++ proposal (запрошен Opus5.5, usage содержит5.5 и4.8).
Первый вызов: DNS/API error, второй:240s timeout; они не считаются кодом.
Codex исправил near-wrap, seqcst publication, arch guard, ABI pointer cast,
unsetenv/null readiness и интегрировал runner/readers. Независимый reviewer
поймал неверный manager opcode в исходном задании Codex: XML определяет
get_input_method первым request, opcode0. Исправлены shim и fake fixture.
Финальный unit получает manager opcode/signature независимо из pinned XML;
первый зелёный fake unit сам по себе эту ошибку не нашёл — подтверждение
необходимости независимого протокольного источника.

88 Python guards,22 reader contracts, закрытая filesystem проверка reader
(mode/size/symlink/FIFO/missing) и22 C++ callback/admission controls проходят.
Настоящая headless Sway цепочка baseline17/focus8 PASS. Все5 target rows:
ALL-private VK Space2/2, grab emission1/1, обычный keyboard emission1/1,
unknown0; addon/Core/grab callback7/7. Это положительная калибровка привязки
logger, не диагноз Hyprland. Отчёт/source pins:
`data/server-route-witness-preflight-001.json`.

Следующий exact запуск ещё не разрешён: один nested Hyprland trial до75s,
те же parent Wayland/renderD128, fixedfile metadata witness,5 повторов.
Нет рабочего Slack/IME/settings изменений. Scope008 уже использован.

## Exact проба009

Одно отдельное разрешение использовано. Все5 target rows FAIL: ALL-private
VKSpace1/2, grab emission attempt1/1, ordinary keyboard0/0, unknown0;
client grab/Core/addonSpace0/1 и all6/7. Coverage полная; остальные3focus,
baseline17, cleanup и compositor liveness PASS. Data:
`data/hyprland-server-route-009-result.json`.

Это разрыв после server emission attempt и до зарегистрированного callback.
Не утверждаем socket transmission: публичный logger вызывается перед send/queue.
Не доказано, что press/release относятся одному поколению grab. Следующая
кандидатная проверка — ограниченная metadata-классификация old-generation после
deactivate/done; повтор нынешнего опыта без новой границы малоинформативен.

Независимое короткое ревью Claude Opus5.5 после009 подтвердило узкую
интерпретацию. Следующая epoch-классификация покажет только server temporal
relation, не client discard. Приняты замечания: различать deactivate-sent и
done-sent, ограничить/отмечать несколько живых grabs, защититься от pointer
reuse и не трактовать эпоху как подтверждение уничтожения proxy у клиента.
Предложенные отдельные server/client биты без общей причинной привязки сами
не доказывают порядок между процессами; не использовать их как готовый диагноз.
Новых host запусков это ревью не разрешает.
