# MitiruEngine Troubleshooting

Common issues and their solutions.

---

## Build Errors

### CMake: "Could not find preset 'default'"

**Cause:** CMake version is too old to support presets.

**Fix:** Update CMake to 3.21 or later.

```bash
cmake --version   # Must be 3.21+
```

On Ubuntu: `sudo snap install cmake --classic` or download from cmake.org.

---

### CMake: "In-source builds are not allowed"

**Cause:** Running cmake from the source directory without a build directory.

**Fix:** Always use the preset or specify a build directory:

```bash
cmake --preset default           # Uses build/ directory
# OR
cmake -B build .                 # Explicit build directory
```

If you already generated files in-source, remove them:

```bash
rm -f CMakeCache.txt
rm -rf CMakeFiles/
```

---

### MSVC: "fatal error C1128: number of sections exceeded object file format limit"

**Cause:** Large translation units with many template instantiations.

**Fix:** Add `/bigobj` to compile options:

```cmake
target_compile_options(MyTarget PRIVATE /bigobj)
```

This is already set for SampleLauncher and MitiruHub in the engine's CMakeLists.txt.

---

### MSVC: "error C2220: warning treated as error" or UTF-8 encoding issues

**Cause:** Source files contain non-ASCII characters without `/utf-8` flag.

**Fix:** Ensure `/utf-8` is in your compile options:

```cmake
if(MSVC)
    target_compile_options(MyTarget PRIVATE /utf-8 /FS)
endif()
```

---

### GCC/Clang: "error: 'xxx' is not a member of 'std'"

**Cause:** Missing C++20 standard flag or older compiler version.

**Fix:**
1. Ensure C++20 is set: `set(CMAKE_CXX_STANDARD 20)` in CMakeLists.txt
2. Use GCC 13+ or Clang 18+
3. Check: `g++ --version` / `clang++ --version`

---

### Linker: "unresolved external symbol" for D3D functions

**Cause:** Missing DirectX libraries on link line.

**Fix:** Add the required Windows libraries:

```cmake
if(WIN32)
    target_link_libraries(MyTarget PRIVATE
        d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
        ws2_32.lib winmm.lib xinput.lib)
endif()
```

---

### Submodule errors: "external/sgc is empty"

**Cause:** Git submodules were not initialized.

**Fix:**

```bash
git submodule update --init --recursive
```

If that fails, try:

```bash
git submodule deinit -f external/sgc
git submodule update --init external/sgc
```

---

### Linux: "pulse/simple.h: No such file or directory"

**Cause:** PulseAudio development headers not installed.

**Fix:**

```bash
# Ubuntu/Debian
sudo apt install libpulse-dev

# Fedora
sudo dnf install pulseaudio-libs-devel
```

If you do not need audio output, the engine will fall back to `NullAudioOutput` automatically.

---

## Shader Compilation Failures

### "D3DCompile failed" or "error X3000"

**Cause:** HLSL shader source has syntax errors or the shader compiler (d3dcompiler_47.dll) is missing.

**Fix:**
1. Ensure the Windows SDK is installed (bundled with Visual Studio Build Tools 2022 or the full IDE)
2. Check that `d3dcompiler.lib` is linked
3. For custom shaders, validate syntax in any HLSL-aware editor (VS Code + HLSL extension, or the full Visual Studio IDE if you happen to have it)
4. The engine's built-in shaders (in `DefaultShaders3D.hpp`, `ToonShaders3D.hpp`, `NPRShaders3D.hpp`) are embedded as string constants -- if these fail, it likely indicates a driver or SDK issue

### Shader mode switch causes crash

**Cause:** Switching `ShaderMode3D` triggers shader recompilation. If the GPU driver does not support the target shader model, this can fail.

**Fix:**
- Update GPU drivers to the latest version
- Fall back to `ShaderMode3D::Phong` which has the broadest compatibility
- Check `renderer3D()->isInitialized()` before calling draw methods

---

## No Audio Output

### No sound on Windows

**Possible causes:**
1. Audio device busy or disabled in Windows settings
2. `NullAudioEngine` selected instead of `WaveAudioEngine`

**Fix:**
1. Check Windows Sound settings (right-click speaker icon -> Sound Settings)
2. Ensure you are not running in headless mode (`config.headless = false`)
3. For MML playback, check that `external/mml` submodule is initialized

### No sound on Linux

**Fix:**
1. Install PulseAudio: `sudo apt install pulseaudio libpulse-dev`
2. Ensure PulseAudio is running: `pulseaudio --check`
3. If PulseAudio is not available, audio will use `NullAudioOutput` silently

---

## Black Screen / No Rendering

### Window opens but screen is entirely black

**Possible causes:**
1. `Screen::clear()` is called but no draw calls follow
2. `RenderPipeline2D` not initialized (no GPU device)
3. Screen resolution mismatch

**Fix:**
1. Verify your `draw()` method calls drawing functions after `screen.clear()`
2. Check `Engine` output -- if it says "NullDevice", GPU backend was not found
3. Ensure `layout()` returns a reasonable size (e.g., `{1280, 720}`)

### 3D objects not visible

**Possible causes:**
1. Camera positioned inside the object or pointing away
2. Near/far clip planes exclude the object
3. `Renderer3D` not initialized (non-Windows or NullDevice)

**Fix:**
1. Set camera far enough: `Camera3D({0, 5, -10}, {0, 0, 0}, ...)`
2. Use near=0.1, far=100.0 as safe defaults
3. Check `hasRenderer3D()` before calling 3D functions
4. Use the wireframe fallback in the 3D starter template for non-GPU environments

### DX12 rendering shows corruption or flicker

**Fix:**
1. Update GPU drivers
2. Try DX11 backend: `config.gfxBackend = mitiru::gfx::Backend::Dx11;`
3. Check that fence synchronization is working (DX12 requires explicit GPU sync)

---

## Text Rendering Issues

### Text overflow / text not visible

**Cause:** Text drawn outside the screen bounds, or font size too large for the area.

**Fix:**
1. Use `screen.drawTextClipped()` to auto-truncate with ellipsis
2. Use `screen.drawTextInRect()` for aligned text within a bounding box
3. Check coordinates: `drawTextInRect({x, y, w, h}, ...)` — rect の左上が (x, y)
4. Measure before drawing: `auto size = screen.measureText(text, fontSize);`

### Japanese / Unicode text not displayed

**Cause:** The built-in `BitmapFont` only supports ASCII (printable range 0x20-0x7E).

**Fix:**
- For Japanese text, use `mitiru::vn::TrueTypeFont` with a TTF file that includes Japanese glyphs
- Or use `mitiru::render::DxTextRenderer` (Windows only, DirectWrite-based)
- Or use `mitiru::render::TrueTypeRenderer` with stb_truetype

---

## DX12-Specific Issues

### "CreateCommittedResource failed" or device removed

**Cause:** GPU memory exhaustion or incompatible hardware.

**Fix:**
1. Close other GPU-intensive applications
2. Reduce window resolution: `config.windowWidth = 800; config.windowHeight = 600;`
3. Fall back to DX11: `config.gfxBackend = mitiru::gfx::Backend::Dx11;`

### "ID3D12CommandAllocator::Reset failed"

**Cause:** Command allocator reset before GPU finished executing.

**Fix:** This is an engine-internal synchronization issue. Ensure you are using the latest version. As a workaround, switch to DX11.

### DX12 validation layer errors (debug build)

**Cause:** D3D12 debug layer enabled and reporting API misuse.

**Fix:**
1. These are warnings, not crashes -- they help find bugs
2. Install the "Graphics Tools" optional feature in Windows Settings
3. For release builds, the debug layer is disabled automatically

---

## ImGui / F12 Debug Overlay

### F12 does nothing

**Cause:** ImGui is only available on Windows with DX11 backend and vendored ImGui headers present.

**Fix:**
1. Check that `external/vendored/imgui/imgui.h` exists
2. Ensure building on Windows (`MITIRU_HAS_IMGUI` must be defined)
3. Ensure not using DX12 or Null backend (ImGui requires DX11 currently)

### ImGui overlay appears but is empty

**Cause:** `Game::drawImGui()` not overridden or empty.

**Fix:** Override `drawImGui()` in your game class and add ImGui widgets:

```cpp
void drawImGui() override
{
    ImGui::Begin("Debug");
    ImGui::Text("Hello from ImGui!");
    ImGui::End();
}
```

---

## Network Issues

### "TcpTransport::listen() returns false"

**Cause:** Port already in use or insufficient permissions.

**Fix:**
1. Use a different port or port 0 (auto-assign): `transport.listen(0);`
2. Get the assigned port: `transport.getLocalPort();`
3. On Linux, ports below 1024 require root

### "connect() failed"

**Fix:**
1. Ensure the server is running and listening
2. Check firewall rules (Windows Firewall may block the connection)
3. Use `127.0.0.1` for local testing, not `localhost` (DNS resolution issues)

---

## Performance Issues

### Low frame rate (< 30 FPS)

**Possible causes:**
1. Too many draw calls per frame
2. Large number of entities being iterated
3. Post-processing enabled with large resolution

**Fix:**
1. Check `screen.drawCallCount()` -- keep under 1000 for 60fps
2. Use batched rendering: `SpriteBatch` / `GpuSpriteBatch`
3. Disable post-processing: `postProcess()->setEnabled(false)`
4. Use `EngineConfig::targetTps = 60.0f` for fixed timestep
5. Profile with Tracy (if available): `MITIRU_HAS_TRACY=1`

### Memory growing over time

**Fix:**
1. Ensure destroyed ECS entities are cleaned up: `world.destroyEntity(e)`
2. Check for unbounded `std::vector` growth in update loops
3. Use `MitiruWorld::entityCount()` to monitor entity count

---

## Common Patterns for Debugging

### Enable headless mode for testing

```cpp
mitiru::EngineConfig config;
config.headless = true;

mitiru::Engine engine;
engine.stepFrames(game, 100, config);  // Run 100 frames without window
auto pixels = engine.capture();         // Get screenshot as RGBA bytes
```

### Enable HTTP API for remote inspection

```cpp
config.enableHttpApi = true;
config.httpApiPort = 8090;
```

Then query: `curl http://localhost:8090/snapshot` for JSON state.

### Get engine state as JSON

```cpp
std::string json = engine.snapshot();
// Returns: {"frameNumber":42,"timestamp":0.7,"entityCount":15,...}
```

---

## Getting Help

1. Check the [Getting Started](GETTING_STARTED.md) guide for setup
2. Browse the [Architecture](ARCHITECTURE.md) doc for system understanding
3. Look at `examples/` for the chapter samples (welcome, shapes, text, input, …)
4. Double-click `MitiruEngine_Launcher.bat` (in the release zip) to run the bundled samples
5. Full guide and tutorial: https://mogmog-0110.github.io/MitiruEngine/
