# Cooperative IME handoff symbolic screen

Claude model draft; Codex ownership fixes/oracle. 3 accepted delivery cases and6 rejection cases;11110 command prefixes include terminal no-ops. Explicit ack is hypothetical, not existing protocol. No runtime/focus/modifier/cleanup proof.

Статус: symbolic_contract_only_not_transport_fix

## Файлы

- `tests/handoff_screen/README.md`
- `tests/handoff_screen/model.py`
- `tests/handoff_screen/test_model.py`
- `tests/handoff_screen/run.py`
- `data/handoff-screen-001-result.json`

## Связи


## Проверки

- `python3 -m unittest discover -s tests/handoff_screen -v`
- `python3 tests/handoff_screen/run.py --run`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
