# Steps 1-3 blueprint: real Slack in nested Hyprland with private Fcitx

M1 IME dry run done; step 2 supervisor done; step 3 addon gate variant built (arm pipe, program slack, commit text); next: nested composition wiring.

Статус: in_progress

## Файлы

- `docs/SLACK-SESSION-BLUEPRINT.md`
- `docs/SLACK-SUPERVISED-SESSION-PLAN.md`
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

## Связи


## Проверки

- `python3 -m unittest discover -s tests/slack_session`
- `python3 -m unittest discover -s tests/slack_probe`
- `python3 -m unittest discover -s tests/closed_mapping`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
