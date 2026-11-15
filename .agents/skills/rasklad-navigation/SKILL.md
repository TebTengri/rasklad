---
name: rasklad-navigation
description: Find rasklad components, source files, relevant tests and current input-lab evidence using the local project map. Use when resuming or locating code in this repository; not for unrelated projects or granting live probe permissions.
---

Use the repository's compact navigation instead of loading chat history or all experiments.

- `bin/project-map check` checks source and generated-note freshness. A stale map is a navigation hint, not current evidence; inspect changed files before updating it.
- `bin/project-map find <topic>` returns up to eight matching components and their paths/test commands. Examples: `fcitx`, `dictionary`, `router`, `hyprland`.
- `bin/project-map file tools/hyprland_ime_lab.py` adds local references and Python definition locations. Tests listed in output have NOT been executed by this command.
- Read only the needed source and nearest evidence/decision doc. If a topic is absent, use targeted `rg`; the graph is not an exhaustive call graph or Qt/QML runtime analysis.
- For current work read `docs/CONTINUATION-PLAN.md`; scoped live-trial approvals do not carry over merely because an earlier trial is in the map.

Architecture overview: `docs/PROJECT-MAP.md`. Semantic nodes/relationships: `docs/project-map.json`. File index: `docs/project-graph.json` (query it through the CLI; do not load it wholesale).

When code paths/status change, update the semantic map if needed, then run `bin/project-map build` and `check`. Build creates only project JSON and Markdown notes; it does not execute tests, install packages, contact services or change the desktop. Derived notes in `docs/knowledge/` can be opened as a separate Obsidian vault. Edit their source map, not generated note copies.

Graphify is optional and not installed by this skill. Do not invoke its installer, auto-update, cloud extraction or global hooks as a navigation side effect. Scope and evaluated version: `docs/NAVIGATION-TOOLS.md`.
