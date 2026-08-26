# MitiruEngine Architecture Overview

ゲームは `MITIRU_GAME(YourType)` で書く DLL として作る。host (`mitiru_host.exe`) が
C ABI (C の関数と生データだけで会話する取り決め) 越しに load し、毎フレーム POD でやり取りする。
gameplay は常に C++ で書き、JS では書かない。`Game` を継承して自前の `main()` を持つ旧 authoring は廃止した。

構成は 2 つある。

- **native 構成** (CEF なし、旧称 Mode A)。下の層をそのまま native 実行する。
  ウィンドウを出さない headless 実行、console、3D action などがここに入る。
- **HTML UI 構成** (CEF あり、旧称 Mode B)。上に CEF host (`include/mitiru/cef/`) と
  JS runtime (`web/mitiru_runtime/`) を載せ、UI と HUD を HTML/CSS で描く。
  bridge は signal-only で、C++ から JS へは state push、JS から C++ へは action event だけが流れる。
  切り替えは `EngineConfig::enableCef`。

JS / JSON / C++ の境界規約は [HYBRID_RUNTIME.md](HYBRID_RUNTIME.md)、
JS runtime モジュールの実体は `web/mitiru_runtime/`。

## Layer Stack

The diagram below shows the full stack of the HTML UI configuration
(with CEF). The native configuration (no CEF) is identical minus the
two layers marked `(CEF only)`. Those sit on top of the
native engine and are inert when `EngineConfig::enableCef = false`.

```
+----------------------------------------------------------+
|  CEF / WEB RUNTIME  (CEF only)                           |
|  web/mitiru_runtime/*.js  -- mitiru.audio / .save / ...  |
|  HTML / CSS UI loaded by CefStartUrl                     |
+----------------------------------------------------------+
|  CEF HOST + JS BRIDGES  (CEF only)                       |
|  mitiru::cef::*  -- MitiruCefContext, AudioBridge,       |
|  StateStore, MitiruCefRenderHandler, ...                 |
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

> **`mitiru::bridge` vs `mitiru::cef`.** These are two unrelated bridge
> families and are easy to confuse:
>
> - **`include/mitiru/bridge/`** — adapters between ShiggyGameCore (`sgc`,
>   the C++ ECS / math / physics / AI library that lives under `external/sgc/`)
>   and the Mitiru type system. Used in **both configurations**. Examples:
>   `AiBridge`, `PhysicsBridge`, `Renderer3DBridge`.
> - **`include/mitiru/cef/`** — the CEF process host plus the JS-facing
>   bridges that marshal calls between the JavaScript runtime and the
>   native engine. **CEF only.** Examples: `MitiruCefContext`,
>   `AudioBridge` (CEF-side, distinct from any sgc-side audio binding).

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
| `render/Renderer3D` | gfx/IDevice (DX12 本命), render/Mesh, render/Camera3D, render/Light | 3D mesh rendering (Phong/Toon/NPR/WBOIT) |
| `render/RenderPipeline2D` | gfx/IDevice, gfx/IBuffer | GPU submission of 2D draw commands |
| `bridge/*` | sgc (ShiggyGameCore) | Adapted APIs for AI, physics, animation, etc. |
| `vn/*` | core/Screen, audio, resource | Visual novel engine (40+ modules) |
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

> **現行:** DLL ゲームは `AudioEngine` を直接呼ばない。`FrameIntents`（エンジンへの依頼を書く欄）に `SoundIntent` を
> 積み（`hud.play("click")`）、host が `SoundIntentRouter` 経由で下記の audio engine を駆動する。
> 下図は host 側 (engine 内部) の経路。

```
Host (SoundIntentRouter)
  |
  +-- AudioEngine::playSE("click.wav")
  |     +-- WaveAudioEngine / SoftAudioEngine / NullAudioEngine
  |
  +-- AudioMixer::play(category, sound)
  |     +-- Category: BGM | SE | Voice
  |     +-- Per-category volume control
  |     +-- Master volume
  |
  +-- MitiruMML (music macro language)
        +-- Parse MML string -> note/rest/tempo events
        +-- SoftSynth generates PCM samples
        +-- Output to Win32AudioOutput / PulseAudio / Null

Audio Output Backends:
  Win32:      Win32AudioOutput (waveOut double-buffered PCM)
  Linux:      PulseAudio (pulse-simple)
  SDL2:       Sdl2Audio (SDL_AudioSpec callback)
  Headless:   NullAudioEngine (no output, state tracking only)
  miniaudio:  MiniaudioEngine (cross-platform fallback)
```

---

## Input Pipeline Flow

> **現行:** DLL ゲームは `InputState` を直接見ない。host が毎フレーム `InputState` から
> POD の `InputSnapshot`（256 キー/マウス/パッド + action event + rngSeed + audioTime）を組んで
> `on_update` に渡す。キー再割り当ては `Game.hpp` の `Binding<Act>`（ゲームの状態 struct に置く）。下図の
> `InputMapper` は非推奨。`InputRecorder/Replayer` 相当は host 側の replay 機構（記録した InputSnapshot 再投入）。

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
InputRecorder / InputReplayer (optional)
  |
  +-- Record input frames to JSON
  +-- Replay input sequences for testing

External Input Injection (HTTP API / test harness):
  InputInjector -> Engine applies to InputState before game.update()
```

---

## Visual Novel System Integration

```
mitiru::vn module (40+ headers)
  |
  +-- ScenarioScript           Parse .vns script DSL
  |     +-- Tokenizer -> Parser -> AST -> Executor
  |     +-- Commands: @bg, @char, @choice, @jump, @set, @if, @wait
  |
  +-- CharacterManager         Manage character sprites/expressions
  +-- BackgroundManager        Background images with transitions
  +-- TransitionEngine         Dissolve, fade, slide, etc.
  +-- MessageWindow            Dialogue display with typewriter effect
  +-- ChoiceUI                 Branching selection interface
  +-- FlagManager              Scenario variable store
  +-- SaveLoadScreen           Save/load slot management
  +-- BacklogUI                Dialogue history viewer
  +-- RichTextEngine           Tags, ruby text, word wrap
  +-- ScreenEffects            Shake, flash, tint, blur
  +-- AchievementSystem        Unlock tracking
  +-- CGGallery                CG collection viewer
  +-- FlowChart                Route visualization
  +-- ConfigScreen             Text speed, volume, display settings
  |
  +-- Integration with core:
        +-- Uses Screen for all 2D rendering
        +-- Uses AudioEngine for BGM/SE/Voice
        +-- Uses InputState for click/key detection
        +-- Uses resource::ImageLoader for texture loading
```

> **CEF-side parallel.** A separate JavaScript implementation lives in
> `web/mitiru_runtime/legacy/mitiru_novel.js` (**legacy。新規使用禁止**)
> and was used by games in the HTML UI configuration that render their VN
> through CEF rather than the native `vn` module. The two implementations are
> intentionally not parity-locked; new work uses the native `vn` module.

---

## Module Descriptions

### Engine (mitiru::core::Engine)
The central coordinator. `Engine::run()` initializes the platform, window, and GPU device according to `EngineConfig`, then drives the main loop: `pollEvents` -> `clock.tick` -> `game.update` -> `sceneManager.currentScene().onUpdate` -> `screen.clear` -> `game.draw` -> `sceneManager.currentScene().onDraw` -> `device.beginFrame` -> `screen.present` -> `device.endFrame`. A headless `stepFrames()` path skips window/GPU setup and activates a software rasterizer inside `Screen` for pixel-level testing.

> **DLL モジュールの場合:** `game.update`/`game.draw` は `Engine::runModule` 内の
> stack-local `ModuleAdapter`。これが host のループを C ABI へ橋渡しし、`InputSnapshot` を組んで
> `on_update(memory, dt, input, intents)` を呼び、`FrameIntents` を drain、`on_draw(memory, Screen*)` を呼ぶ。
> `game.update` を継承クラスで実装する旧 authoring の形は現行 host では使わない。

### Screen (mitiru::Screen)
The drawing surface passed to `Game::draw()`. It accumulates draw commands into `SpriteBatch` (quads/sprites) and `ShapeRenderer` (lines, triangles, circles). On `present()` the accumulated vertex data is forwarded to `RenderPipeline2D` for GPU submission, or rasterized in software when the headless framebuffer is active. Screen has no awareness of the underlying GPU backend.

### ECS (mitiru::ecs / sgc::ecs)
`MitiruWorld` wraps `sgc::ecs::World` and adds string tags, semantic labels, and JSON snapshot support. Systems iterate components via `world.forEach<T>()`. `SystemRunner` holds an ordered list of `ISystem` instances and calls `updateAll()` each frame. `GameWorld` provides a simpler flat entity/component store used by sample games and tests.

### Scene Management (mitiru::scene)
`MitiruSceneManager` maintains a stack of `MitiruScene` objects. `pushScene` / `popScene` / `replaceScene` trigger `onEnter` / `onExit` lifecycle callbacks. The Engine holds a non-owning pointer to the manager and calls `onUpdate` / `onDraw` on the top-of-stack scene each frame after delegating to `Game`.

### Bridge Layer (mitiru::bridge)
Sixteen bridge classes (plus the umbrella `SgcBridge`) adapt subsystems from the `sgc` (ShiggyGameCore) library into Mitiru's type system. Each bridge owns the sgc objects it manages and exposes a clean Mitiru-flavored API. For example, `AiBridge` owns `sgc::bt::Node` trees and `sgc::ai::UtilitySelector` instances, translating `sgc::bt::Status` to `AiState` and forwarding A* calls unchanged. This isolates the rest of the engine from direct sgc type exposure. These bridges are used in **both configurations**. They are unrelated to the CEF-only JS bridges under `include/mitiru/cef/`.

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
| Windows       | Win32Window   | DX12 (明示 fallback: DX11) | Win32AudioOutput / miniaudio |
| Linux/macOS   | GlfwWindow    | Vulkan     | SoftAudioEngine   |
| Linux/macOS   | Sdl2Window    | OpenGL     | Sdl2Audio         |
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
| spdlog | `MITIRU_HAS_SPDLOG` | Structured logging |
| Ozz-Animation | `MITIRU_HAS_OZZ` | Skeletal animation |
| Dear ImGui | `MITIRU_HAS_IMGUI` | Debug overlay (Win32 only) |
| SDL2 | `MITIRU_HAS_SDL2` | SDL2 window/input/audio backend |
| GLFW | `MITIRU_HAS_GLFW` | GLFW window/input backend |
| Vulkan SDK | `MITIRU_HAS_VULKAN` | Vulkan graphics backend |
| OpenGL | `MITIRU_HAS_OPENGL` | OpenGL graphics backend |

All optional dependencies degrade gracefully. When absent, null/stub implementations are used automatically.
