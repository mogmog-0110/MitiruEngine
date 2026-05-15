# MitiruEngine — Scope and Mode Declaration

> **Canonical statement.** When other docs (README.md, HYBRID_RUNTIME.md,
> ARCHITECTURE.md, GETTING_STARTED.md) appear to disagree with this file,
> **this file wins.** The other docs are being aligned over time.

## TL;DR

MitiruEngine is a **dual-mode game engine**. Both modes are first-class:

- **Mode A — Native (C++ only):** for console, mobile, 3D action, headless, and any non-CEF target.
- **Mode B — Hybrid (Mode A + CEF + JS):** for desktop narrative / management / sim games and AI-assisted development.

Neither mode is deprecated. Neither will be deprecated in the foreseeable roadmap.

---

## Mode A — Native

**What it is.** Pure C++ game using `mitiru::Game`, `mitiru::Engine`, `mitiru::Screen` and the rest of the `include/mitiru/` tree. No CEF process. No JavaScript runtime. No HTML.

**Targets.**
- Desktop (Windows / Linux / macOS)
- **Console** (planned: Xbox / PS5 via DX12, Switch via Vulkan)
- **Mobile** (planned: evaluate Metal vs Vulkan mobile)
- Headless / server / test
- 3D action / twitch genres where 60 fps deterministic loop matters

**Provided by Mode A.**
- `core` — Engine loop, Screen API, `Game` base
- `gfx` — DX11 / DX12 / Vulkan / OpenGL / WebGL / Null backends behind `IDevice`
- `platform` — Win32 / GLFW / SDL2 / Emscripten windows
- `scene`, `ecs` — scene graph + ECS (entt + sgc bridge)
- `physics` — Box2D / Jolt
- `render` — full 2D + 3D pipeline (Phong, Toon, NPR, Shadow, SSS, particle compute when complete)
- `audio` — miniaudio + MitiruMML
- `input`, `network`, `vn` (native), `ai`, `animation`, `scripting`, etc.

**When to choose Mode A.**
- Shipping to console or mobile (Mode B unavailable: CEF is desktop-only)
- 3D action / twitch genres (CEF round-trip latency unacceptable)
- Server-side simulation / headless tests
- You prefer one language for the whole game

---

## Mode B — Hybrid

**What it is.** Mode A's full native runtime, plus Chromium Embedded Framework (CEF) hosting an HTML / CSS / JS / JSON game runtime. Game logic primarily lives in JavaScript. C++ provides platform services and is exposed to JS through typed bridges (`StateStore`, `AudioBridge`, `SaveStore`, …).

**Targets.** Desktop only (Windows / Linux / macOS). CEF is not available for console or mobile.

**Adds on top of Mode A.**
- `include/mitiru/cef/` — CEF process host, `MitiruCefContext`, `StateStore`, render handler
- `include/mitiru/bridge/` (CEF-side subset) — typed bridges for JS ↔ C++
- `web/mitiru_runtime/` — JS runtime modules (`mitiru.audio`, `mitiru.save`, `mitiru.novel`, `mitiru.input`, …)

**When to choose Mode B.**
- UI is the dominant cost center (narrative, management, simulation, dialogue-heavy)
- AI-assisted development matters (LLMs generate HTML / CSS / JS faster than they generate C++ UI code)
- Iteration speed > runtime efficiency
- Desktop-only ship is acceptable

---

## Module status matrix

| Module group | Mode A | Mode B | Status |
|---|:-:|:-:|---|
| `core`, `gfx`, `platform`, `scene`, `ecs`, `audio`, `input`, `asset`, `resource`, `data`, `control`, `util`, `math`, `i18n`, `observe`, `debug`, `validate` | ✓ | ✓ | Stable |
| `render` — 2D pipeline + basic 3D | ✓ | ✓ | Stable |
| `render` — compute paths (`ParticleCollision`, `SubsurfaceScatter`, `ShadowPass3D` advanced) | ✓ | — | **Incomplete** (TODO compute shaders); to be completed for Mode A console / 3D roadmap |
| `physics` — Box2D, Jolt | ✓ | optional | Stable |
| `vn` (native) | ✓ | optional | Stable (40+ modules) |
| `vn` — JS implementation (`web/mitiru_runtime/mitiru_novel.js`) | — | ✓ | Stable, parallel to native vn |
| `network` — TCP, lobby, state sync | ✓ | optional | Stable |
| `network` — `ReliableUDP`, GameNetworkingSockets | ✓ | — | **Incomplete** (TODO callbacks); to be completed for multiplayer 3D roadmap |
| `cef`, `web/mitiru_runtime/`, CEF-side bridges | — | ✓ | Stable |
| `ai`, `animation`, `scripting`, `ui`, `sprite`, `effects`, `live2d`, `server` | ✓ | ✓ | Stable |

---

## Out of scope (explicit non-goals)

- **Visual editor.** Use Godot or Unity if you need a built-in scene editor.
- **New JSON DSLs.** The HYBRID_RUNTIME §4 decision matrix (data → JSON, interactive → JS, hot → C++) is final.
- **Browser-only ship.** Mode A supports Emscripten; Mode B requires CEF (desktop). Neither is a pure web ship target.
- **Heavy-handed scope cuts.** Earlier proposals to deprecate 3D / ECS / multi-backend / network were rejected on 2026-05-04. Both modes evolve forward.

---

## Roadmap snapshot

**Mode A**
- Complete render compute paths (Particle, SSS, Shadow advanced)
- Complete ReliableUDP + GameNetworkingSockets for multiplayer
- Console preparation: DX12 polish for Xbox / PS5; Vulkan polish for Switch
- Mobile preparation: evaluate Metal vs Vulkan-mobile backend

**Mode B**
- ~~Bridge code generator (`bridge.json` → C++ + JS pair, eliminates manual sync)~~ — done 2026-05-04 (`tools/generate_bridge.py`, see `docs/BRIDGE_CODEGEN.md`)
- vitest for JS unit tests (started 2026-05-04 with `mitiru.save` — `web/mitiru_runtime/*.js` coverage to grow)
- ~~Hot-reload first-class, documented~~ — done 2026-05-04 (promoted `tools/mitiru_serve.py` in README, HYBRID_RUNTIME, READING_ORDER)
- State replay debugger via `observe/CausalChain` + `Snapshot` → CEF DevTools

**Both**
- Documentation re-alignment (this file is part of that effort)
- ~~`docs/API_CATALOG.md` split into LLM-friendly chunks + compressed index~~ — done 2026-05-04 (`docs/api/<module>.md` + `docs/api/_index.jsonl` via `tools/generate_api_chunks.py`)
- `.claude/rules/` audit (completed 2026-05-04: 21 → 11 files; stale generic-template rules removed; see `.claude/docs/rules-reference.md`)
- `MITIRU_HEADER_ONLY` switchable model to allow per-module `.cpp` separation (consumer build-time relief)

---

## Reading next

- New to the engine? → `docs/READING_ORDER.md`
- Building a Mode A game? → `docs/ARCHITECTURE.md`, `include/mitiru/core/Engine.hpp`
- Building a Mode B game? → `docs/HYBRID_RUNTIME.md`, `docs/HYBRID_UI_GUIDE.md`, `templates/web-first-cef-shell/`
- LLM agent? → `CLAUDE.md`, then this file, then `docs/HYBRID_RUNTIME.md §2` (decision matrix)
