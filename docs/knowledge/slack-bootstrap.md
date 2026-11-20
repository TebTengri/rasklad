# Real-profile Slack bootstrap feasibility (step 0)

Dry runs PASS: Chromium control maps window 0.44s; empty-profile offline Slack registry only. Live real-profile run needs one-shot owner approval.

Статус: prepared_dry_run_pass_live_not_run

## Файлы

- `docs/SLACK-BOOTSTRAP-PROBE.md`
- `tools/slack_bootstrap_probe.py`
- `tools/slack_https_bridge.py`
- `tests/slack_probe/test_bootstrap_probe.py`
- `data/slack-bootstrap-dryrun-001-result.json`
- `docs/SLACK-SUPERVISED-SESSION-PLAN.md`

## Связи


## Проверки

- `python3 -m unittest discover -s tests/slack_probe`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
