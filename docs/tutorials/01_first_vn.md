# Tutorial 1: Your First Visual Novel in 10 Minutes

Build a working visual novel with dialogue, characters, and branching choices using MitiruEngine's VN module.

---

## Step 1: Create the Project

```bash
cp -r templates/vn_starter ../MyVisualNovel
cd ../MyVisualNovel
cmake -B build -DMITIRU_ENGINE_DIR=/path/to/MitiruEngine
cmake --build build --config Debug
```

Run it to confirm: `./build/Debug/MyVisualNovel.exe`

You should see a dialogue window with text appearing character by character.

---

## Step 2: Understand the Game Structure

Every MitiruEngine game inherits from `mitiru::Game` and implements three methods:

```cpp
class MyGame : public mitiru::Game
{
public:
    void update(float dt) override;                            // Logic
    void draw(mitiru::Screen& screen) override;                // Rendering
    [[nodiscard]] mitiru::Size layout(int w, int h) override;  // Resolution
};
```

The entry point creates an `Engine`, a `Game`, and calls `engine.run()`:

```cpp
int main()
{
    mitiru::EngineConfig config;
    config.title = "MyVisualNovel";
    config.windowWidth = 1280;
    config.windowHeight = 720;

    mitiru::Engine engine;
    MyGame game;
    engine.run(game, config);
    return 0;
}
```

---

## Step 3: Write a Scenario Script

MitiruEngine includes a full scenario scripting system. Create a scenario as a string constant (or load from file):

```cpp
#include <mitiru/vn/ScenarioScript.hpp>

static constexpr const char* SCENARIO = R"(
@scene "chapter1"
@bg "school_gate.png" fade 1.0
@bgm "morning_theme.ogg"

@char "Sakura" center show fade
Sakura "Good morning! You're here early today."

@char "Player" left show fade
Player "I wanted to get a head start on the festival prep."

Sakura "That's the spirit! Let me show you the classroom."

@choice
  "Sounds great!" -> enthusiastic
  "I'm a bit tired..." -> reluctant
@endchoice

@label enthusiastic
@set flag_enthusiastic true
Sakura "I love your energy! Let's go!"
@jump hallway

@label reluctant
Sakura "Come on, it'll be fun! I promise."
@jump hallway

@label hallway
@bg "hallway.png" dissolve 0.5
Sakura "Here we are. The decorations are already started."
)";
```

### Script Commands Reference

| Command | Syntax | Description |
|---------|--------|-------------|
| `@scene` | `@scene "name"` | Declare a scene/chapter |
| `@bg` | `@bg "file" [transition] [duration]` | Set background image |
| `@bgm` | `@bgm "file"` | Play background music |
| `@se` | `@se "file"` | Play sound effect |
| `@char` | `@char "name" [pos] show/hide [transition]` | Show/hide character |
| `@choice` | `@choice ... @endchoice` | Present branching choices |
| `@label` | `@label name` | Define a jump target |
| `@jump` | `@jump name` | Jump to a label |
| `@set` | `@set var value` | Set a scenario flag |
| `@if` | `@if condition ... @endif` | Conditional block |
| `@wait` | `@wait duration` | Pause for N seconds |
| Dialogue | `Speaker "text"` | Display dialogue line |

---

## Step 4: Parse and Execute the Script

```cpp
#include <mitiru/vn/ScenarioScript.hpp>
#include <mitiru/vn/FlagManager.hpp>
#include <mitiru/vn/CharacterManager.hpp>

class MyVNGame : public mitiru::Game
{
public:
    MyVNGame()
    {
        // Parse the scenario
        m_script.parse(SCENARIO);

        // Register characters
        m_characters.registerCharacter("Sakura", {
            .displayName = "Sakura",
            .defaultExpression = "normal"
        });
        m_characters.registerCharacter("Player", {
            .displayName = "You",
            .defaultExpression = "normal"
        });
    }

    void update(float dt) override
    {
        m_textTimer += dt;

        if (hasInput())
        {
            if (input().isKeyJustPressed(0x0D) ||  // Enter
                input().isMouseButtonJustPressed(mitiru::MouseButton::Left))
            {
                if (isTextComplete())
                {
                    advanceScript();
                }
                else
                {
                    m_textTimer = 100.0f;  // Instant-display
                }
            }
        }
    }
    // ... (draw and layout below)
```

---

## Step 5: Draw the VN Screen

```cpp
    void draw(mitiru::Screen& screen) override
    {
        screen.clear({0.02f, 0.02f, 0.04f, 1.0f});
        const int w = screen.width();
        const int h = screen.height();

        // Background gradient (placeholder for @bg images)
        for (int y = 0; y < h; ++y)
        {
            float t = static_cast<float>(y) / static_cast<float>(h);
            screen.fillRect(0, y, w, 1,
                {0.08f + t * 0.04f, 0.05f + t * 0.03f, 0.12f + t * 0.06f, 1.0f});
        }

        // Character sprite area (placeholder rectangle)
        screen.fillRect(w / 2 - 60, h / 2 - 120, 120, 240,
            {0.3f, 0.3f, 0.4f, 0.3f});

        // Dialogue box
        const int boxH = h / 3;
        const int boxY = h - boxH;
        const int pad = 24;

        // Box background
        screen.fillRect(pad, boxY, w - pad * 2, boxH - pad,
            {0.0f, 0.0f, 0.0f, 0.75f});

        // Box border
        screen.drawRect(pad, boxY, w - pad * 2, boxH - pad,
            {0.4f, 0.5f, 0.7f, 0.6f});

        // Speaker name plate
        if (!m_currentSpeaker.empty())
        {
            screen.fillRect(pad + 12, boxY - 14, 140, 28,
                {0.15f, 0.2f, 0.35f, 0.9f});
            screen.drawText({static_cast<float>(pad + 18),
                             static_cast<float>(boxY - 10)},
                m_currentSpeaker, {0.8f, 0.9f, 1.0f, 1.0f}, 16.0f);
        }

        // Dialogue text with typewriter effect
        if (!m_currentText.empty())
        {
            int visibleChars = std::min(
                static_cast<int>(m_currentText.size()),
                static_cast<int>(m_textTimer * 30.0f));
            std::string visible = m_currentText.substr(0, visibleChars);
            screen.drawText({static_cast<float>(pad + 20),
                             static_cast<float>(boxY + 24)},
                visible, {1.0f, 1.0f, 1.0f, 0.95f}, 16.0f);
        }

        // Blinking continue indicator
        if (isTextComplete())
        {
            if (static_cast<int>(m_textTimer * 2.0f) % 2 == 0)
            {
                screen.fillRect(w - pad - 30, boxY + boxH - pad - 16,
                    12, 12, {0.6f, 0.7f, 0.9f, 0.8f});
            }
        }
    }

    [[nodiscard]] mitiru::Size layout(int, int) override
    {
        return {1280, 720};
    }
```

---

## Step 6: Add Character Sprites

To load actual images, use the resource system:

```cpp
#include <mitiru/resource/ImageLoader.hpp>

// In your game initialization:
auto spriteData = mitiru::resource::ImageLoader::loadFromFile("assets/sakura_normal.png");
```

For the VN module's texture manager:

```cpp
#include <mitiru/vn/VNTextureManager.hpp>

mitiru::vn::VNTextureManager textures;
textures.registerTexture("sakura_normal", "assets/characters/sakura_normal.png");
textures.registerTexture("school_gate", "assets/backgrounds/school_gate.png");
```

Organize your assets directory:

```
assets/
├── characters/
│   ├── sakura_normal.png
│   ├── sakura_happy.png
│   └── sakura_sad.png
├── backgrounds/
│   ├── school_gate.png
│   └── hallway.png
└── audio/
    ├── morning_theme.ogg
    └── click.wav
```

---

## Step 7: Add Background Images

Use the `BackgroundManager` for smooth transitions between backgrounds:

```cpp
#include <mitiru/vn/BackgroundManager.hpp>

mitiru::vn::BackgroundManager bgManager;

// Set a background (called when processing @bg commands)
bgManager.setBackground("school_gate.png");

// With transition
bgManager.setBackground("hallway.png",
    mitiru::vn::TransitionType::Dissolve, 0.5f);

// Update each frame
bgManager.update(dt);
```

---

## Step 8: Test with F12 Editor

Run your game and press **F12** to open the debug overlay. Override `drawImGui()` to add VN-specific debug tools:

```cpp
void drawImGui() override
{
#ifdef MITIRU_HAS_IMGUI
    ImGui::Begin("VN Debug");
    ImGui::Text("Current line: %d", m_script.currentLine());
    ImGui::Text("Speaker: %s", m_currentSpeaker.c_str());

    if (ImGui::Button("Skip to next choice"))
    {
        // Skip forward
    }

    // Display all flags
    ImGui::Separator();
    ImGui::Text("Flags:");
    for (const auto& [key, val] : m_flags.allFlags())
    {
        ImGui::Text("  %s = %s", key.c_str(), val.c_str());
    }
    ImGui::End();
#endif
}
```

---

## Step 9: Build and Run

```bash
cmake --build build --config Debug
./build/Debug/MyVisualNovel.exe
```

---

## Full Source Code

See `templates/vn_starter/src/main.cpp` for the complete working template.

---

## Next Steps

- Explore the 40+ VN modules listed in `include/mitiru/vn/VN.hpp`
- Add choices with `@choice` / `@endchoice` blocks
- Implement save/load with `mitiru::vn::SaveLoadScreen`
- Add screen effects with `mitiru::vn::ScreenEffects`
- Add a CG gallery with `mitiru::vn::CGGallery`
- See [Tutorial 2: Build a Flappy Bird Clone](02_arcade_game.md) for 2D game development
