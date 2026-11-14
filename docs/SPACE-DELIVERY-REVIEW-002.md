# Разбор доставки Space: независимая критика

2026-11-14, SYS-245. Исходная контрольная точка 04615d7.
Это ревью публичного кода и синтетических агрегатов, не новый live-тест.

## Установлено

Exact Hyprland: baseline17 проходят, focus3/4; case2 оставляет оригинальное
слово без пробела, other/password пусты. Предыдущая wrong-field ошибка case4
исправлена и подтверждена. Private Sway: instrumented и исходный
uninstrumented сценарии проходят4/4. Причина различия пока неизвестна.

Коллеги отдельно проверили addon, driver/fixture и архитектуру маршрутизации.
fcitx_addon_review: bypass/empty buffer пропускают Space; непустой буфер
потребляет Space и вызывает commit с добавленным пробелом; pending-release
потребляет только keyup. Это не доказывает доставку commit и не исключает
stale lifecycle. См. src/fcitxlab.cpp:363–419.

focus_suite_critic: keys.cpp отправляет каждый press/release с roundtrip
и25ms паузой; ACK выдаётся после команды. driver.mjs type дополнительно
ждёт200ms, reset150ms. Поэтому говорить «немедленный snapshot» неверно.
Но roundtrip одного клиента не подтверждает обработку очередей Fcitx и
Chromium. Это следует из [wl_display.sync](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_display).

## Проверяемые гипотезы

| Гипотеза | Различающее наблюдение | Статус |
|---|---|---|
| stale addon buffer: Space принят, commit не применён | enum ветки addon=commit, buffer nonempty; дальше отсутствие соответствующего input | Не исключена; DOM-only trace недостаточно |
| Space пропущен addon, потерян при forwarding/grab | enum ветки=pass; отсутствие downstream доставки | Не исключена; нужен наблюдаемый разрыв цепочки |
| поздняя DOM доставка после основного snapshot | input/value-class появляется после строгого отказа, до cleanup | Не исключена; существующие200ms снижают убедительность обычной задержки |
| input был, затем поле перезаписано/reset | input показывает original+Space, финал original | Уже подготовленная telemetry может различить |
| событие обработано IME как Process/Unidentified | нет Space-coded keydown, но есть input либо другая key class | Текущий фильтр не видит non-Space code; ноль записей неоднозначен |

input_architecture_challenge также предложил проверить Hyprland retained
pressed-state при переходе IM/direct маршрута. Это source-level кандидат,
не воспроизведённый дефект и не основание менять системный compositor.

## Гипотеза, пониженная после критики

В [Fcitx5.1.23 waylandimserverv2.cpp](https://github.com/fcitx/fcitx5/blob/5.1.23/src/frontend/waylandim/waylandimserverv2.cpp)
есть return при !vkReady_ в sendKeyToVK; reactivation может пересоздавать VK.
Но keymap и key нового grab идут по одной упорядоченной очереди, keymap callback
синхронно выставляет ready. Достижимый interleaving нормального Space с
!vkReady_ не показан. fcitx_addon_review согласился понизить гипотезу:
защитная ветка существует, но для данного отказа не подтверждена.
PersistentVirtualKeyboard=false по умолчанию в exact5.1.23; менять config
как «исправление» необоснованно. Не путать cached master source с tag5.1.23.

## Следующий опыт и критерии

Первым остаётся уже подготовленный optional DOM trace одного Space на exact
Hyprland, без изменения строгого oracle. До75s, прежние ограниченные ресурсы;
новый разовый допуск владельца ещё не получен. Не открывать окно автоматически.

Если trace неоднозначен, следующий developer-only вариант может ждать уже
зарегистрированный реальный input после первоначального отказа и ДО cleanup,
с ограниченным deadline. Исходный r.passed остаётся false независимо от позднего
совпадения. Это позволит отделить late delivery от наблюдаемого отсутствия
input за отведённое время. Такой вариант пока не реализован/не выполнен.

Затем при необходимости — compile-time lab-only enum counters addon:
Space seen, eligibility, buffer empty/nonempty, bypass, decision pass/commit,
lifecycle generation. Никаких текстовых payload/ключей в логах, raw
WAYLAND_DEBUG, полного Controller.DebugInfo, фоновой телеметрии и новых прав.
Отсутствие input до deadline не доказывает вечную потерю или виновность addon.
Не увеличивать sleep, не повторять Space, не ослаблять oracle.

Три независимых Codex reviewer завершили read-only ревью; тесты и live
сеанс они не запускали. Их согласие не заменяет наблюдение.

## Фактическая консультация Claude и отклонённые выводы

Два no-tools запроса к Claude вернули modelUsage=claude-opus-5-5. В первый
Codex ошибочно передал старую task-copy addon до8008ad7; это выявлено diff,
второй запрос содержал актуальный src/fcitxlab.cpp и исправленные ограничения.
Непроверенные ответы не включены в Git как доказательства. Claude отозвал
утверждение о Hyprland stale-serial reject и претензию к уже исправленному
commit ordering. Зафиксировал отсутствие оснований для нового core/config fix.

Codex и независимый critic дополнительно отклонили:

- Рейтинг stale buffer как «наиболее вероятной» причины: наблюдений недостаточно.
- Позднюю запись в other как найденный факт: текущее финальное other пусто.
  Это не исключает кратковременную запись до snapshot, но она не наблюдалась.
- Исключение stale buffer по совпадению original: stale может быть именно
  буфером этого же слова. starts1/ends1 не описывают доставку Space: case2
  явно ждёт compositionend при уходе в other ДО отправки Space.
- requestAnimationFrame×2 или no-op roundtrip как доказательство settled
  IME: независимые очереди Fcitx/Chromium ими не синхронизируются. Позднее
  совпадение было бы свидетельством позднего состояния, но отсутствие такого
  совпадения не исключает произвольную задержку.
- Чтение compositionend.data/общего debug вывода: подготовленный trace
  принципиально не читает event.data и не должен расширяться ради гипотезы.
- Автоматические повторы live trial или reset modifiers: меняют эксперимент
  и не входят в текущий одиночный scope.
- Утверждение о микросекундной стоимости pwrite: она не измерена, запись
  в key path сама способна изменить timing.

Согласованное действие: существующая ограниченная DOM telemetry первой,
не новый слой инструментирования до её результата. Это выбор эксперимента,
а не установленный диагноз или готовность программы к рабочему вводу.

## Новое наблюдение exact trial002

Однократный допуск после ревью получен и использован. Полная ограниченная
трасса пуста (valid/armed/complete, dropped0), исходный отказ повторился.
Детали в data/hyprland-space-telemetry-002-result.json. Это исключает
наблюдавшийся beforeinput/input до cut в этой пробе, но не non-Space-code
keyboard event или late delivery. Происхождение потери пока не установлено.

После результата Claude Opus5.5 подготовил diagnosticv2, Codex интегрировал
и добавил тесты. Независимые reviewer согласились с последовательностью:
не менять core, сначала закрыть эти две неоднозначности. Code-class только
Space/Unidentified/empty/other; Process обычно относится к key, которое
не читается. После исходного FAIL можно ждать существующий input-сигнал
с500ms браузерным deadline, затем отдельно проверять прежний exact oracle.
Первоначальный результат не восстанавливается. Наблюдатель установлен до
Space; snapshot/stop/input/deadline завершают waiter и очищают таймер.

Статический focus_suite_critic блокеров не нашёл; тесты он не запускал.
500ms timer не гарантирует такой же предел wall time при занятом renderer;
сохраняются внешние driver/run deadlines. observedBeforeWait охватывает
всю armed фазу до await, включая возможный разрыв между primary snapshot
и await: этот статус не доказывает позднюю доставку. Wait учитывает input
любого target; его принадлежность и строгие значения проверяются отдельно.
