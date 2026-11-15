# Server

Next server emission/request witness; preflight pending at this map snapshot.

Статус: preflight_pending

## Файлы

- `tools/server_route_witness_guard.py`
- `tests/chromium_ime/server-route-witness.mjs`
- `docs/SERVER-ROUTE-WITNESS-001.md`

## Связи

- [exact](exact.md): optional compositor-only preload
- [current008](current008.md): incoming motivates next boundary observation

## Проверки

- `node --test tests/wayland_ime/server_route_witness_tests.mjs`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
