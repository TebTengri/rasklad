# Политика приложений

Решение с владельцем, 2026-11-14: настройки содержат отдельное управление
исправлением для каждого поддерживаемого приложения. Список расширяется
постепенно после проверки адаптеров, без автоматического включения новых.

Предлагаемые режимы: выключено, вручную по двойному Super, автоматически.
Режим по умолчанию для нового приложения — выключено. Недоступный режим
не предлагается как работающий: сначала адаптер должен доказать поддержку.
Распознавание намерения и механизм безопасной замены — разные возможности.

Политика является единым входным ограничением для адаптеров. Проверять её
нужно до начала composition/capture и повторно перед заменой текста. При
выключении освобождать состояние и ресурсы подписки; незавершённый текст
сохранять оригинальным безопасным способом конкретного адаптера. Если это
нельзя безопасно отправить исходный текст, не переключать режим посреди
composition молча. Нативная однократная отправка не гарантирует принятие
приложением; локальное завершение и delivery имеют разные результаты.

Включение приложения не разрешает исправлять пароли, команды, URL, code,
mentions и неподтверждённые контексты. Неизвестное поле или неподдерживаемая
замена означает пропуск, без clipboard/key-replay fallback. Смена фокуса
сбрасывает право на замену в предыдущем контексте.

Registry адаптеров должен содержать устойчивый идентификатор приложения,
доступные режимы, проверенные версии/платформы, capabilities и известные
ограничения. Видимый заголовок окна и произвольный app-id сами по себе не
являются надёжной аутентификацией приложения; идентичность требует проверки
доступными средствами платформы. Неуверенное определение — выключено.

Настройки хранятся локально и не содержат введённого текста. Наличие поддержки
в registry отдельно от пользовательского разрешения. Общий выключатель имеет
приоритет. Фоновые адаптеры выключенных приложений не должны расходовать
периодический polling/CPU. Общие системные подписки допускаются только там,
где их необходимость обоснована и стоимость измерена.

Это согласованное направление настроек, не заявление о готовой реализации.
Следующий шаг после transport safety: минимальный registry + policy gate,
затем UI списка приложений. Начинать с одного доказанного приложения.

## Основа SYS-246 — 2026-11-16

`src/applicationpolicy.{h,cpp}` реализует чистое C++17 ядро без I/O, текста
и системных вызовов. Trusted registry отделён от пользовательского разрешения.
Общий Off имеет приоритет; ManualDoubleSuper пока не поддерживается. Захват
разрешён только для явно подтверждённого Prose. Числовой lease связывает версии
registry/permission, context id/epoch и identity binding; перед исправлением
проверка повторяется. Изменение снимка требует новой revision.

Identity — доказательство, предоставляемое адаптером, а не аутентификация,
выполненная ядром. Одного app-id недостаточно. Production-записей нет. LAB
интеграция использует только собственный fixture contract; его метки окружения
не подтверждают личность произвольного приложения. Вне fixture scope профиль,
property и watchers не создаются. ProfileDecision требует явного типа поля.

При уведомлении preedit состояние и принятие события теперь фиксируются до
внешнего callback; после него указатель состояния не используется. Шесть
проверок reset/destruction вызывают реальные helpers; отдельная проверка
подтверждает инертность вне fixture. ASan и LeakSanitizer прошли. Это не
доказательство сохранения оригинала при любом reset существующего LAB.

Результаты: 66 contract assertions; полный bin/test — 602 passed, 0 failed,
1 skipped; закрытый Sway/native Chromium — 18 проверок и 4 сценария фокуса.
Новая host Hyprland проба не выполнялась: trial011 относится к предыдущему
transport checkpoint. Независимое финальное статическое ревью не нашло
блокирующих дефектов; попытка Claude завершилась таймаутом и не считается ревью.

Следующий deployment gate: адаптер с проверяемой идентичностью приложения/поля
и контроллер отключения, сохраняющий оригинал до освобождения ресурсов.
`disableDisposition` сейчас лишь возвращает решение, а не выполняет runtime Off.
UI, хранение настроек, ручной режим и включение обычных приложений не готовы.
Лицензии словарей/данных и стороннего frontend не меняются; собственное ядро MIT.

## Контроллер отключения SYS-246

`compositiondisable.{h,cpp}` — отдельный single-thread C++ контроллер без текста,
I/O и callbacks. По умолчанию Disabled. bind выполняет свежую авторизацию;
Off немедленно запрещает capture/correction, сохраняя старую lease только
для завершения оригинала. takeOriginal повторно проверяет context и выдаёт
единственный ticket, переходя в InFlight до внешнего вызова. Потеря binding
или неизвестный результат переводят в Deferred без повторной попытки.

Подтверждение nativeIdle связано с generation конкретной composition. Во время
InFlight оно лишь запоминается: teardown и enable ждут matching complete.
Запоздалые tickets и generations не закрывают новую composition. Completed
означает подтверждённое нативное завершение, а не просто возврат callback.
NotWritten и Unknown без отдельного native completion сохраняют Deferred.

Адаптер обязан хранить original и ресурсы до подтверждения завершения, передавать
актуальные registry/permission/context, увеличивать permission revision при
изменении настройки и проверять gate на каждом capture/correction. Методы не
аутентифицируют эти данные. Один controller обслуживает одну composition;
копирование запрещено, вызовы сериализованы, tokens не переносятся между
экземплярами. Между takeOriginal и вызовом adapter I/O нельзя обрабатывать
новые события без повторной проверки контекста.

Это проверяемая основа, пока не подключённая к Fcitx/UI: гарантирует максимум
одну разрешённую попытку, но сама не гарантирует доставку/сохранность текста.
Нужен отдельный adapter integration с тестами реентерабельного finish-original.
На ревью приняты два замечания: убрать cached Decision из bind и отложить
teardown после reentrant nativeIdle до окончания внешней операции.

## LAB adapter bridge: отключение во время composition

В LabState подключён CompositionDisable: новая composition связывается свежим
bind; validate использует controller correction; Off останавливает новые
нажатия до изменения буфера. Старые поглощённые releases продолжают дренироваться.
Локальный clearBuffer завершает только обычную Active composition, но никогда
не выдаёт nativeIdle для Pending/InFlight/Deferred/Disabled.

requestLabOff пока не имеет UI/IPC/горячей клавиши: это закрытая plumbing-основа.
Перед отправкой исходного слова проверяются текущие metadata gates и binding;
отказ оставляет deferred без повторной отправки. Внешний void commitString
всегда даёт Unknown. Shared operation переживает уничтожение context/property;
после callback сырые указатели не используются. Копия исходного слова очищается
best-effort. Исходный bounded buffer и preedit не очищаются после Unknown.
Lifecycle discard при pending Off инвалидирует epoch, но сохраняет original;
уничтожение контекста всё ещё стирает buffer и НЕ доказывает сохранение текста.

Проверки actual production helpers через SDK InputContext::commitString,
подменяя только frontend виртуальный метод: шесть сценариев (обычный Unknown,
потеря фокуса, другой epoch, reentrant discard, уничтожение context/property,
отказ eligibility). Проверены repeated Off, no retry, retained original и
запрет teardown. ASan+LSan passed. Прежние шесть preedit callback сценариев и
outside-fixture guard также passed. Сети/daemon/display в memory tests нет.

Закрытый Sway/Chromium regression: 18 checks + 4 focus cases passed. Он проверяет
обычный ввод с подключённым controller, НЕ доставку при Off: runtime trigger
отсутствует. Сборка regression предшествовала извлечению void commit в именованный
helper без изменения обычного key path; финальный helper проверен memory tests.
Новой host Hyprland пробы не было. Попытки делегированной реализации и ревью
оборвались из-за лимита; завершение и проверка выполнены ведущим Codex, свежего
независимого ревью этого bridge пока нет. Перед deployment оно обязательно.

Открытая граница: доказать контракт завершения composition на стороне адаптера
и безопасное освобождение preedit/resources, включая context destruction.
Нельзя публиковать Off как готовый пользовательский режим по этим тестам.

## Подтверждение завершения: приватный receipt contract

Независимое ревью bridge 9e811f1 завершено: блокеров для закрытого, не вызываемого
из UI пути не найдено. Нативная доставка и context destruction остаются открыты.
По закреплённым Fcitx 5.1.23 исходникам (waylandimserverv2.h,
commitStringDelegate) отправляются commitString + commit(serial), без callback
подтверждения вставки. Input-method-v2 commit и text-input-v3 done применяют
пакет состояния; сами по себе они не являются receipt конкретного слова.
Официальный XML локально: /usr/share/wayland-protocols/unstable/text-input/
text-input-unstable-v3.xml; upstream URL:
https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/text-input/text-input-unstable-v3.xml
(онлайн доступ закрыт Anubis; анализ основан на локальном XML и pinned source).

Добавлен private acknowledgeLabOriginal, пока без runtime receipt source. Только
доверенный адаптер, наблюдавший вставку исходного слова в точную composition,
может вызвать его после возврата внешней операции. Требуются Deferred, та же
generation, актуальные eligibility/focus/context epoch/binding и старая lease.
Сначала controller подтверждает nativeIdle, затем стирается original и
подготавливается пустой preedit; внешний callback вызывается последним.
После него сырые указатели не используются. Старые tracked releases сохраняются.
Unknown без receipt по-прежнему не разрешает очистку/retry/teardown. Receipt
в InFlight отклоняется; интеграция должна отложить его обработку до завершения
операции, а не потерять событие.

Шесть новых actual-helper сценариев: matching receipt; stale epoch; focus loss;
wrong generation; failed eligibility; уничтожение context/property из preedit
callback. В каждом commit callback проверяется отказ преждевременного receipt.
Прежние шесть Off сценариев сохранены. Все 12 проходят ASan+LeakSanitizer.
Тестовая фикстура воспроизводит production lease и непустой preedit через SDK,
чтобы проверить действительный callback очистки (SDK подавляет пустой→пустой).
Первоначальные ошибки setup исправлены без ослабления assertions.

Это тест контракта получения receipt, а не готовый способ его получения из
Chromium/Slack. Нужен отдельный тестовый наблюдатель собственного поля и
ограниченный канал подтверждения; он не должен читать пользовательские поля.
Пользовательский Off по-прежнему не включён. Новый host/runtime прогон не нужен
для невызываемого private helper и не выполнялся.

## Актуальное решение: нативная отправка вместо ожидания receipt

Предыдущие разделы описывают последовательные эксперименты. Текущий LAB
requestLabOff использует Result::Submitted и обычный commitAndClear, не Unknown
с бесконечным ожиданием. Нормативное объяснение: ADR-003-OFF-SUBMISSION.md.
Unknown/receipt helpers остаются закрытым контрактом для адаптеров с настоящим
подтверждением. Системный Fcitx путь не генерирует вымышленный receipt.

Shared alive/epoch binding подавляет пустой preedit update после callback,
изменившего lifecycle или уничтожившего property/context. Контроллер освобождает
локальное владение после возврата вызова, не называя результат доставленным.
Новая отдельная --disable-suite проверяет только собственный Sway/Chromium;
обычный addon не содержит F12 trigger. Общий per-app Off/UI по-прежнему не готов.

## Реестр разрешений нескольких приложений

ApplicationPermissions — чистый event-thread store, до 64 trusted adapter
записей. Идентификаторы копируются; один canonical applicationId у нескольких
адаптеров разделяет разрешение. Alias canonicalization — явный контракт
платформенного адаптера, не угадывание по имени окна. По умолчанию все Off.
Enable/revoke имеют независимые application revisions; повторный Off идемпотентен.
RevokeAll сначала проверяет возможность изменения всех записей, затем отзывает
их без внешних callbacks. Переполнение revision навсегда блокирует store;
его достижение не проверялось runtime-тестом.

Permission — копия значений, а не живая capability. Старая enabled-копия
не меняется после revoke. Поэтому на каждой capture/correction boundary и
после callbacks нужно получать новый snapshot. string_view жив до уничтожения
store; это гарантирует только память строки, не свежесть разрешения.

Проверены A/B isolation, общий revoke, старые lease после повторного enable,
неизвестные ids, владение строками, invalid registry и необходимость свежего
snapshot. Assertions без NDEBUG. Store пока не подключён к LAB/production
registry и не меняет разрешения пользователя. Независимое ревью не выявило
блокеров в этом scope; его замечание о stale snapshots принято.

## Общий Off owned fixture family

LAB теперь использует общий ApplicationPermissions на каждом bind/validate.
Оба явно заданных Chromium alias относятся к одному owned client family; это
не production-аутентификация. Off сначала публикует отзыв всех alias, затем
без внешних вызовов ставит requestOff всем совпадающим существующим контекстам.
После этого перебирает lifetime watches, заново получает state и свежие gates
каждого выжившего контекста перед однократной отправкой original. Новые контексты
не получают capture при общем Off. Raw pointers через предыдущий callback
не сохраняются. Reentrant Off не повторяет отправку.

После общего Off новый press возвращается до profile/capability/property lookup;
release ветка остаётся выше, чтобы дренировать старые поглощённые клавиши.
Только test build пропускает повторный synthetic F12 к прежним fixture gates.
Нет polling или timers. Watchers пока остаются зарегистрированы: это не
реализация полного retirement подписок и не измерение idle/long-run нагрузки.

Пять actual SDK memory сценариев: reentrant Off/попытка capture в другом
контексте; уничтожение будущего target; сброс epoch; потеря фокуса; Sensitive
capability. Callback создаёт новый alias context; press не поглощается,
разрешение уже Off. Проверяются отсутствие повторных commits и release drain.
ASan+LSan pass. Reset проверяет invalidation напрямую, не event dispatch.
Прежние 16 Off/receipt/submission и 6 preedit сценариев с inert guard также pass.
Независимый review реализации и fast path не нашёл блокеров в этом LAB scope.

Финальные закрытые прогоны: общий Off 5/5 и normal addon 18+focus4 pass.
Обычный binary не содержит test flag marker, отдельный test addon содержит.
Доступы и лицензии не расширены; UI/production app registry/On/preferences
и снятие watchers не реализованы. Пользовательский режим не включён.
