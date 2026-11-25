# rasklad — текущая передача работы

Обновлено 2026-11-26. Полный план — [ACTIVE-PLAN.md](ACTIVE-PLAN.md), роли — [AGENT_SETUP.md](AGENT_SETUP.md).

## Состояние

- Продукт: Fcitx-аддон `src/raskladproduct.cpp` и окно настроек `settings/`.
  Версия в исходниках — 0.5.31; владелец проверил вживую и принял 0.5.13.
  После установки Rasklad выключен, пока его не включат в окне настроек.
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
   для наборов `listed unlisted off layout exceptions gestures focus`; в каждом отчёте
   `passed: true`.
4. Слияние только squash: `gh pr merge N --squash --delete-branch`. Владелец разрешил
   сливать самостоятельно после пунктов 2 и 3.

## Дальше

- Проверка владельцем вживую версии 0.5.31.
- SYS-256 — ревью распространения готово и сверено с 0.5.31; ждёт решения владельца.
- SYS-258 (CI), SYS-259 (идеи по продукту) — решает владелец.
- Обновление словарей — `tools/update_dictionaries.py`, когда в LibreOffice изменятся
  en_US или ru_RU (скачивание — каждый раз с разрешения владельца).
