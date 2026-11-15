# Actual Fcitx drain fixture draft

Lifetime integration syntax/nested-depth controls pass. Raw old-event forwarding rejected on independent review; diagnostic aborts125. Actual Fcitx/runtime ownership remains blocked; next investigate compositor handoff contract.

Статус: lifetime_diagnostic_not_delivery_fix

## Файлы

- `tests/fcitx_drain_server/README.md`
- `tests/fcitx_drain_server/run.py`
- `tests/fcitx_drain_server/server.cpp`
- `tests/fcitx_drain_server/test_runner.py`
- `tools/fcitx_frontend_build.py`
- `tools/fcitx_frontend_lab.pro`
- `data/fcitx-frontend-source-lock.json`
- `patches/fcitx5/README.md`
- `patches/fcitx5/0001-lab-grab-release-without-local-destroy.patch`
- `tests/fcitx_drain_server/check_wrapper.py`
- `tests/fcitx_drain_server/wrapper_ownership.cpp`
- `data/wrapper-ownership-001-result.json`
- `patches/fcitx5/0002-lab-track-listener-depth.patch`
- `patches/fcitx5/0003-lab-bounded-grab-retirement.patch`
- `tools/fcitx_retirement_candidate.py`
- `docs/FCITX-RETIREMENT-REVIEW-001.md`
- `data/retirement-review-001-result.json`

## Связи


## Проверки

- `python3 tests/fcitx_drain_server/test_runner.py`
- `python3 tests/fcitx_drain_server/check_wrapper.py --run`
- `python3 tests/fcitx_drain_server/check_wrapper.py --run --depth`
- `python3 tools/fcitx_retirement_candidate.py --syntax`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
