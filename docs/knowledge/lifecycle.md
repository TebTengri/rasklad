# Wire lifecycle contrast

Private actual libwayland socketpair; expected drop0/1 vsdrain1/1; one-server-grab and async barrier. No Fcitx/compositor/host input.

Статус: closed_wire_pass_not_fcitx_fix

## Файлы

- `tests/wayland_lifecycle/run.py`
- `tests/wayland_lifecycle/wire_client.cpp`
- `tests/wayland_lifecycle/wire_lifecycle.cpp`
- `docs/research/FCITX-DRAIN-EXPERIMENT-PLAN.md`
- `data/wayland-lifecycle-001-result.json`

## Связи

- [fcitx](fcitx.md): candidate retirement primitive; actualfrontend repair not implemented

## Проверки

- `python3 tests/wayland_lifecycle/run.py`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
