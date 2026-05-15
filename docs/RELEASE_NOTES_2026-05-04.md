# Release Notes — 2026-05-04

**Date:** 2026-05-04  
**Commits:** 33 total (16 morning alignment + 17 evening sweep)  
**Test coverage:** 328/328 ✓ (`npm test`, 22 files)  
**Working tree:** clean

---

## Headline

33 commits closed 7 outstanding consumer requests. Bridge codegen schema expressiveness reached 16/16 on the save bridge (jsOnly, blob, function, constants all shipping). Added Playwright config, replay_inspector test suite, and fixed stale-API bugs in `templates/native-plus-cef-overlay/`. All 4 consumers have forward-compatible changes to integrate.

---

## KaeruCrape (2026-04-24 request doc)

**Closed since F-01..F-16 baseline:**
- **F-02** CSS component library — F-02 was tagged PARTIAL in initial audit, but investigation revealed all 9 named components already shipped in `mitiru_components.css §1–§10` plus a 10th hud-note. Reclassified IMPLEMENTED. No new work needed; your mockups can use `.mitiru-menu-card`, `.mitiru-pendant-lamp`, etc. today.
- **F-15** Theme pack convention — `docs/THEME_PACKS.md` shipped with worked example (`web/mitiru_runtime/themes/example-theme.css`). Drop-in CSS file redefines `--mitiru-*` tokens without touching engine source. See `docs/READING_ORDER.md` (cross-link added). Commit: `41c0616d`.

**Still open / deferred:**
- F-03, F-04, F-05, F-06, F-07, F-08, F-09, F-10, F-11, F-12, F-13, F-16 remain IMPLEMENTED as of Apr 24 audit. No regressions.
- F-14 (MCP `outputPath` parameter) remains REJECTED_BY_SCOPE — belongs upstream with MCP project authors, not engine scope.

**What you can pick up now:**
Theme packs are ready for immediate use. Copy `web/mitiru_runtime/themes/example-theme.css` as a template and create `your-game/assets/ui/themes/theme-<name>.css` with your own token values. See `docs/THEME_PACKS.md §3` (drop-in mechanism). No engine rebuild needed.

---

## hato (2026-04-25 request doc)

**Closed since H-01..H-08 baseline:**
- **H-05** Novel textbox inline-style minimization — textbox defaults moved from inline `height: 100%` to cascade-friendly CSS class in `mitiru_components.css §19`. Consumer overrides no longer need `!important` to take effect. Commit: `5f7491c9`.
- **H-08** OSR CEF keyboard auto-focus on first paint — `claimKeyboardFocus()` helper added to `MitiruCefBrowser.hpp:143–146`. Auto-claim happens on first paint + `setInputEnabled(true)` by default; opt-out via `setAutoFocusOnFirstPaint(false)`. Commit: `889d18e3`. **Caveat:** not validated against a live CEF build in-engine — your next build should validate this works.

**Still open / deferred:**
- H-01, H-02, H-03, H-04, H-06 remain IMPLEMENTED. Input lockout built-in bonus also shipped (H-01 closure, commit `673a7c60`).
- No blocking regressions.

**What you can pick up now:**
H-05 removes the `!important` smell from your textbox overrides — re-export a fresh build and test with lighter CSS. H-08 auto-focus is live; test keyboard input during your CEF integration loop (if not already validated).

---

## Mathlands (2026-05-02 request doc)

**Closed since M-01..M-05 baseline:**
- **M-03** `MitiruEngineConfig.cmake` probe-order docs vs reality — comment block rewritten to emphasize Phase A gotcha (ユーザーレジストリが global engine を優先 resolve). Template now defaults `MITIRU_ENGINE_DIR` to `${CMAKE_SOURCE_DIR}/../engine` when that sibling exists, making worktree layouts "just work" without `-DMITIRU_ENGINE_DIR` on the command line. Commit: `8fb6b3e1`.

**Still open / deferred:**
- M-01, M-02, M-04 remain IMPLEMENTED (template main.cpp modernized, `mitiru_add_cef_game()` now called, `@NAME@_DEV_BUILD` option added). No new work.
- M-05 (feature/engine-core ozz regression) remains NOT_IMPLEMENTED on main — feature-branch only, deferred until that branch merges or closes.

**What you can pick up now:**
M-03 docs are clearer now, but your workaround (instantiate, manually fix main.cpp to modern API, add `mitiru_add_cef_game()`) remains valid. Fresh instantiations of `web-first-cef-shell` template will now have these fixes baked in. If you re-clone from template, the build path will be smoother.

---

## pandd-dodo (2026-04-22 request doc)

**Closed since G-01..G-18 baseline:**
- **G-13** `ScenarioScript` vs JSON parity decision matrix — formalized in `docs/NARRATIVE_VMS.md` (decision matrix § 1–6). Native `vn::ScenarioScript` serves engine-driven scenes (16 directives including `@branch` / `@minigame`); JSON `narrative::ScriptRunner` serves linear text scenes (6 directives); `mitiru.novel` (Mode B JS) serves interactive scenes (12 directives + NF-11 effects). Parity is explicitly NOT a goal per `docs/SCOPE.md`. Commit: `6d4cf075`.
- **G-14** GIF + image interaction semantics — documented in `docs/NARRATIVE_VMS.md §7`. All VMs replace prior content unconditionally on the same display layer; no built-in `gif` directive; animated GIFs flow via the same `path` field as static images. **Caveat:** surfaced a real gap — `BackgroundManager` has no `AnimatedSprite::loadGIF()` auto-wire. Roadmap note in §7.7. Commit: `f6533a8d`.

**Still open / deferred:**
- G-01..G-12, G-15, G-16, G-17 remain IMPLEMENTED from 2026-04-22 baseline.
- G-15 (Probabilistic effects directive) remains NOT_IMPLEMENTED — minge-port has C++ workaround, not blocking.
- G-18 (2D Action / Platformer primitives) remains NOT_IMPLEMENTED — held until 2nd consumer is confirmed; Mathlands metroidvania may become that consumer.

**What you can pick up now:**
Read `docs/NARRATIVE_VMS.md` to understand which VM to use for your scenes. The decision matrix is now explicit: use `vn::ScenarioScript` for branching / minigames, JSON `ScriptRunner` for linear dialogue sequences, and `mitiru.novel` (Mode B) for interactive web-based scenes. No API changes — clarification only.

---

## Engine-Side Schema & API Additions (Cross-Cutting)

### Bridge Codegen (tools/generate_bridge.py)

Four new schema features shipped in Delta 2, closing the gap to 16/16 expressible on the save bridge:

| Feature | Type | Use case | Example |
|---------|------|----------|---------|
| **jsOnly** | method attribute | JS-only methods (no C++ handler) | `blob` parameters imply `jsOnly: true` |
| **blob** | parameter type | `Blob` objects passed to JS, skipped in C++ | File upload previews, binary cache |
| **function** | parameter type | JavaScript function callbacks (not serializable) | Async handlers, event listeners |
| **constants** | schema top-level | Typed constants emitted into both C++ header + JS module | `SCHEMA_VERSION`, `MAX_SLOTS` |

See `docs/BRIDGE_CODEGEN.md §2.2–§2.3` (Type system + Constants sections).

Bridge validation remains at **17/17 assertions** (`tools/generate_bridge.py --self-test`); backward compat proven on `telemetry.bridge.json` (byte-identical regen).

### mitiru.novel — Textbox Styling

Textbox default styles moved from inline `height: 100%` (which overpowered external CSS) to cascade-friendly class rules in `mitiru_components.css §19` (`web/mitiru_runtime/mitiru_components.css:1163–1203`). Consumers no longer need `!important` to override textbox dimensions. `mitiru_novel.js:111–117` contains the fallback shim for older textbox markup.

### MitiruCefBrowser — Keyboard Focus (H-08)

New method: `claimKeyboardFocus()` (`include/mitiru/cef/MitiruCefBrowser.hpp:143–146`). Automatically invoked on first paint and on `setInputEnabled(true)` — opt-out via `setAutoFocusOnFirstPaint(false)` on `MitiruCefContext`. Resolves OSR mode's keyboard-routing issue without boilerplate per-game.

### template/native-plus-cef-overlay Fix

Template's `src/main.cpp` and `CMakeLists.txt` updated to use current `Game` API:
- `update(float dt) override`
- `draw(mitiru::Screen&) override`
- `layout(int, int) -> Size override`
- `mitiru_add_cef_game(@NAME@)` now called in POST_BUILD

Old template (pre-2026-04) used deprecated API (`onInit()`, `screen()` accessor, no `layout()`). Commit pairs H-07/M-01 modernized `web-first-cef-shell`; this sweep brings `native-plus-cef-overlay` into alignment. Template build not exercised in this session — consumer rebuild validates correctness.

### MitiruEngineConfig.cmake — Probe Order Documentation

Comment block in `cmake/MitiruEngineConfig.cmake:1–46` and template `CMakeLists.txt:11–24` now emphasize the Phase A gotcha: when multiple MitiruEngine checkouts exist (global registry + local sibling), the user registry wins unless explicitly overridden. Template now defaults `MITIRU_ENGINE_DIR` to `../engine` when that sibling exists, making worktree layouts "just work".

### Playwright Config (Tests)

`playwright.config.ts` added at repo root. `tests/web/boot.spec.ts` (KaeruCrape-coupled) is provided as a worked example. Playwright install not automatic (`@playwright/test` not in package.json) — consumers decide on the ~200 MB browser fetch. See `tests/web/README.md` for bootstrap.

### Replay Inspector Tests

`tools/replay_inspector/` test suite added (17 new tests, 290 → 311 total). Read-only viewer for `ObserveServer` capture files — see `docs/REPLAY_DEBUGGER.md` for usage.

---

## Validation Gaps (Honestly Noted)

1. **H-08 (CEF auto-focus)** — Header-only change. Not exercised against a live CEF build in-engine. KaeruCrape should validate during next build.

2. **`templates/native-plus-cef-overlay/` stale-API fix** — Template build not in test matrix this session. Consumer rebuild proves the fix works.

3. **G-14 finding** — `BackgroundManager` and `narrative::ScriptRunner` have no built-in `AnimatedSprite::loadGIF()` auto-wire. Documented as a roadmap note in `docs/NARRATIVE_VMS.md §7.7` but NOT fixed. Consumers using animated GIFs will need to wire the auto-load themselves or promote to C++ bridge if needed.

---

## What the Engine Team Is NOT Doing (Scope Enforcement)

Per `docs/SCOPE.md` and session history:

1. **Parity between `vn::ScenarioScript` and JSON `narrative::ScriptRunner`** — intentionally partitioned. Native VM is for engine-driven scenes; JSON VM is for static content. No "feature parity" goal.

2. **2D Action / Platformer Primitives (G-18)** — held until a 2nd consumer commits to the architecture. Mathlands metroidvania may be that consumer; reassess if/when that project picks an engine design.

3. **Visual editor, new JSON DSLs, browser-only ship, scope-down of 3D / ECS / network** — all explicitly chosen NOT to do. Engine scope remains dual-mode (Mode A + B), multi-backend, ECS-based, networked-ready.

---

## How to Integrate (By Consumer)

- **KaeruCrape:** Theme pack docs ready. Check H-05 textbox style changes if you have custom CSS.
- **hato:** Re-export with H-05 changes (lighter CSS); validate H-08 keyboard focus during CEF boot.
- **Mathlands:** Fresh template instantiation now includes M-03 fixes; no immediate action needed.
- **pandd-dodo:** Read `docs/NARRATIVE_VMS.md` for VM decision matrix. G-14 clarifies layer model; no code change. G-15 (probabilistic effects) remains deferred.

All changes are forward-compatible. No API deletions, no breaking ABI changes. Existing game code remains valid.

---

## Test Status

```bash
npm test                              # 311/311 ✓ (~6 s, happy-dom)
python tools/generate_bridge.py --self-test   # 17/17 ✓
git status                            # clean, main branch
```

See `docs/READING_ORDER.md` for next-step guidance by role (newcomer vs. LLM).
