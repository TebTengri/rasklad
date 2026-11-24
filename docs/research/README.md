# Исследования rasklad

Здесь отчёты, ADR, методики и лабораторные документы, из которых вырос продукт. Они
сохранены как история и доказательства. Они не описывают текущее поведение
аддона: оно описано в [README](../../README.md), [SYS-246](../SYS-246-PRODUCT-ADDON.md),
[SYS-226](../SYS-226-AUTO-CORRECTION.md) и [SYS-254](../SYS-254-SETTINGS-DESIGN.md).

- **Детектор и маршрутизация.** Опыты 001–006 (`EXPERIMENT-*`): отчёт, предрегистрация
  (`-PREREG`) и методика (`-METHOD`). Решение о схеме —
  [ADR-001-DETECTOR-ROUTING](ADR-001-DETECTOR-ROUTING.md).
- **Механизм ввода.** [OPEN_SOURCE_REVIEW](OPEN_SOURCE_REVIEW.md),
  [ADR-001-input-boundary](ADR-001-input-boundary.md), стенды `INPUT-LAB-*`,
  `WAYLAND-IME-001`, `HYPRLAND-*`, `FCITX-*`.
- **Приложения.** Slack (`SLACK-*`), Chromium (`CHROMIUM-IME-*`), браузерные
  адаптеры (`BROWSER-*`, `PROSEMIRROR-*`).
- **Ревью и аудиты.** `CODE-REVIEW-*`, `PROJECT-AUDIT-001`, `MULTI-MODEL-REVIEW-002`,
  `PARALLEL-REVIEW-001`, `ECC-REVIEW-004`.
- **Старое окно предпросмотра** (SYS-239, до аддона): `MVP-001-REVIEW`,
  `MVP-THREAT-MODEL`, `UI-DIRECTION`.
- **Передача работы 2026-11-21** (лабораторная фаза): [HANDOFF-2026-11-21](HANDOFF-2026-11-21.md).

Лабораторные варианты аддона заморожены в `src/fcitxlab.cpp`; результаты прогонов
лежат в `data/*-result.json`.
