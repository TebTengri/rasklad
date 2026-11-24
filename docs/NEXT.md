# rasklad — текущая передача работы

Обновлено 2026-11-24. Полный план — [ACTIVE-PLAN.md](ACTIVE-PLAN.md), роли — [AGENT_SETUP.md](AGENT_SETUP.md).

## Состояние

- Продукт: Fcitx-аддон `src/raskladproduct.cpp` и окно настроек `settings/`.
  Версия в исходниках — 0.5.16; владелец проверил вживую и принял 0.5.13.
- Подключение к рабочему столу — `rasklad-setup` (двойной Alt, значок в панели,
  раскладки Fcitx) и раздел «Подключение к Omarchy» в окне настроек.
- Лабораторные варианты аддона заморожены в `src/fcitxlab.cpp`; исследования —
  [docs/research/](research/).

## Как принимать изменения

1. Ветка `SYS-номер/описание`, версия в `packaging/arch/PKGBUILD` и `settings/settings.pro`.
2. Независимое ревью другой моделью: `codex exec "…" < /dev/null` (без `< /dev/null`
   зависает на чтении stdin). Автор своё изменение не одобряет.
3. Пакет и наборы стенда на нём: `tools/build_arch_package.sh <профиль>`, затем
   `python3 tools/wayland_ime_lab.py --run --product-suite <набор> --packaged --input-root <профиль>`
   для наборов `listed unlisted layout exceptions gestures`. Ожидается 30/3/18/7/7.
4. Слияние только squash: `gh pr merge N --squash --delete-branch`. Владелец разрешил
   сливать самостоятельно после пунктов 2 и 3.

## Дальше

- SYS-256 — лицензии словарей и модели перед публичным выпуском.
- SYS-258 — CI на GitHub; нужно согласие владельца.
- SYS-259 — идеи по продукту; решает владелец.
