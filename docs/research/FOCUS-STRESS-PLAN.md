# Критерии следующей проверки фокуса

## Актуальный результат

Реальный Claude Opus5.5 реализовал модуль и C++ ordering fix; Codex интегрировал,
проверил вместе с независимым агентом и усилил lifetime/focus/cleanup gates.
Роли закреплены в AGENTS.md. Фактический model-id подтверждён CLI metadata;
первый timed-out заход не считался выполненной реализацией.

На закрытом Sway прошли17baseline checks +4focus cases. Overall required[]
содержит18условий:17baseline и один агрегат stress; четыре случая перечислены
отдельно, их не следует складывать как21равнозначных protocol proofs.
Qt/offscreen и GTK/X11 regression прошли после пересборки addon. Граница:
data/focus-stress-reference-001.json, Hyprland с новым кодом ещё не проверен.

### Найденная и исправленная ошибка нашего addon

В независимом reentrant случае hook compositionend ровно1 переключал фокус.
Перед исправлением исходное поле опустошалось, а corrected payload+delimiter
попадал во второе поле: data/focus-stress-before-fix-001.json. Идентичность
узлов/состояния, precondition и cleanup подтверждены; это не оставшийся bypass.

commitAndClear сначала отправлял empty-preedit update и лишь потом commitString.
В [Fcitx5.1.23 frontend](https://github.com/fcitx/fcitx5/blob/5.1.23/src/frontend/waylandim/waylandimserverv2.h)
commitStringDelegate делает protocol commit;
[updatePreeditDelegate](https://github.com/fcitx/fcitx5/blob/5.1.23/src/frontend/waylandim/waylandimserverv2.cpp)
тоже делает отдельный commit. Наш ранний empty update создавал границу для
focus hook до доставки payload.

Исправление Claude: buffer/local panel/ownership готовятся до outward call,
payload отправляется первым, уведомление empty-preedit — после. Pending-release
и bypass bookkeeping перенесены до commit; после callback старый LabState не
трогается. watch().get() и hasFocus() защищают delayed update от мёртвого или
потерявшего фокус IC. Тот же неизменённый оракул после исправления прошёл4/4.
Это не одна атомарная Wayland transaction и не исправление stale serial.

### Исправления тестовой модели, отделённые от ошибки addon

У установленного Chromium155 реальный compositionend в исходном поле имел
isTrusted=false (document-capture target0,count1), тогда как start был trusted.
Это согласуется с [точным Blink source](https://github.com/chromium/chromium/blob/155.0.8059.84/third_party/blink/renderer/core/editing/ime/input_method_controller.cc):
end идёт через scoped dispatch, start через обычный dispatch. Первые строгие
barrier failures не являлись доказательством повреждения текста.

Default модуля остаётся trusted_only. Driver выбирает явный
controlled_chromium_scoped_end только после exact-version + source SHA review
четырёх своих файлов + real VK Space positive control (trusted source start,
один source end с false, точная correction). Policy отказывает при неподтверждённых
условиях; no auto-downgrade после failure. Это provenance закрытой fixture,
не способ различать произвольную JS-фабрикацию и native untrusted end. Исключение
только для end; trusted starts, captured target, identity, balance, text/focus/
hook predicates сохранены. Manifest data/focus-source-review-001.json обновляется
вручную после ревизии, не при каждом запуске. Код Chromium не копировался.

После Escape addon bypass живёт до delimiter; один DOM reset не сбрасывал engine
и загрязнял следующий case. Cleanup после snapshot теперь: abort listeners,
reset(other), real Space, assert только один Space в other и пустые остальные,
нет новой composition, затем reset. При failed cleanup следующие случаи помечаются
DEPENDENT_NOT_RUN, а не независимыми продуктовыми ошибками.

Выполнены19 Node fault-injection tests (это oracle tests, не браузерная модель
доказательства),43Wayland Python guards +8Chromium +10Fcitx guards. Проверяются
утрата слова, пропущенный/двойной/wrong-target hook/end, state replacement,
abort, игнорируемый Escape, privacy ошибок, cleanup payload/контаминация,
policy refusal без provenance/version/control и strict-default поведение.
Команды: `node tests/wayland_ime/focus_oracle_tests.mjs`;
`python3 bin/test-wayland-ime --run --focus-stress --input-root /home/mvn/Work/rasklad`.

Следующий конкретный шаг: отдельно согласованная
`bin/test-hyprland-ime --run-approved --focus-stress --input-root /home/mvn/Work/rasklad`.
Рабочий Slack/IME не подключён. После этого всё ещё нужен protocol-level
cross-surface/stale-serial тест; readyForLiveUse=false. Ниже исходный контракт.

2026-11-14. Роли владельца: Claude реализует модуль сценариев; Codex задаёт
контракт, проверяет код/изоляцию и интегрирует; независимый агент критикует
условия успеха. Неоконченная или timed-out рецензия не считается выполненной.

## Пробел, найденный независимой ревизией

В baseline driver focusOriginalWordPreserved вычисляется и в выполненном
Hyprland run был true, но не входил в required. Это пробел тестового gate,
не свидетельство наблюдавшейся потери. При интеграции сделать его обязательным;
старый отчёт16checks остаётся историческим, новые baseline checks будут17.

Для каждого нового сценария нужно доказать исходную composition именно у prose,
после действия проверять точные значения всех своих полей. Не возвращать текст
в отчёте. Race hook привязывать к исходному элементу и event.target; hookHits
должен быть1, unfired hook не pass. Не очищать значения/журнал между действием
и assertion, не считать sleep или CDP roundtrip подтверждением compositor ack.
Готовый type helper имеет fixed VK ack; это не произвольный replay интерфейс.

## Уменьшенный контракт Claude

1. Pending original → password → Space: старый оригинал сохранён, в password
только Space, в other пусто, password composition не происходила.
2. Pending original → other → prose → Space: ровно original+Space в prose,
другие поля пусты; старое состояние не воскресло для повторной коррекции.
3. Escape → other → Space: original в prose, только Space в other, password пуст.
4. compositionend hook исходного prose при Space фокусирует other: hook ровно1,
фокус other, correction+Space только в prose, другие поля пусты. Это строгий
stress probe: потеря delimiter/слова не становится допустимой альтернативой.

Каждый сценарий имеет precondition, bounded actions и finally cleanup своих
listeners/state. Ошибки только фиксированными кодами. Никаких CDP Input,
синтетических JS input/composition/keyboard events, raw logs, внешних файлов,
зависимостей или сетевых вызовов. Тесты действуют только внутри своей fixture.

## Отдельный уровень: Wayland serial

DOM-поля одного Chromium окна остаются одной Wayland surface. Эти сценарии
не доказывают корректность stale serial и не заменяют будущий protocol-level
тест. Существующий INPUT-LAB-001 проверяет Strict и UncheckedModel на wire,
но это также не runtime проверка установленного Hyprland.

Повторно прочитан точный Hyprland commit efb50993780079460b0cbed1363e2166a2de1d9f:
[InputMethodV2](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/protocols/InputMethodV2.cpp)
не использует serial в прочитанном setCommit;
[relay](https://github.com/hyprwm/Hyprland/blob/efb50993780079460b0cbed1363e2166a2de1d9f/src/managers/input/InputMethodRelay.cpp)
при commit выбирает текущий focused input. Это source concern, не подтверждённая
уязвимость или доказанная ошибка с нашим Fcitx. Будущий точный тест требует
двух своих surfaces, наблюдаемых activate/deactivate/done, сохранённого старого
serial, намеренно позднего commit и positive control со свежим serial. До этого
readyForLiveUse=false, обычный Slack/IME не подключается.
