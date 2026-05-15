# Tutorial 2: Build a Flappy Bird Clone

Create a complete Flappy Bird game with sprite rendering, keyboard input, collision detection, scoring, and game-over/restart logic.

---

## Step 1: Create the Project

```bash
cp -r templates/2d_starter ../FlappyClone
cd ../FlappyClone
cmake -B build -DMITIRU_ENGINE_DIR=/path/to/MitiruEngine
cmake --build build --config Debug
```

---

## Step 2: Game Design

Before coding, define the game elements:

- **Bird**: A small rectangle (or sprite) affected by gravity. Spacebar makes it flap upward.
- **Pipes**: Pairs of vertical rectangles with a gap. They scroll left.
- **Score**: Incremented when the bird passes a pipe.
- **Game Over**: Triggered when the bird hits a pipe or the screen boundary.

---

## Step 3: Define Game Constants and Data

Replace the contents of `src/main.cpp`:

```cpp
#include <cmath>
#include <string>
#include <vector>

#include <mitiru/core/Engine.hpp>
#include <mitiru/core/Screen.hpp>

class FlappyClone : public mitiru::Game
{
public:
    void update(float dt) override;
    void draw(mitiru::Screen& screen) override;
    [[nodiscard]] mitiru::Size layout(int, int) override { return {800, 600}; }

private:
    // Constants
    static constexpr float GRAVITY = 800.0f;
    static constexpr float FLAP_IMPULSE = -300.0f;
    static constexpr float PIPE_SPEED = 200.0f;
    static constexpr float PIPE_WIDTH = 60.0f;
    static constexpr float GAP_HEIGHT = 150.0f;
    static constexpr float SPAWN_INTERVAL = 1.8f;
    static constexpr float BIRD_X = 120.0f;
    static constexpr float BIRD_SIZE = 24.0f;

    // Pipe data
    struct Pipe
    {
        float x = 0.0f;
        float gapY = 0.0f;   // Center of the gap
        bool scored = false;
    };

    // State
    float m_birdY = 300.0f;
    float m_birdVelY = 0.0f;
    float m_spawnTimer = 0.0f;
    int m_score = 0;
    bool m_started = false;
    bool m_gameOver = false;
    std::vector<Pipe> m_pipes;

    void resetGame();
    void spawnPipe();
    [[nodiscard]] bool checkCollision(const Pipe& pipe) const;
};
```

---

## Step 4: Implement Input and Physics

```cpp
void FlappyClone::update(float dt)
{
    if (m_gameOver)
    {
        // Press Enter to restart
        if (hasInput() && input().isKeyJustPressed(0x0D))
        {
            resetGame();
        }
        return;
    }

    bool flap = hasInput() &&
        (input().isKeyJustPressed(0x20) ||   // Space
         input().isKeyJustPressed(0x26));     // Up arrow

    // Wait for first flap to start
    if (!m_started)
    {
        if (flap)
        {
            m_started = true;
            m_birdVelY = FLAP_IMPULSE;
        }
        return;
    }

    // Bird physics
    m_birdVelY += GRAVITY * dt;
    m_birdY += m_birdVelY * dt;

    if (flap)
    {
        m_birdVelY = FLAP_IMPULSE;
    }

    // Spawn pipes
    m_spawnTimer += dt;
    if (m_spawnTimer >= SPAWN_INTERVAL)
    {
        m_spawnTimer -= SPAWN_INTERVAL;
        spawnPipe();
    }

    // Update pipes
    for (auto it = m_pipes.begin(); it != m_pipes.end(); )
    {
        it->x -= PIPE_SPEED * dt;

        // Score when bird passes pipe
        if (!it->scored && BIRD_X > it->x + PIPE_WIDTH)
        {
            it->scored = true;
            ++m_score;
        }

        // Collision check
        if (checkCollision(*it))
        {
            m_gameOver = true;
            return;
        }

        // Remove off-screen pipes
        if (it->x + PIPE_WIDTH < 0.0f)
        {
            it = m_pipes.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Floor and ceiling collision
    if (m_birdY < 0.0f || m_birdY + BIRD_SIZE > 600.0f)
    {
        m_gameOver = true;
    }
}
```

---

## Step 5: Collision Detection

AABB (Axis-Aligned Bounding Box) collision between the bird and each pipe segment:

```cpp
bool FlappyClone::checkCollision(const Pipe& pipe) const
{
    // Bird bounds
    const float bLeft = BIRD_X;
    const float bRight = BIRD_X + BIRD_SIZE;
    const float bTop = m_birdY;
    const float bBottom = m_birdY + BIRD_SIZE;

    // Pipe left/right edges
    const float pLeft = pipe.x;
    const float pRight = pipe.x + PIPE_WIDTH;

    // Only check if bird overlaps pipe horizontally
    if (bRight < pLeft || bLeft > pRight)
    {
        return false;
    }

    // Top pipe: from y=0 to gapY - GAP_HEIGHT/2
    const float topPipeBottom = pipe.gapY - GAP_HEIGHT * 0.5f;
    if (bTop < topPipeBottom)
    {
        return true;
    }

    // Bottom pipe: from gapY + GAP_HEIGHT/2 to screen bottom
    const float bottomPipeTop = pipe.gapY + GAP_HEIGHT * 0.5f;
    if (bBottom > bottomPipeTop)
    {
        return true;
    }

    return false;
}
```

---

## Step 6: Pipe Spawning

```cpp
void FlappyClone::spawnPipe()
{
    // Random gap position (avoid edges)
    const float minY = GAP_HEIGHT * 0.5f + 40.0f;
    const float maxY = 600.0f - GAP_HEIGHT * 0.5f - 40.0f;

    // Simple pseudo-random using score and timer
    float t = std::fmod(m_spawnTimer * 1234.5f + static_cast<float>(m_score) * 567.8f, 1.0f);
    float gapY = minY + t * (maxY - minY);

    m_pipes.push_back(Pipe{800.0f, gapY, false});
}

void FlappyClone::resetGame()
{
    m_birdY = 300.0f;
    m_birdVelY = 0.0f;
    m_spawnTimer = 0.0f;
    m_score = 0;
    m_started = false;
    m_gameOver = false;
    m_pipes.clear();
}
```

---

## Step 7: Rendering

```cpp
void FlappyClone::draw(mitiru::Screen& screen)
{
    // Sky background
    screen.clear({0.3f, 0.6f, 0.9f, 1.0f});

    const int w = screen.width();
    const int h = screen.height();

    // Ground
    screen.fillRect(0, h - 40, w, 40, {0.4f, 0.7f, 0.2f, 1.0f});
    screen.fillRect(0, h - 44, w, 4, {0.3f, 0.5f, 0.15f, 1.0f});

    // Pipes
    for (const auto& pipe : m_pipes)
    {
        const float topBottom = pipe.gapY - GAP_HEIGHT * 0.5f;
        const float botTop = pipe.gapY + GAP_HEIGHT * 0.5f;

        // Top pipe
        screen.fillRect(
            static_cast<int>(pipe.x), 0,
            static_cast<int>(PIPE_WIDTH), static_cast<int>(topBottom),
            {0.2f, 0.7f, 0.3f, 1.0f});

        // Top pipe cap
        screen.fillRect(
            static_cast<int>(pipe.x) - 4, static_cast<int>(topBottom) - 16,
            static_cast<int>(PIPE_WIDTH) + 8, 16,
            {0.25f, 0.8f, 0.35f, 1.0f});

        // Bottom pipe
        screen.fillRect(
            static_cast<int>(pipe.x), static_cast<int>(botTop),
            static_cast<int>(PIPE_WIDTH), h - static_cast<int>(botTop),
            {0.2f, 0.7f, 0.3f, 1.0f});

        // Bottom pipe cap
        screen.fillRect(
            static_cast<int>(pipe.x) - 4, static_cast<int>(botTop),
            static_cast<int>(PIPE_WIDTH) + 8, 16,
            {0.25f, 0.8f, 0.35f, 1.0f});
    }

    // Bird
    screen.fillRect(
        static_cast<int>(BIRD_X), static_cast<int>(m_birdY),
        static_cast<int>(BIRD_SIZE), static_cast<int>(BIRD_SIZE),
        {1.0f, 0.8f, 0.1f, 1.0f});

    // Bird eye
    screen.fillRect(
        static_cast<int>(BIRD_X) + 16, static_cast<int>(m_birdY) + 4,
        5, 5, {1.0f, 1.0f, 1.0f, 1.0f});
    screen.fillRect(
        static_cast<int>(BIRD_X) + 18, static_cast<int>(m_birdY) + 6,
        3, 3, {0.0f, 0.0f, 0.0f, 1.0f});

    // Score display
    screen.fillRect(w / 2 - 30, 16, 60, 32, {0.0f, 0.0f, 0.0f, 0.4f});
    screen.drawText({static_cast<float>(w / 2 - 16), 20.0f},
        std::to_string(m_score), {1, 1, 1, 1}, 24.0f);

    // Start prompt
    if (!m_started)
    {
        screen.fillRect(w / 2 - 120, h / 2 - 20, 240, 40,
            {0.0f, 0.0f, 0.0f, 0.6f});
        screen.drawText({static_cast<float>(w / 2 - 100),
                         static_cast<float>(h / 2 - 8)},
            "Press SPACE to start", {1, 1, 1, 1}, 16.0f);
    }

    // Game over screen
    if (m_gameOver)
    {
        screen.fillRect(0, 0, w, h, {0.0f, 0.0f, 0.0f, 0.5f});

        screen.fillRect(w / 2 - 100, h / 2 - 50, 200, 100,
            {0.1f, 0.1f, 0.15f, 0.9f});
        screen.drawRect(w / 2 - 100, h / 2 - 50, 200, 100,
            {0.5f, 0.5f, 0.7f, 0.8f});

        screen.drawText({static_cast<float>(w / 2 - 50),
                         static_cast<float>(h / 2 - 36)},
            "GAME OVER", {1.0f, 0.3f, 0.3f, 1.0f}, 24.0f);

        screen.drawText({static_cast<float>(w / 2 - 40),
                         static_cast<float>(h / 2 - 4)},
            "Score: " + std::to_string(m_score), {1, 1, 1, 1}, 16.0f);

        screen.drawText({static_cast<float>(w / 2 - 60),
                         static_cast<float>(h / 2 + 20)},
            "Enter to restart", {0.7f, 0.7f, 0.7f, 1.0f}, 16.0f);
    }
}
```

---

## Step 8: Entry Point

```cpp
int main()
{
    mitiru::EngineConfig config;
    config.title = "Flappy Clone";
    config.windowWidth = 800;
    config.windowHeight = 600;

    mitiru::Engine engine;
    FlappyClone game;
    engine.run(game, config);
    return 0;
}
```

---

## Step 9: Build and Run

```bash
cmake --build build --config Debug
./build/Debug/FlappyClone.exe
```

**Controls:**
- **Space / Up** -- Flap
- **Enter** -- Restart after game over
- **F12** -- Toggle debug overlay (Windows)

---

## Key Concepts Covered

| Concept | Where Used |
|---------|-----------|
| `mitiru::Game` subclass | Main game class |
| `Screen::fillRect()` | Drawing sprites, UI, backgrounds |
| `Screen::drawText()` | Score display, prompts |
| `InputState::isKeyJustPressed()` | Edge-detected input |
| AABB collision | Bird vs pipe detection |
| Game state machine | started/playing/game-over |
| `EngineConfig` | Window size, title |

---

## Exercises

1. **Add sound effects** using `mitiru::audio::AudioEngine` for flap and collision
2. **Replace rectangles with sprites** using `Screen::drawSprite()`
3. **Add parallax scrolling** backgrounds at different speeds
4. **Implement a high score** that persists between game sessions
5. **Add difficulty scaling** -- pipes get faster and gaps get smaller over time

---

## Next Steps

- [Tutorial 3: Your First 3D Scene](03_3d_scene.md)
- [Tutorial 4: Simple Multiplayer Pong](04_multiplayer.md)
- [Tutorial 5: Entity Component System Guide](05_ecs_basics.md)
