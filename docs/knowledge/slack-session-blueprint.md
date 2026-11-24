# Steps 1-3 blueprint: real Slack in nested Hyprland with private Fcitx

Steps 1-3 done offline incl. gate test 5/5 with negative control; next: supervisor+helper wiring, nested Hyprland (owner approval).

Статус: in_progress

## Файлы

- `docs/research/SLACK-SESSION-BLUEPRINT.md`
- `docs/research/SLACK-SUPERVISED-SESSION-PLAN.md`
- `tools/slack_bootstrap_probe.py`
- `tools/hyprland_ime_lab.py`
- `tools/hyprland_mapping_probe.py`
- `tools/closed_hyprland_mapping.py`
- `tools/native_ime_clients.py`
- `tools/slack_injector.py`
- `tools/slack_supervisor.py`
- `src/sessionarm.h`
- `tools/fcitx_slack_lab.pro`
- `tests/slack_session/sessionarm_tests.cpp`
- `tools/fcitx_slack_gate_test.pro`
- `tests/slack_session/raskladslackgatetest.conf`
- `data/slack-gate-offline-001-result.json`

## Связи


## Проверки

- `python3 -m unittest discover -s tests/slack_session`
- `python3 -m unittest discover -s tests/slack_probe`
- `python3 -m unittest discover -s tests/closed_mapping`
- `python3 -I tools/wayland_ime_lab.py --run --slack-gate`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
