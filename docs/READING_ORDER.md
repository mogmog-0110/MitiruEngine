# Reading Order

Where to start, depending on who you are and what you're doing.

## Newcomer — human

1. **`README.md`** — what is this engine, `mitiru` CLI quickstart
2. **`docs/GETTING_STARTED.md`** — install `mitiru` CLI, scaffold your first project (`mitiru new` / `mitiru run`)
3. **`docs/SCOPE.md`** — Mode A (Native) vs Mode B (Hybrid)
4. **`docs/HYBRID_RUNTIME.md`** — JS / JSON / C++ split rules (Mode B users)
5. **First tutorial:** `site/content/tutorials/01-hello-novel/` — a 15-minute dialogue scene built from a fresh `mitiru new` scaffold
6. **`docs/ARCHITECTURE.md`** — when you need to dig in

Templates live under `templates/` for reference; day-to-day project creation goes through `mitiru new`, which embeds the templates directly inside the CLI.

## Newcomer — LLM (Claude Code, Copilot CLI, etc.)

1. **`CLAUDE.md`** (project root) — engine ground rules; mandatory
2. **`docs/SCOPE.md`** — determine Mode A or Mode B for this task
3. **`docs/HYBRID_RUNTIME.md §2`** (decision matrix) — where does this feature go (Mode B context)
4. **`.claude/rules/`** — pick the rule file matching the area you're touching
5. **`docs/API_QUICKREF.md`** — most-used APIs (curated chart)
6. **`docs/api/_index.jsonl`** — one JSON line per class / function / enum, grep-friendly. Search this first to locate a symbol.
7. **`docs/api/<module>.md`** — load only the module(s) you need (34 files, ~6 KB to ~200 KB each). **Avoid loading** the monolithic `docs/API_CATALOG.md` (19k lines).

---

## Choosing a narrative VM

See [`docs/NARRATIVE_VMS.md`](NARRATIVE_VMS.md) — decision matrix covering `vn::ScenarioScript` (native C++) vs `mitiru.novel` / `narrative::ScriptRunner` (JSON/JS, Mode B).

## Adding a Mode A feature (C++ only)

1. **`docs/SCOPE.md`** — verify Mode A
2. **`docs/ARCHITECTURE.md`** — locate the right layer in the stack
3. **`include/mitiru/<module>/`** — read existing headers in that module first
4. **`.claude/rules/mitiru-engine.md`** — coding conventions, hot-path discipline, rendering API rules, validation tools
5. **`.claude/rules/test-standards.md`** — Catch2 patterns
6. **`tests/mitiru/Test*.cpp`** — find an existing test to copy

## Adding a Mode B feature (UI screen, JS minigame, new bridge)

1. **`docs/SCOPE.md`** — verify Mode B
2. **`docs/HYBRID_RUNTIME.md §2`** — decide JSON vs JS vs C++ for this feature
3. **`docs/HYBRID_UI_GUIDE.md`** — CEF UI vs native UI choice
4. **`docs/CEF_STATE_BRIDGE.md`** — bridge mechanics
5. **`web/mitiru_runtime/mitiru_*.js`** — read existing JS patterns; `mitiru_save.js` is the canonical bridge wrapper with C++ fallback
6. **`.claude/rules/html-animation.md`**, **`local-design-workflow.md`** — visual conventions and design workflow
7. **`docs/THEME_PACKS.md`** — drop-in CSS theme packs: stable token surface, naming convention, worked example

## Mode B dev workflow (hot-reload)

For day-to-day Mode B work:

1. Run `python tools/mitiru_serve.py --root web --port 8765`
2. Edit `.html` / `.css` / `.js` / `.json` — the browser / CEF view reloads within ~250 ms (SSE-based, no manual refresh)
3. Point CEF at `http://localhost:8765/...` instead of `file://` URLs in dev (i.e. override `mitiru.toml`'s `[cef] start_url` from a dev override or env var)
4. See [`docs/DEV_SERVER.md`](DEV_SERVER.md) for full CLI

## Promoting JS → C++ (hot path or determinism needed)

1. **`docs/HYBRID_RUNTIME.md §3`** — promotion checklist
2. **Preferred path:** [`docs/BRIDGE_CODEGEN.md`](BRIDGE_CODEGEN.md) — write a schema, generate paired C++ + JS
3. **Manual path:** **`include/mitiru/cef/StateStore.hpp`** — the core pattern (`onAction` handlers); **`web/mitiru_runtime/mitiru_save.js`** — fallback pattern (JS keeps working when the C++ handler is absent)

## Investigating a bug in the JS layer

1. Run with debug panel **ON** (see `.claude/rules/definition-of-done.md`)
2. Reproduce with **Real User Path Smoke (RUP-S)** — real `PointerEvent` dispatches, **NOT** internal API calls (`CookingActions.handleDrop(...)` is forbidden as a verification path)
3. Save evidence screenshot to `specs/.../evidence/`
4. Fix the **root cause**; do not patch around symptoms

## Consuming the engine from an existing CMake project

If you're not using `mitiru` CLI (e.g. integrating into a larger monorepo), see the **「CMake から直接消費したい (上級)」** section of `docs/GETTING_STARTED.md` for the `FetchContent` / `find_package` recipe. The CLI just wraps that same flow with a `mitiru.toml` on top.
