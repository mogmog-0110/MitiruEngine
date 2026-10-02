# MitiruEngine Architecture Overview

ゲームは`MITIRU_GAME(YourType)`で書くDLLとして作る。host (`mitiru_host.exe`)が
C ABI (Cの関数と生データだけで会話する取り決め)越しにloadし、毎フレームPODでやり取りする。
gameplayは常にC++で書き、JSでは書かない。`Game`を継承して自前の`main()`を持つ旧authoringは廃止した。

UI と HUD は RmlUi (`include/mitiru/ui_rml/`) が RML / RCSS で描き、ゲームを描いた同じ DX12 のフレームに重ねる。
DLL の隣に `assets/ui/main.rml` があるときだけ動く (`EngineConfig::uiDocument`)。無ければ下の層だけで動き、
headless 実行、console、3D action などはこの形になる。bridge は signal-only で、C++ から UI へは `hud.set` の
state push、UI から C++ へは `dispatch` の action だけが流れる。スクリプトは無い。

UI の書き方は [UI_RMLUI.md](UI_RMLUI.md)、ツール窓は [TOOL_WINDOWS.md](TOOL_WINDOWS.md)。

## Layer Stack

The top layer (UI) runs only when the game ships `assets/ui/main.rml`
(`EngineConfig::uiDocument`). Without it, the stack below is the whole engine.

```
+----------------------------------------------------------+
|  UI LAYER  (mitiru::ui_rml, only when main.rml exists)    |
|  RmlUiHost  RmlStateModel  RmlRenderInterfaceDx12        |
|  RML / RCSS composited over the game frame (DX12)        |
+----------------------------------------------------------+
|  APPLICATION LAYER                                       |
|  User Games (MITIRU_GAME DLL)  | examples/*              |
+----------------------------------------------------------+
|  SGC BRIDGE LAYER  (mitiru::bridge)                      |
|  Adapts ShiggyGameCore (sgc) into Mitiru types.          |
|  Ai  Anim  DebugDraw  Dialogue  Event  I18n  Particle   |
|  Physics  Procedural  Save  Steering  Tilemap            |
|  Transition  UI  VN  Renderer3D                          |
+----------------------------------------------------------+
|  ENGINE LAYER  (mitiru::core)                            |
|  Engine  Clock  GameLoop  Game  Screen  Config           |
+----------------------------------------------------------+
|  SCENE / ECS LAYER  (mitiru::scene / mitiru::ecs)        |
|  MitiruSceneManager  SceneGraph  GameWorld               |
|  MitiruWorld  SystemRunner  SystemScheduler              |
+----------------------------------------------------------+
|  SUBSYSTEM LAYER                                         |
|  Screen  Input  Audio  Physics  Network  Scripting       |
|  Render  Resource  Asset  Data  Control  Observe         |
+----------------------------------------------------------+
|  PLATFORM LAYER  (mitiru::platform)                      |
|  IPlatform  IWindow  WindowFactory                       |
|  Win32Window | GlfwWindow | Sdl2Window | EmscriptenWindow|
+----------------------------------------------------------+
|  GRAPHICS LAYER  (mitiru::gfx)                           |
|  IDevice  IBuffer  ICommandList  ISwapChain  IPipeline   |
|  DX11 | DX12 | Vulkan | OpenGL | WebGL | Null            |
+----------------------------------------------------------+
```

> **`include/mitiru/bridge/`** holds adapters between ShiggyGameCore (`sgc`,
> the C++ ECS / math / physics / AI library that lives under `external/sgc/`)
> and the Mitiru type system (`AiBridge`, `PhysicsBridge`, `Renderer3DBridge`),
> plus `StateStore`, the last value of every `hud.set` key, used for
> observation and replay-as-test. None of them depend on the UI layer.

---

## Module Dependency Diagram

```
                    mitiru::core::Engine
                    /       |        \
                   /        |         \
          platform/     gfx/IDevice    core/Clock
          IWindow       /  |   \        |
            |         DX11 DX12 Null  core/GameLoop
            |          |              |
            v          v              v
        input/      render/       scene/
       InputState  Screen         MitiruScene
          |        / |   \           |
          |   Sprite Shape Text   ecs/MitiruWorld
          |   Batch  Rend  Rend      |
          |     \    |    /       sgc::ecs::World
          v      v   v   v
       bridge/  RenderPipeline2D
       (16 bridges to sgc)
          |
          v
    external/sgc  (ShiggyGameCore)
```

### Key Dependencies

| Module | Depends On | Provides To |
|--------|-----------|-------------|
| `core/Engine` | platform, gfx, render, input, scene | Game loop orchestration |
| `core/Screen` | render/SpriteBatch, render/ShapeRenderer, render/TextRenderer | 2D drawing API for games |
| `ecs/MitiruWorld` | sgc::ecs::World, observe/SemanticLabel | Entity management with metadata |
| `scene/MitiruScene` | core/Screen, ecs/MitiruWorld | Scene lifecycle (enter/exit/update/draw) |
| `render/Renderer3D` | gfx/IDevice (DX12本命), render/Mesh, render/Camera3D, render/Light | 3D mesh rendering (Phong/Toon/NPR/WBOIT) |
| `render/RenderPipeline2D` | gfx/IDevice, gfx/IBuffer | GPU submission of 2D draw commands |
| `bridge/*` | sgc (ShiggyGameCore) | Adapted APIs for AI, physics, animation, etc. |
| `vn/*` | data/Json | Visual novel script core: ScenarioScript + FlagManager + ExpressionEvaluator (ADR 0049) |
| `network/*` | platform/SocketCompat | TCP transport, lobby, state sync |

---

## Render Pipeline Flow

### 2D Rendering

```
Game::draw(Screen&)
  |
  +-- screen.fillRect()  -----> SpriteBatch.drawRect()
  +-- screen.drawCircle() ----> ShapeRenderer.drawCircle()
  +-- screen.drawText()   ----> TextRenderer.drawText()
  +-- screen.drawLine()   ----> ShapeRenderer.drawLine()
  |
Screen::present()
  |
  +-- SpriteBatch.end()          Finalize vertex buffer
  +-- ShapeRenderer.flush()      Finalize line/shape vertices
  |
  +-- RenderPipeline2D::submitBatch(vertices, indices)
        |
        +-- IBuffer::update()    Upload to GPU
        +-- IDevice::draw()      Issue draw call
        +-- ISwapChain::present() Flip buffers
```

### 3D Rendering

```
Game::draw(Screen&)
  |
  +-- renderer3D()->beginFrame(clearColor)
  |     +-- Set render target + depth buffer
  |     +-- Clear backbuffer
  |
  +-- renderer3D()->setCamera(camera)
  |     +-- Compute view/projection matrices
  |     +-- Update CbTransform constant buffer
  |
  +-- renderer3D()->setLight(light)
  |     +-- Update CbLighting constant buffer
  |
  +-- renderer3D()->drawMesh(mesh, worldMatrix, material)
  |     +-- Create/update vertex buffer (VB) and index buffer (IB)
  |     +-- Update world matrix in CbTransform
  |     +-- Update material in CbLighting
  |     +-- IASetVertexBuffers + IASetIndexBuffer
  |     +-- DrawIndexed()
  |
  +-- renderer3D()->endFrame()
        +-- 2D overlay (Screen::present on top of 3D)

--- Post-Processing (optional) ---
  |
  +-- PostProcessManager::beginScene()  Redirect to offscreen RT
  +-- PostProcessManager::endScene()    Apply effects, blit to backbuffer
```

### 2D and 3D Coexistence

The engine supports mixed 2D/3D rendering in a single frame:

1. `IDevice::beginFrame()` starts the GPU frame
2. `Renderer3D::beginFrame()` clears the 3D backbuffer and sets depth state
3. `Renderer3D::drawMesh()` renders 3D objects
4. `Renderer3D::endFrame()` finalizes 3D pass
5. Engine resets render target (removes depth buffer for 2D)
6. `Screen::present()` draws 2D content on top as overlay (HUD, UI, text)
7. Post-processing (if enabled) runs on the composited result
8. `IDevice::endFrame()` presents to the window

For DX12, `Renderer3D_DX12` manages the overlay automatically via `setOverlayScreen()`.

---

## Audio Pipeline Flow

> **現行:** DLLゲームは`AudioEngine`を直接呼ばない。`FrameIntents`（エンジンへの依頼を書く欄）に`SoundIntent`を
> 積み（`hud.play("click")`）、hostが`SoundIntentRouter`経由で下記のaudio engineを駆動する。
> 下図はhost側(engine内部)の経路。

```
Host (SoundIntentRouter)
  |
  +-- AudioEngine::playSE("click.wav")
  |     +-- MiniaudioEngine (mitiru_host) / WebAudioEngine (wasm) / NullAudioEngine
  |
  +-- AudioMixer::play(category, sound)
  |     +-- Category: BGM | SE | Voice
  |     +-- Per-category volume control
  |     +-- Master volume
  |
  +-- MitiruMML (music macro language)
        +-- Parse MML string -> note/rest/tempo events
        +-- SoftSynth generates PCM samples
        +-- Output to MiniaudioOutput / Null

Audio Output Backends:
  Desktop:    miniaudio (WASAPI / DirectSound / WinMM / PulseAudio / ALSA / CoreAudio を miniaudio が選ぶ)
  Web:        WebAudioEngine (AudioBufferSourceNode。ミックスはブラウザの音声スレッド)
  Headless:   NullAudioEngine / NullAudioOutput (no output, state tracking only)
```

---

## Input Pipeline Flow

> **現行:** DLLゲームは`InputState`を直接見ない。hostが毎フレーム`InputState`から
> PODの`InputSnapshot`（256キー/マウス/パッド + action event + rngSeed + audioTime）を組んで
> `on_update`に渡す。キー再割り当ては`Game.hpp`の`Binding<Act>`（ゲームの状態structに置く）。下図の
> `InputMapper`は非推奨。録画・再生はhost側のreplay機構（`replay/Recorder` / `replay/Player`、`.mtrr`に記録したInputSnapshotの再投入）。

```
OS Events (WM_KEYDOWN, SDL_Event, etc.)
  |
  v
Platform Window (Win32Window / Sdl2Window / GlfwWindow)
  |
  +-- setInputState(&inputState)    Connect raw events
  |
  v
InputState
  |
  +-- isKeyDown(keyCode)            Current frame state
  +-- isKeyJustPressed(keyCode)     Edge detection (this frame only)
  +-- isMouseButtonDown(button)     Mouse button state
  +-- mousePosition()               Cursor coordinates
  |
  v
InputMapper (optional)
  |
  +-- Map physical keys to logical actions
  +-- "jump" -> Space, "move_left" -> A/Left
  |
  v
InputSnapshot (host builds one per frame)
  |
  +-- replay/Recorder writes it to .mtrr (mitiru_host --record)
  +-- replay/Player swaps it in on playback (mitiru_host --replay-test)

External Input Injection (HTTP API / test harness):
  InputInjector -> Engine applies to InputState before game.update()
```

---

## Visual Novel System Integration

```
mitiru::vn (script core only, ADR 0049)
  |
  +-- ScenarioScript           Parse the .scenario text DSL
  |     +-- Lexer -> Parser -> ScenarioNode list -> ScenarioExecutor
  |     +-- Commands: @scene, @bg, @char, @choice, @label, @jump, @set, @if, @wait, @script
  |     +-- ScenarioCallback receives every command; the game decides what it means
  |
  +-- FlagManager              Scenario variable store (JSON round trip)
  +-- ExpressionEvaluator      Conditions for @if ($var, arithmetic, built-ins)

Message window, choices, backlog and settings are UI-layer work (RmlUi, ADR 0051);
the old C++ UI parts live on branch attic/vn-ui.
```

---

## Module Descriptions

### Engine (mitiru::core::Engine)
The central coordinator. `Engine::run()` initializes the platform, window, and GPU device according to `EngineConfig`, then drives the main loop: `pollEvents` -> `clock.tick` -> `game.update` -> `sceneManager.currentScene().onUpdate` -> `screen.clear` -> `game.draw` -> `sceneManager.currentScene().onDraw` -> `device.beginFrame` -> `screen.present` -> `device.endFrame`. A headless `stepFrames()` path skips window/GPU setup and activates a software rasterizer inside `Screen` for pixel-level testing.

> **DLLモジュールの場合:** `game.update`/`game.draw`は`Engine::runModule`内の
> stack-local `ModuleAdapter`。これがhostのループをC ABIへ橋渡しし、`InputSnapshot`を組んで
> `on_update(memory, dt, input, intents)`を呼び、`FrameIntents`をdrain、`on_draw(memory, Screen*)`を呼ぶ。
> `game.update`を継承クラスで実装する旧authoringの形は現行hostでは使わない。

### Screen (mitiru::Screen)
The drawing surface passed to `Game::draw()`. It accumulates draw commands into `SpriteBatch` (quads/sprites) and `ShapeRenderer` (lines, triangles, circles). On `present()` the accumulated vertex data is forwarded to `RenderPipeline2D` for GPU submission, or rasterized in software when the headless framebuffer is active. Screen has no awareness of the underlying GPU backend.

### ECS (mitiru::ecs / sgc::ecs)
`MitiruWorld` wraps `sgc::ecs::World` and adds string tags, semantic labels, and JSON snapshot support. Systems iterate components via `world.forEach<T>()`. `SystemRunner` holds an ordered list of `ISystem` instances and calls `updateAll()` each frame. `GameWorld` provides a simpler flat entity/component store used by sample games and tests.

### Scene Management (mitiru::scene)
`MitiruSceneManager` maintains a stack of `MitiruScene` objects. `pushScene` / `popScene` / `replaceScene` trigger `onEnter` / `onExit` lifecycle callbacks. The Engine holds a non-owning pointer to the manager and calls `onUpdate` / `onDraw` on the top-of-stack scene each frame after delegating to `Game`.

### Bridge Layer (mitiru::bridge)
Sixteen bridge classes (plus the umbrella `SgcBridge`) adapt subsystems from the `sgc` (ShiggyGameCore) library into Mitiru's type system. Each bridge owns the sgc objects it manages and exposes a clean Mitiru-flavored API. For example, `AiBridge` owns `sgc::bt::Node` trees and `sgc::ai::UtilitySelector` instances, translating `sgc::bt::Status` to `AiState` and forwarding A* calls unchanged. This isolates the rest of the engine from direct sgc type exposure. These bridges do not depend on the UI layer.

### Graphics Abstraction (mitiru::gfx)
`IDevice` is the sole GPU interface. All backends implement `beginFrame`, `endFrame`, `readPixels`, and factory-constructed pipeline/buffer/shader objects through their respective `I*` interfaces. `GfxFactory::createDevice()` selects the backend at runtime according to the priority chain described below. `NullDevice` fulfils the interface with no-ops, enabling headless execution without conditional compilation at call sites.

### Platform Abstraction (mitiru::platform)
`IPlatform` creates `IWindow` instances. `IWindow` provides `width`, `height`, `pollEvents`, and `shouldClose`. Platform-specific input handling (`Win32Input`, `Sdl2Input`, `GlfwInput`) connects raw OS events to `InputState` via `setInputState`. `WindowFactory::createWindow(WindowBackend::Auto, ...)` selects the best available window implementation at runtime.

---

## Frame Data Flow

```
Engine::run()
  |
  +-- IWindow::pollEvents()          OS events -> InputState
  |
  +-- Clock::tick()                  -> float dt
  |
  +-- Game::update(dt)               user logic
  +-- MitiruScene::onUpdate(dt)      scene logic, ECS systems
  |
  +-- Screen::clear()                resets SpriteBatch/ShapeRenderer
  +-- Game::draw(Screen&)            issues draw* calls
  +-- MitiruScene::onDraw(Screen&)   scene rendering
  |
  +-- IDevice::beginFrame()          GPU frame start / clear RT
  |
  +-- Screen::present()
  |     |
  |     +-- SpriteBatch::end()       finalize vertex list
  |     +-- RenderPipeline2D::submitBatch(verts, indices)
  |           |
  |           +-- IBuffer::update()  upload vertices to GPU
  |           +-- ICommandList::draw()
  |           +-- ISwapChain::present()
  |
  +-- IDevice::endFrame()            flush / swap buffers
```

---

## Graphics Backend Selection (Auto mode)

`gfx::createDevice(Backend::Auto, window)` resolves at compile-time and runtime:

```
Windows?
  yes -> Win32Window present?
           yes -> Dx12Device  (本命。生成失敗時のみ Dx11Device へ明示 fallback)
           no  -> NullDevice
  no  -> Emscripten?
           yes -> WebGLDevice  (WebGPU backend も存在)
           no  -> MITIRU_HAS_VULKAN && MITIRU_HAS_GLFW && GlfwWindow?
                    yes -> VulkanDevice
                    no  -> MITIRU_HAS_OPENGL && Sdl2Window?
                             yes -> GlDevice
                             no  -> NullDevice
```

Explicit backend selection (`Backend::Dx12`, `Backend::Vulkan`, etc.) throws `std::runtime_error` when the required window type or compile flag is absent, rather than silently falling back.

The Auto-mode DX12 → DX11 fallback is **explicit, not silent**: it emits one stderr line (`warnOnce`, key `gfx.dx12.fallback`) with the failure reason and the DX12-only features that become unavailable (WBOIT / HDR / MSAA / FXAA / shadow). Explicitly selecting `Backend::Dx11` is a user decision and is not logged.

---

## Bridge Pattern Detail

Each bridge class holds one or more sgc objects by value or `unique_ptr` and provides:

- **Type translation** -- sgc enums/structs converted to Mitiru-local equivalents (e.g. `sgc::bt::Status` -> `AiState`).
- **Lifetime management** -- registered resources are keyed by `std::string` name and stored in `unordered_map`; unregister APIs clean up ownership cleanly.
- **JSON introspection** -- every bridge exposes `toJson()` for use by the `observe` subsystem's HTTP inspection server.

The 16 bridges cover: AI (BehaviorTree/UtilityAI/GOAP/A*), Animation, DebugDraw, Dialogue, Event, I18n, Particle, Physics, Procedural generation, Renderer3D, Save/Load, Steering behaviours, Tilemap, Screen transitions, UI, and Visual Novel sequencing.

---

## Multi-Platform Strategy

| Platform      | Window        | Graphics   | Audio             |
|---------------|---------------|------------|-------------------|
| Windows       | Win32Window   | DX12 (明示fallback: DX11) | miniaudio |
| Linux/macOS   | GlfwWindow    | Vulkan     | SoftAudioEngine   |
| Linux/macOS   | Sdl2Window    | OpenGL     | miniaudio         |
| Web (WASM)    | EmscriptenWindow | WebGL2  | SoftAudioEngine   |
| Headless/Test | HeadlessPlatform | NullDevice | NullAudioEngine |

All platform and graphics objects are accessed exclusively through abstract interfaces (`IPlatform`, `IWindow`, `IDevice`), so user game code and engine systems are fully portable across the matrix above. The `EngineConfig::headless` flag bypasses all windowing and GPU initialization for test and server-side use.

---

## Optional Dependencies

| Dependency | CMake Flag | Feature |
|-----------|------------|---------|
| Jolt Physics | `MITIRU_HAS_JOLT` | Full 3D physics simulation |
| Tracy Profiler | `MITIRU_HAS_TRACY` | Frame-level profiling |
| Zstandard | `MITIRU_HAS_ZSTD` | Asset compression |
| efsw | `MITIRU_HAS_EFSW` | File watching for hot reload (falls back to mtime polling) |
| Taskflow | `MITIRU_HAS_TASKFLOW` | Worker threads for `JobSystem` (falls back to running jobs inline) |
| Recast/Detour | `MITIRU_HAS_RECAST` / `MITIRU_HAS_DETOUR` | Navmesh bake (`mitiru_navbake`) and path queries (`nav::NavMesh`), linked per target via `mitiru_nav_bake` / `mitiru_nav` ([NAVMESH.md](NAVMESH.md)) |
| GekkoNet | `MITIRU_HAS_GEKKONET` (opt-in `-DMITIRU_WITH_GEKKONET=ON`) | Rollback netcode: `RollbackPeer` drives a game DLL via GameMemory memcpy save/load, `mitiru_rollback` checks two DLLs over loopback ([ROLLBACK_NETCODE.md](ROLLBACK_NETCODE.md)) |
| Effekseer | `MITIRU_HAS_EFFEKSEER` (opt-in `-DMITIRU_WITH_EFFEKSEER=ON`, Windows) | Particle effects (`.efkefc`) drawn in the DX12 3D frame; games call `drawModel(path, pos, rotY, scale, key, ageSec)` ([EFFEKSEER.md](EFFEKSEER.md)) |
| spdlog | `MITIRU_HAS_SPDLOG` | Structured logging |
| SDL3 | `MITIRU_HAS_SDL3` | Gamepads on every OS (`SdlGamepadInput`, ADR 0048). Windows configure fetches the pinned VC package into `external/sdl3/` (`cmake/MitiruSdl3.cmake`, `tools/fetch_sdl3.py`) |
| SDL2 | `MITIRU_HAS_SDL2` | Frozen Linux/macOS window for OpenGL (`Sdl2Window`, ADR 0047). Searched only on non-Windows when SDL3 is absent |
| GLFW | `MITIRU_HAS_GLFW` | GLFW window/input backend |
| Vulkan SDK | `MITIRU_HAS_VULKAN` | Vulkan graphics backend |
| OpenGL | `MITIRU_HAS_OPENGL` | OpenGL graphics backend |

All optional dependencies degrade gracefully. When absent, null/stub implementations are used automatically.

### アニメーションブレンド

骨格アニメの姿勢は `animation/AnimPose.hpp` の `evaluatePose(asset, params, pose)` が出す。
`AnimPoseParams` (POD) のレイヤを順に重ね、Override は重みとマスクで混ぜ、Additive は差分を足す。
歩行→走行の遷移は 2 レイヤ、ブレンドスペースは `AnimBlendSpace.hpp` の重みをレイヤにして作る。
ゲーム DLL と host の描画が同じ関数を使う。詳細は [ANIMATION_RUNTIME.md](ANIMATION_RUNTIME.md)。
