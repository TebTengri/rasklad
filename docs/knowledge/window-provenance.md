# Native window provenance

Two same-appid native windows on owned Sway; stable IDs agree across observer connections and change on recreation. No Fcitx/field binding.

Статус: closed_native_window_identity_only

## Файлы

- `docs/WINDOW-PROVENANCE-001.md`
- `tests/window_provenance/README.md`
- `tests/window_provenance/run.py`
- `tests/window_provenance/native_windows.cpp`
- `data/window-provenance-001-result.json`

## Связи

- [application-policy](application-policy.md): candidate lifecycle seam, production join unimplemented

## Проверки

- `python3 -m unittest discover -s tests/window_provenance`
- `python3 tests/window_provenance/run.py --run`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
