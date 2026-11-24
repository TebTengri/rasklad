# Aquamarine feedback readiness and parallel review

Actual noGPU baseline distinguishes immediate/deferred feedback; Claude patch prepared,8confinement tests pass. Grok/Gemini targeted review complete; patched build/runtime still pending.

Статус: prepared_patch_unbuilt

## Файлы

- `tools/aquamarine_readiness_patch.py`
- `tools/hyprland_feedback_parent.py`
- `tests/aquamarine_readiness/test_patch_confinement.py`
- `docs/research/AQUAMARINE-READINESS-EXPERIMENT.md`
- `docs/research/PARALLEL-REVIEW-001.md`
- `data/parallel-review-001-result.json`
- `data/hyprland-feedback-parent-001-result.json`

## Связи


## Проверки

- `python3 -I -m unittest discover -s tests/aquamarine_readiness -p 'test_*.py'`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
