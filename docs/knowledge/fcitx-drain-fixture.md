# Actual Fcitx drain fixture draft

Retained Fcitx candidate samepeer0/1->1/1, knowncleanupfixed. ClosedSway18+focus4 and one owner-approved exactnestedHyprland01118+focus4 PASS. Approvalconsumed; noSlack/regular-session/releaseproof.

Статус: one_exact_nested_hyprland_preflight_pass_not_live_release

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
- `docs/ACTUAL-FCITX-HANDOFF-001.md`
- `data/actual-fcitx-handoff-001-result.json`
- `data/fcitx-frontend-baseline-build-001-result.json`
- `patches/fcitx5/keep-grab-lab.patch`
- `patches/fcitx5/keep-grab-cancellation.patch`
- `patches/fcitx5/keep-grab-lock.json`
- `docs/FCITX-KEEP-GRAB-001.md`
- `data/fcitx-keep-grab-001-result.json`
- `data/native-private-frontend-001-result.json`
- `tests/wayland_ime/test_private_frontend.py`
- `tests/wayland_ime/test_hyprland_private_frontend.py`
- `data/fcitx-keep-grab-modifier-001-result.json`
- `data/hyprland-keep-grab-011-result.json`

## Связи


## Проверки

- `python3 tests/fcitx_drain_server/test_runner.py`
- `python3 tests/fcitx_drain_server/check_wrapper.py --run`
- `python3 tests/fcitx_drain_server/check_wrapper.py --run --depth`
- `python3 tools/fcitx_retirement_candidate.py --syntax`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
