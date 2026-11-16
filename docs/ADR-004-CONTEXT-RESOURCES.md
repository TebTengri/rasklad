# Контекстные ресурсы: ленивое состояние и безопасное освобождение

Принято для LAB, 2026-11-16; SYS-246.

Сохраняем семь стабильных event subscriptions: клавиши и шесть lifecycle событий.
Они не создают собственных таймеров/polling. Lifecycle нужен для invalidation
Pending/InFlight/Deferred original; Off не означает quiescence. Удалять callable
из текущего или вложенного dispatch без отдельной гарантии SDK небезопасно.
Не вводим defer/timer/state machine ради снятия нескольких dormant callbacks.

Вместо этого сокращаем контекстное владение. Fcitx eagerly вызывает property
factory для каждого контекста, в том числе неподдерживаемого. Factory теперь
создаёт LabProperty с unique_ptr<LabState>; inherited needCopy=false исключает
перенос composition между контекстами. Буфер/контроллер/callback binding создаются
лениво только для явно допустимого fixture program, когда нужен собственный ввод.
Program gate здесь ограничивает выделение памяти, не аутентифицирует приложение.
Release/lifecycle/Off scans используют state(create=false); On пропускает пустые
holders. Первое состояние после On всё равно проходит resume boundary guard.

После Off payload можно удалить только при всех условиях: familyOff актуален,
controller Disabled, buffer пуст, owned preedit нет, pending consumed releases нет.
Сайты cleanup ограничены: после фильтрации поглощённого release, без дальнейшего
использования старого state; после возврата submission/completion, с повторным
получением контекста по lifetime watch. Внутри внешнего callback InFlight
запрещает cleanup. Deferred original остаётся с lifecycle epoch protection.
Holder и подписки продолжают жить. Общий проверенный profile/evidence cache
остаётся загруженным; это не обещание полного сброса RSS или выгрузки словарей.

Пропущенный key-up сохраняет pending bookkeeping. Новый raw press той же клавиши
при Off должен отменить только её старое ожидание, как уже делает enabled path:
иначе последующий raw key-up ошибочно поглощается. Используется noncreating
lookup, takePending конкретного key, затем guarded cleanup. Другие pending keys
сохраняются. Ни timeout, ни polling для поиска пропущенного release не добавлены.

Проверки: 32 пустых holder (16 owned и 16 unsupported) не создают payload при
peek/release/lifecycle/empty Off/On; вложенный release в commit callback не удаляет
InFlight state; после возврата terminal state удаляется; Deferred original
остаётся. Same-key supersession проверяет сквозную новую пару down/up, different
key не снимает старое обязательство. Старые callback binding помечаются dead;
On создаёт новый payload с boundary protection. Все memory cases проходят
ASan+LeakSanitizer; независимое review не нашло блокеров в этом scope.

На текущем ABI sizeof(LabProperty)=16, sizeof(LabState)=256 байт. Это размеры
объектов, не полный объём heap/RSS и не benchmark скорости. Small holder остаётся
для каждого контекста; потерянные releases/Deferred original могут удерживать
payload до соответствующего события или уничтожения контекста.
