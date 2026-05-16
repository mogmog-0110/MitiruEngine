# Getting Started with MitiruEngine

> **Pick your mode first.** MitiruEngine is dual-mode (see [`SCOPE.md`](SCOPE.md), canonical):
>
> - **Mode A — Native (C++ only):** console / mobile / 3D action / headless / tools. Pure C++,
>   no browser process. Smallest dependency surface, fastest cold build.
> - **Mode B — Hybrid (Mode A + CEF + JS):** desktop narrative / management / simulation games
>   where HTML / CSS / JS iterate faster than C++ UI code.
>
> The clone + build steps below are identical for both modes. Template choice (further down)
> depends on the mode you pick — there are now **three** starter templates.

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.21+ | [cmake.org/download](https://cmake.org/download/) |
| C++ Compiler | MSVC 2022 / GCC 13+ / Clang 18+ | C++20 required |
| Git | Any recent | Submodule support needed |

**Windows (recommended):** Install Visual Studio 2022 with the "Desktop development with C++" workload. CMake is bundled with VS but a standalone install also works.

**Linux:** `sudo apt install cmake g++-13 git` (Ubuntu/Debian). Ensure `g++-13` or later is available.

---

## Clone and Build (5 commands)

```bash
git clone --recursive https://github.com/mogmog-0110/MitiruEngine.git
cd MitiruEngine
cmake --preset default
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

> If you forgot `--recursive`, run `git submodule update --init --recursive` inside the repo.

### Build Presets

| Preset | Description |
|--------|-------------|
| `default` | Debug build (MSVC on Windows) |
| `release` | Optimized release build |
| `gcc` | GCC 13 on Linux |
| `clang` | Clang 18 on Linux |
| `emscripten` | WebAssembly build |

For a release build:

```bash
cmake --preset release
cmake --build build --config Release
```

---

## Run the Examples

The runnable sample set was retired in Round 39 pending a redesigned consumer
example collection. A fresh `examples/` set will land in the next release.

Until then, the templates under `templates/` and the [tutorials](tutorials/)
are the recommended starting points — both build cleanly against the
header-only engine.

---

## Create Your First Project from a Template

MitiruEngine ships **three starter templates** in `templates/`. All three use
`find_package(MitiruEngine)` and resolve the engine via either
`-DMITIRU_ENGINE_DIR=<path>` or a sibling `../engine` worktree.

| Template | Mode | When to pick |
|---|:-:|---|
| `native-only` | A | Pure C++ game. No CEF, no HTML, no JS runtime. Headless / console (planned) / 3D action / tools. Fastest build. |
| `native-plus-cef-overlay` | B | Native gameplay loop with a transparent CEF HUD on top. Menus, dialogs, score panels in HTML. KaeruCrape-style split. |
| `web-first-cef-shell` | B | Whole game is HTML/CSS/JS; C++ host is a thin shell + bridges. |

### Quick Start: Instantiate

```bash
# Mode A (pure native)
python templates/_scripts/instantiate.py \
    --variant native-only \
    --name MyGame \
    --dest ../MyGame

# Mode B (native + HUD overlay)
python templates/_scripts/instantiate.py \
    --variant native-plus-cef-overlay \
    --name MyGame \
    --dest ../MyGame

# Mode B (web-first)
python templates/_scripts/instantiate.py \
    --variant web-first-cef-shell \
    --name MyGame \
    --dest ../MyGame
```

The script substitutes `@NAME@` / `@NAME_LOWER@` placeholders in both file
contents and file names. See `templates/README.md` for the full substitution
table and probe-order details.

### Build the instantiated project

```bash
cd ../MyGame
cmake --preset default              # picks up sibling ../engine automatically
cmake --build build --config Debug
./build/MyGame                      # or build/Debug/MyGame.exe on Windows
```

If your engine checkout is **not** a sibling of the game project, pass
`-DMITIRU_ENGINE_DIR=<absolute path>` to the configure step. See the
"Two-phase resolution gotcha" section in `templates/README.md` if a different
engine checkout is being picked up than expected.

---

## Minimal Game Code (Mode A)

This is what `templates/native-only/src/main.cpp` produces in shape. Copy
into a fresh project, then replace the placeholder geometry with your own.

```cpp
#include <cmath>
#include <mitiru/Mitiru.hpp>

constexpr int kKeyEscape = 27;   // VK_ESCAPE
constexpr int kKeySpace  = 32;   // VK_SPACE

class MyGame final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        m_elapsed += dt;
        if (hasInput() && input().isKeyJustPressed(kKeyEscape))
        {
            if (auto* eng = engine()) eng->requestStop();
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        screen.clear(sgc::Colorf{0.04f, 0.05f, 0.09f, 1.0f});

        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());

        const bool  boost = hasInput() && input().isKeyDown(kKeySpace);
        const float pulse = 0.5f + 0.5f * std::sin(m_elapsed * 2.0f);
        const float size  = 80.0f + (boost ? 60.0f : 20.0f) * pulse;
        screen.drawRect(
            sgc::Rectf{w * 0.5f - size * 0.5f, h * 0.5f - size * 0.5f, size, size},
            sgc::Colorf{0.30f, 0.95f, 0.85f, 0.9f});

        // Use drawTextInRect, not drawText — see .claude/rules/mitiru-engine.md.
        screen.drawTextInRect(
            sgc::Rectf{0.0f, 24.0f, w, 32.0f},
            "Hello, Mitiru!",
            sgc::Colorf{0.95f, 0.97f, 1.0f, 1.0f},
            24.0f,
            mitiru::Screen::TextAlignH::Center, mitiru::Screen::TextAlignV::Top);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    float m_elapsed = 0.0f;
};

int main()
{
    mitiru::Engine engine;
    MyGame        game;

    mitiru::EngineConfig cfg;
    cfg.title           = "Hello Mitiru";
    cfg.windowWidth     = 1280;
    cfg.windowHeight    = 720;

    // Mode A: skip CEF entirely (no libcef.dll dependency at launch).
    cfg.enableCef = false;

    // Latin-only atlas → ~1 s startup vs ~15 s default Japanese atlas.
    // Switch to FontAtlas::Japanese when you render kana / kanji.
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
```

For Mode B, the `cefStartUrl` field of `EngineConfig` plus the template's
`web/` directory drive CEF — see the README inside each Mode B template
for the exact wiring.

---

## Project Structure

```
MitiruEngine/
├── include/mitiru/         # All engine headers (header-only library)
│   ├── core/               # Engine, Clock, Config, Game, Screen, GameLoop
│   ├── ecs/                # Entity-Component-System (MitiruWorld, SystemScheduler)
│   ├── scene/              # Scene management, GameWorld, SceneGraph
│   ├── render/             # 2D/3D rendering, Camera, Mesh, Material, Light
│   ├── gfx/                # GPU abstraction (DX11, DX12, Vulkan, OpenGL, WebGL, Null)
│   ├── input/              # Input state, key codes, gamepad, input mapper
│   ├── audio/              # Audio engine, mixer, MML music playback
│   ├── physics/            # Collision detection, rigid body, physics system
│   ├── network/            # TCP transport, lobby, state sync
│   ├── vn/                 # Visual novel engine (40+ modules)
│   ├── ui/                 # UI framework (nodes, themes, layout)
│   ├── bridge/             # ShiggyGameCore integration layer
│   ├── scripting/          # Script engine, parser, evaluator
│   ├── resource/           # Asset loading, hot-reload, font loader
│   ├── asset/              # Asset pipeline, SVG generation, mesh cache
│   ├── data/               # JSON, config, schema validation, tilemap
│   ├── control/            # Command queue, replay system
│   ├── observe/            # State inspection, diff tracking, HTTP server
│   ├── validate/           # Test harness, health checks, invariants
│   ├── debug/              # Debug overlay, profiler, logging
│   ├── ai/                 # Behavior trees, pathfinding, test runner
│   ├── platform/           # Window creation (Win32, GLFW, SDL2, Emscripten)
│   ├── cef/                # CEF host, StateStore, render handler (Mode B)
│   └── Mitiru.hpp          # Umbrella header (includes everything)
├── external/               # Third-party dependencies (submodules)
│   ├── sgc/                # ShiggyGameCore (ECS, math, physics, AI)
│   ├── mml/                # MitiruMML (music macro language)
│   ├── stb/                # stb_image, stb_truetype
│   └── ...
├── templates/              # Starter templates: native-only (Mode A), native-plus-cef-overlay (Mode B), web-first-cef-shell (Mode B)
├── examples/               # 11 single-purpose runnable samples
├── web/                    # JS runtime modules (Mode B): mitiru.audio/save/novel/input/...
├── tests/                  # Unit tests (CTest)
├── tools/                  # CLI tool, helper scripts
├── assets/                 # Shared assets
└── docs/                   # Documentation and tutorials
```

---

## Next Steps

- [Reading Order — where to go next](READING_ORDER.md)
- [Scope and Mode Declaration (canonical)](SCOPE.md)
- [Hybrid Runtime — Mode B JS / JSON / C++ split](HYBRID_RUNTIME.md)
- [Tutorial 1: Your First Visual Novel](tutorials/01_first_vn.md)
- [Tutorial 2: Build a Flappy Bird Clone](tutorials/02_arcade_game.md)
- [Tutorial 3: Your First 3D Scene](tutorials/03_3d_scene.md)
- [Tutorial 5: Entity Component System Guide](tutorials/05_ecs_basics.md) *(tutorial 4 is not yet written)*
- [Architecture Overview](ARCHITECTURE.md)
- [Troubleshooting](TROUBLESHOOTING.md)
