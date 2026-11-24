# Рассинхронизация grab: результат010 и границы исправления

## Наблюдение

Exact010, пять коррелированных повторов word→other→prose→Space:

| Уровень | Press | Release |
|---|---:|---:|
| Server emission attempt | 1 | 1 |
| Grab marker до последнего deactivate+done sent | 1 | 0 |
| Grab marker текущего поколения | 0 | 1 |
| Client grab callback / Core / addon | 0 | 1 |

Unknown0, peakLive1, два создания/уничтожения grab за интервал. Все пять
строгих oracle FAIL с сохранённым исходным словом без delimiter. Остальные
3focus, baseline17, cleanup и compositor liveness PASS. Положительный Sway
контроль:5PASS, old0/0,current1/1. Данные:
`data/hyprland-grab-epoch-010-result.json`. Разовый допуск010 использован.

Это наблюдаемая server-side split-generation пара. Marker рождения — первый
keymap, done — событие отправлено, не client ack. Logger предшествует
send/queue. Client discard напрямую не измерялся. Следовательно, это сильная
локализация interoperability race, не доказательство единственного виновника
и не готовое исправление.

## Сопоставление с точными источниками

Fcitx5.1.23 синхронно сбрасывает keyboardGrab_ на pendingDeactivate/done и
заменяет его на pendingActivate. Hyprland efb5099 сохраняет grabs после
deactivate, hasGrab проверяет их наличие, sendKey обходит живые grabs.
Wayland1.26 пропускает listener для локально уничтоженного proxy.
Сочетание совместимо с наблюдением010. Public source pins:
`data/ime-source-review-010.json`; исходники прочитаны в ignored build/,
установленные библиотеки не изменены и чужие тела не перенесены в продукт.
Fcitx source LGPL-2.1-or-later; будущий upstream patch должен сохранять её
условия, а не автоматически получать лицензию собственного MIT ядра.

## Что не делаем

- Не считаем deactivate неявным release: протокол принимает inactive requests,
  grab — отдельное владение до release. Безусловный compositor gate способен
  изменить поведение постоянных IME grabs.
- Не задерживаем только wl_proxy_destroy под уничтоженным C++ listener userdata.
  Такой raw interposer может создать use-after-free.
- Не удерживаем server grab до появления нового: это конфликтует с проверяемым
  условием единственного grab. Клиентский объект и серверное владение различны.
- Не заменяем физическое событие перепечаткой текста, sleep/retry или clipboard.
- Не отправляем upstream issue и не меняем рабочий WM/Fcitx без разрешения.

## Следующий закрытый механизм

Собственный toy protocol через реальный libwayland и приватный socketpair:
server ставит retire затем press в один wire stream. Client либо использует
обычный destructor request (локальный proxy сразу исчезает), либо отправляет
release без локального destroy, сохраняет объект/listener и уничтожает proxy
после асинхронного wl_display.sync. В обоих случаях server освобождает старый
grab до создания нового и отправляет release новому. Проверяется контраст
callback0/1 против1/1, server1/1, barrier и one-live invariant.

Это object-lifetime regression, НЕ реализация IME/Fcitx. Даже успешный drain
не докажет безопасность обработки retired keys в новом text context или
готовность патча Fcitx. Потребуются настоящий frontend prototype и прежние
password/focus/key-pair/no-duplicate проверки. Пользовательский сеанс этому
toy test не нужен. Arbitrary sleeps и nested synchronous roundtrip в retire
callback не используются. Собственные source/XML MIT; libwayland unchanged.

Одно короткое независимое ревью Claude Opus5.5 рекомендует проверять draining,
согласуется с диагностикой и отмечает необходимость password/focus controls.
Codex уточнил порядок: release серверного владения отдельно от локального
destroy и без ожидания нового grab при удержанном старом. Модельное согласие
не заменяет wire experiment или runtime proof.

## Closed wire contrast выполнен

Оба контрольных режима прошли на реальных pinned libwayland client/server
через private socketpair: server1/1, drop callback0/1, drain callback1/1.
One-live invariant и async barrier выполнены, protocolError0. Без Fcitx,
compositor, реального ввода или пользовательского сеанса. Отчёт:
`data/wayland-lifecycle-001-result.json`. Следующий реальный frontend experiment
и его gates: `docs/research/FCITX-DRAIN-EXPERIMENT-PLAN.md`. Это ещё не Fcitx fix.
