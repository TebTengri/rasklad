# Wayland object ID scope

Real private-socket core surface object IDs collide across connections; no native window/Fcitx proof.

Статус: closed_wire_namespace_counterexample

## Файлы

- `tests/wayland_identity/README.md`
- `tests/wayland_identity/run.py`
- `tests/wayland_identity/object_id_scope.cpp`
- `data/wayland-object-id-scope-001-result.json`

## Связи


## Проверки

- `python3 -m unittest discover -s tests/wayland_identity`
- `python3 tests/wayland_identity/run.py --run`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
