# MitiruEngine

A header-only C++20 game engine designed around one principle: gameplay logic
lives in C++, and HTML/CSS handles the UI. The engine runs the simulation.
CEF renders the HUD. A thin signal-only bridge connects the two — C++ tells
CEF what to display; CEF tells C++ what the player did. No gameplay state
leaks into JavaScript.

Pre-1.0 / experimental. API may break between tagged releases.

[Live site](https://mogmog-0110.github.io/MitiruEngine) &middot;
[Examples](https://mogmog-0110.github.io/MitiruEngine/examples/) &middot;
[API reference](https://mogmog-0110.github.io/MitiruEngine/api/) &middot;
[ADRs](https://mogmog-0110.github.io/MitiruEngine/adr/)

---

## What you get

- **Header-only by default** — `#include <mitiru/Mitiru.hpp>` and link nothing;
  `MITIRU_HEADER_ONLY=ON` is the CMake default. A static-library mode
  (`MITIRU_HEADER_ONLY=OFF`) is available for larger projects.
- **684 headers across 20+ modules** — core, ecs, scene, gfx, audio, bridge,
  vn, animation, asset, physics, input, and more under `mitiru::*` namespaces.
- **Multiple graphics backends** — Direct3D 11, Direct3D 12, Vulkan, OpenGL,
  WebGL, and a Null backend for headless testing. Windows + MSVC is the primary
  target; the OpenGL/GLFW path builds on macOS and Linux (less tested).
- **CEF 128.4.12 UI layer** — load any HTML/CSS page as your game's HUD or
  menu. The signal-only bridge keeps gameplay state in C++ and DOM updates in
  JavaScript.
- **nlohmann::json content pipeline** — novel scripts, i18n tables, balance
  data, and save blobs are plain JSON. JSON Schema validation is built in.
- **Tracy profiling** — drop `external/tracy/public/TracyClient.cpp` into the
  tree and the engine auto-detects it at configure time.

---

## Quick start

Consume via CMake `FetchContent`:

```cmake
include(FetchContent)

FetchContent_Declare(
    mitiru
    GIT_REPOSITORY https://github.com/mogmog-0110/MitiruEngine.git
    GIT_TAG        v0.1.0   # replace with the tag you want
)
FetchContent_MakeAvailable(mitiru)

target_link_libraries(my_game PRIVATE mitiru::mitiru)
```

A minimal game entry point:

```cpp
#include <mitiru/Main.hpp>
#include <mitiru/MitiruCore.hpp>

class MyGame : public mitiru::Game {
public:
    void onUpdate(float dt) override { /* gameplay here */ }
    void onDraw()           override { /* draw calls here */ }
};

MITIRU_MAIN(MyGame)
```

---

## Build and test

```bash
cmake --preset default
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

Windows + MSVC is the primary build target. The OpenGL/GLFW backend compiles
on macOS and Linux but receives less testing. Direct3D backends require the
Windows SDK. Tracy profiling is opt-in: place
`external/tracy/public/TracyClient.cpp` in the tree before configuring.

---

## Project structure

```
include/mitiru/     -- all engine headers (header-only mode default)
examples/           -- 13 worked examples, each a self-contained CMake target
docs/               -- architecture docs, ADRs, bridge API contract
site/               -- Hugo source for the live documentation site
tests/              -- Catch2 v3 unit and integration tests
external/           -- vendored dependencies (CEF, nlohmann, Catch2, ...)
tools/              -- scaffolding, API catalog generator, release tooling
```

---

## Examples

The [examples gallery](https://mogmog-0110.github.io/MitiruEngine/examples/)
covers 13 worked scenarios. Three good starting points:

- **`vn_game_hybrid`** — visual-novel gameplay in C++ with a CEF HUD; shows
  the full signal-only bridge round-trip.
- **`cef_overlay`** — minimal CEF overlay on top of a native render loop.
- **`parametric_portrait`** — procedural character rendering with no CEF
  dependency; good for Mode A (native-only) projects.

---

## Architecture

Gameplay — scenes, state machines, simulation, save/load, AI, physics — is
written in C++. CEF is a display layer only: it renders HTML/CSS HUDs and
menus and fires input events back to C++ through `window.cefQuery`. No
gameplay state lives in JavaScript. This boundary is described in full in
[ADR 0001](https://mogmog-0110.github.io/MitiruEngine/adr/0001/) and
`docs/BRIDGE_API_CONTRACT.md`.

---

## License

MIT. See [LICENSE](LICENSE).

---

## Mirror notice

This repository is a snapshot mirror of the private development repository
(`MitiruEngineDev`). Tagged releases are built from that tree and pushed here
by the release tooling. The source you see is the exact code that shipped —
nothing is trimmed or altered beyond stripping internal-only tooling.

Issues are welcome here. Pull requests may be redirected to a discussion on
the issue tracker first, since the canonical history lives upstream.
