# Closed Hyprland window mapping

Prepared GPU-only closed Sway→Hyprland mapping; no host Wayland. Actual trial pending separate approval; no field authority.

Статус: prepared_not_executed

## Файлы

- `tools/closed_hyprland_mapping.py`
- `tests/closed_mapping/test_boundary.py`
- `tests/window_provenance/MAPPING.md`
- `docs/CLOSED-HYPRLAND-MAPPING-PLAN.md`
- `data/closed-mapping-preparation-001-result.json`
- `vendor/wayland/sources.json`

## Связи


## Проверки

- `python3 -m unittest discover -s tests/closed_mapping`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
