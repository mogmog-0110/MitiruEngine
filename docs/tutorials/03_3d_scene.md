# Tutorial 3: Your First 3D Scene

Create a 3D scene with a rotating mesh, camera controls, lighting, materials, and post-processing effects using MitiruEngine's rendering system.

---

## Step 1: Create the Project

```bash
cp -r templates/3d_starter ../My3DScene
cd ../My3DScene
cmake -B build -DMITIRU_ENGINE_DIR=/path/to/MitiruEngine
cmake --build build --config Debug
```

Run it to see a wireframe cube: `./build/Debug/My3DGame.exe`

---

## Step 2: Understanding 3D Rendering in Mitiru

MitiruEngine provides two 3D rendering paths:

| Renderer | Backend | Class |
|----------|---------|-------|
| Renderer3D | DX11 | `mitiru::render::Renderer3D` |
| Renderer3D_DX12 | DX12 | `mitiru::render::Renderer3D_DX12` |

Both are automatically initialized by the `Engine` and accessible via `renderer3D()` and `renderer3D_DX12()` in your `Game` subclass. The template includes a 2D wireframe fallback for platforms without GPU support.

---

## Step 3: Create a Mesh

MitiruEngine includes factory methods for common primitives:

```cpp
#include <mitiru/render/Mesh.hpp>

// Built-in primitives
auto cube = mitiru::render::Mesh::createCube(2.0f);
auto sphere = mitiru::render::Mesh::createSphere(1.0f, 32);
auto plane = mitiru::render::Mesh::createPlane(10.0f, 10.0f);
```

### Loading OBJ Files

```cpp
#include <mitiru/render/ObjLoader.hpp>

auto result = mitiru::render::ObjLoader::load("assets/models/character.obj");
if (result.has_value())
{
    m_mesh = std::move(result.value());
}
```

The OBJ loader supports:
- Vertex positions (`v`)
- Texture coordinates (`vt`)
- Normals (`vn`)
- Face formats: `v`, `v/vt`, `v/vt/vn`, `v//vn`

---

## Step 4: Set Up Camera and Lights

```cpp
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Light.hpp>

class My3DScene : public mitiru::Game
{
public:
    My3DScene()
    {
        // Camera: position, target, up, fov, aspect, near, far
        m_camera = mitiru::render::Camera3D(
            {0.0f, 3.0f, -6.0f},   // Eye position
            {0.0f, 0.0f, 0.0f},    // Look-at target
            {0.0f, 1.0f, 0.0f},    // Up vector
            0.785f,                  // FOV (45 degrees in radians)
            1280.0f / 720.0f,        // Aspect ratio
            0.1f,                    // Near clip
            100.0f                   // Far clip
        );

        // Directional light (sunlight)
        m_light = mitiru::render::Light::directional(
            {0.3f, -1.0f, 0.5f},                      // Direction
            {1.0f, 0.95f, 0.9f, 1.0f}                 // Warm white color
        );

        // Create mesh
        m_cubeMesh = mitiru::render::Mesh::createCube(2.0f);
        m_floorMesh = mitiru::render::Mesh::createPlane(20.0f, 20.0f);
    }

private:
    mitiru::render::Camera3D m_camera;
    mitiru::render::Light m_light;
    mitiru::render::Mesh m_cubeMesh;
    mitiru::render::Mesh m_floorMesh;
    float m_rotation = 0.0f;
    float m_cameraAngle = 0.0f;
};
```

### Light Types

```cpp
// Directional light (sun)
auto sun = mitiru::render::Light::directional({0, -1, 0.5f});

// Point light (lamp)
auto lamp = mitiru::render::Light::point({5, 3, 0}, 50.0f);

// Spot light
auto spot = mitiru::render::Light::spot(
    {0, 10, 0},     // Position
    {0, -1, 0},     // Direction
    30.0f,           // Range
    45.0f            // Cone angle (degrees)
);
```

---

## Step 5: Apply Materials

```cpp
#include <mitiru/render/Material.hpp>

// Default gray material
auto defaultMat = mitiru::render::Material::defaultMaterial();

// Custom colored material
mitiru::render::Material redMat;
redMat.ambient  = {0.1f, 0.02f, 0.02f, 1.0f};
redMat.diffuse  = {0.8f, 0.2f, 0.1f, 1.0f};
redMat.specular = {1.0f, 0.8f, 0.8f, 1.0f};
redMat.shininess = 64.0f;

// PBR-style material
mitiru::render::Material metalMat;
metalMat.diffuse   = {0.7f, 0.7f, 0.7f, 1.0f};
metalMat.metallic  = 0.9f;
metalMat.roughness = 0.2f;

// Floor material
mitiru::render::Material floorMat;
floorMat.ambient  = {0.05f, 0.08f, 0.05f, 1.0f};
floorMat.diffuse  = {0.3f, 0.5f, 0.3f, 1.0f};
floorMat.specular = {0.2f, 0.2f, 0.2f, 1.0f};
floorMat.shininess = 8.0f;
```

---

## Step 6: Camera Controls and Object Rotation

```cpp
void My3DScene::update(float dt)
{
    // Rotate the cube
    m_rotation += dt * 1.0f;

    // Camera orbit controls
    if (hasInput())
    {
        const float camSpeed = 2.0f * dt;
        if (input().isKeyDown(0x41)) m_cameraAngle -= camSpeed;  // A: orbit left
        if (input().isKeyDown(0x44)) m_cameraAngle += camSpeed;  // D: orbit right

        // Zoom with W/S
        float dist = 6.0f;
        if (input().isKeyDown(0x57)) dist -= 2.0f;  // W: closer
        if (input().isKeyDown(0x53)) dist += 2.0f;  // S: farther

        // Update camera position
        m_camera.setPosition({
            std::sin(m_cameraAngle) * dist,
            3.0f,
            -std::cos(m_cameraAngle) * dist
        });
    }
}
```

---

## Step 7: Draw the 3D Scene

```cpp
void My3DScene::draw(mitiru::Screen& screen)
{
    screen.clear({0.05f, 0.05f, 0.08f, 1.0f});

    if (hasRenderer3D())
    {
        auto* r = renderer3D();

        // Begin 3D frame
        r->beginFrame({0.1f, 0.12f, 0.18f, 1.0f});

        // Set camera and light
        r->setCamera(m_camera);
        r->setLight(m_light);

        // Draw rotating cube
        sgc::Mat4f cubeWorld = sgc::Mat4f::rotationY(m_rotation);
        cubeWorld = cubeWorld * sgc::Mat4f::translation(0.0f, 1.0f, 0.0f);
        r->drawMesh(m_cubeMesh, cubeWorld, m_redMaterial);

        // Draw floor
        sgc::Mat4f floorWorld = sgc::Mat4f::translation(0.0f, 0.0f, 0.0f);
        r->drawMesh(m_floorMesh, floorWorld, m_floorMaterial);

        // End 3D frame
        r->endFrame();
    }
    else
    {
        // Fallback: 2D wireframe representation
        drawWireframeFallback(screen);
    }

    // HUD overlay (2D on top of 3D)
    screen.fillRect(8, 8, 200, 24, {0.0f, 0.0f, 0.0f, 0.5f});
    screen.drawText({12.0f, 12.0f}, "WASD: Camera  |  F12: Debug",
        {0.7f, 0.7f, 0.7f, 1.0f}, 16.0f);
}
```

---

## Step 8: Shader Modes

MitiruEngine supports 10 shader modes that can be switched at runtime:

```cpp
// Available modes
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Phong);      // Realistic
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Toon);       // Cel-shaded
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Unlit);      // No lighting
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Flat);       // Flat 2-tone
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Posterize);  // Poster-style
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Halftone);   // Dot pattern
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Hatching);   // Pen-sketch
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::GradientMap);// Storybook
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Silhouette); // Shadow-play
renderer3D()->setShaderMode(mitiru::render::ShaderMode3D::Watercolor); // Watercolor
```

Add a key to cycle through them:

```cpp
if (hasInput() && input().isKeyJustPressed(0x4D))  // M key
{
    int mode = static_cast<int>(renderer3D()->shaderMode());
    mode = (mode + 1) % 10;
    renderer3D()->setShaderMode(static_cast<mitiru::render::ShaderMode3D>(mode));
}
```

---

## Step 9: Post-Processing Effects

On Windows with the DX11 backend, post-processing is available:

```cpp
void My3DScene::drawImGui()
{
#ifdef _WIN32
    // Access post-process from engine (set in constructor or update)
    // Example: enable bloom
    if (auto* pp = m_postProcess)
    {
        pp->setEnabled(true);
        // Configure bloom, vignette, color grading, etc.
    }
#endif
}
```

Software post-processing is available via `RenderTexture`:

```cpp
#include <mitiru/render/PostEffects.hpp>
#include <mitiru/render/RenderTexture.hpp>

mitiru::render::RenderTexture rt(800, 600);

// Apply bloom
mitiru::render::PostEffects::bloom(rt, 0.7f, 0.5f, 3);

// Apply vignette
mitiru::render::PostEffects::vignette(rt, 0.3f);
```

---

## Step 10: Build and Run

```bash
cmake --build build --config Debug
./build/Debug/My3DGame.exe
```

**Controls:**
- **A/D** -- Orbit camera left/right
- **W/S** -- Zoom in/out
- **M** -- Cycle shader mode
- **F12** -- Toggle debug overlay

---

## Key Classes Reference

| Class | Header | Purpose |
|-------|--------|---------|
| `Renderer3D` | `render/Renderer3D.hpp` | DX11 3D rendering |
| `Renderer3D_DX12` | `render/Renderer3D_DX12.hpp` | DX12 3D rendering |
| `Camera3D` | `render/Camera3D.hpp` | Perspective camera |
| `Light` | `render/Light.hpp` | Directional/point/spot lights |
| `Material` | `render/Material.hpp` | Surface properties (Phong + PBR) |
| `Mesh` | `render/Mesh.hpp` | Vertex/index data + primitives |
| `ObjLoader` | `render/ObjLoader.hpp` | Wavefront OBJ file loader |
| `PostEffects` | `render/PostEffects.hpp` | Bloom, vignette, color grading |

---

## Next Steps

- Load complex models with `ObjLoaderTiny` (extended OBJ support)
- Add skeletal animation with `SkeletalAnimation` (requires Ozz-Animation)
- Use `Scene3D` for managing multiple objects
- Add shadow mapping with `ShadowPass3D`
- Explore deferred rendering with `DeferredPipeline`
- See [Tutorial 4: Simple Multiplayer Pong](04_multiplayer.md)
