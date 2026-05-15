# Tutorial 5: Entity Component System Guide

Learn how to use MitiruEngine's ECS to build data-driven game logic with entities, components, systems, and queries.

---

## Why ECS?

Traditional OOP:

```
Player (class)
├── position, velocity       (data)
├── sprite, animation        (data)
├── health, score            (data)
├── move(), attack(), draw() (logic)
└── tightly coupled, hard to extend
```

ECS approach:

```
Entity 42 (just an ID)
├── TransformComponent  { x, y, rotation }
├── VelocityComponent   { vx, vy }
├── SpriteComponent     { texture, color }
├── HealthComponent     { hp, maxHp }
└── (any system can process any combination)
```

**Benefits:** Cache-friendly iteration, easy to compose behaviors, clean separation of data and logic.

---

## Step 1: MitiruWorld Basics

```cpp
#include <mitiru/ecs/MitiruWorld.hpp>

mitiru::ecs::MitiruWorld world;

// Create an entity
auto entity = world.world().createEntity();

// Tag it for identification
world.setTag(entity, "player");

// Add semantic labels
world.addLabel(entity, "controllable");
world.addLabel(entity, "damageable");

// Query
bool isControllable = world.hasLabel(entity, "controllable");  // true
std::string tag = world.getTag(entity);                         // "player"
std::size_t count = world.entityCount();                        // 1
```

---

## Step 2: Define Components

Components are plain data structs. No inheritance required.

```cpp
// Position and rotation in the world
struct TransformComponent
{
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

// Linear movement
struct VelocityComponent
{
    float vx = 0.0f;
    float vy = 0.0f;
};

// Visual appearance
struct SpriteComponent
{
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float width = 32.0f;
    float height = 32.0f;
};

// Health and damage
struct HealthComponent
{
    int hp = 100;
    int maxHp = 100;
    bool alive = true;
};

// Player-controlled flag
struct PlayerControlled {};

// Enemy AI flag
struct EnemyTag {};
```

---

## Step 3: Add Components to Entities

```cpp
auto& w = world.world();

// Create player
auto player = w.createEntity();
w.addComponent(player, TransformComponent{400.0f, 300.0f});
w.addComponent(player, VelocityComponent{});
w.addComponent(player, SpriteComponent{0.3f, 0.7f, 1.0f, 1.0f, 32.0f, 32.0f});
w.addComponent(player, HealthComponent{100, 100, true});
w.addComponent(player, PlayerControlled{});
world.setTag(player, "player");

// Create enemies
for (int i = 0; i < 10; ++i)
{
    auto enemy = w.createEntity();
    w.addComponent(enemy, TransformComponent{
        static_cast<float>(100 + i * 80),
        static_cast<float>(50 + (i % 3) * 100)
    });
    w.addComponent(enemy, VelocityComponent{0.0f, 50.0f});
    w.addComponent(enemy, SpriteComponent{1.0f, 0.3f, 0.2f, 1.0f, 24.0f, 24.0f});
    w.addComponent(enemy, HealthComponent{30, 30, true});
    w.addComponent(enemy, EnemyTag{});
    world.setTag(enemy, "enemy_" + std::to_string(i));
}

// Create collectibles
for (int i = 0; i < 5; ++i)
{
    auto coin = w.createEntity();
    w.addComponent(coin, TransformComponent{
        static_cast<float>(200 + i * 120),
        static_cast<float>(400)
    });
    w.addComponent(coin, SpriteComponent{1.0f, 0.9f, 0.1f, 1.0f, 16.0f, 16.0f});
    world.setTag(coin, "coin_" + std::to_string(i));
}
```

---

## Step 4: Write Systems

Systems are functions (or classes) that process entities with specific component combinations.

### Movement System

```cpp
void movementSystem(sgc::ecs::World& w, float dt)
{
    w.forEach<TransformComponent, VelocityComponent>(
        [dt](sgc::ecs::Entity, TransformComponent& t, VelocityComponent& v)
        {
            t.x += v.vx * dt;
            t.y += v.vy * dt;
        }
    );
}
```

### Player Input System

```cpp
void playerInputSystem(sgc::ecs::World& w, const mitiru::InputState& input, float dt)
{
    const float speed = 200.0f;

    w.forEach<TransformComponent, PlayerControlled>(
        [&input, dt, speed](sgc::ecs::Entity, TransformComponent& t, PlayerControlled&)
        {
            if (input.isKeyDown(0x41)) t.x -= speed * dt;  // A
            if (input.isKeyDown(0x44)) t.x += speed * dt;  // D
            if (input.isKeyDown(0x57)) t.y -= speed * dt;  // W
            if (input.isKeyDown(0x53)) t.y += speed * dt;  // S
        }
    );
}
```

### Enemy AI System

```cpp
void enemyAISystem(sgc::ecs::World& w, float dt)
{
    w.forEach<TransformComponent, VelocityComponent, EnemyTag>(
        [dt](sgc::ecs::Entity, TransformComponent& t, VelocityComponent& v, EnemyTag&)
        {
            // Simple bounce movement
            if (t.y < 50.0f || t.y > 500.0f)
            {
                v.vy = -v.vy;
            }
        }
    );
}
```

### Render System

```cpp
void renderSystem(sgc::ecs::World& w, mitiru::Screen& screen)
{
    w.forEach<TransformComponent, SpriteComponent>(
        [&screen](sgc::ecs::Entity, const TransformComponent& t, const SpriteComponent& s)
        {
            screen.fillRect(
                static_cast<int>(t.x - s.width * 0.5f),
                static_cast<int>(t.y - s.height * 0.5f),
                static_cast<int>(s.width),
                static_cast<int>(s.height),
                {s.r, s.g, s.b, s.a}
            );
        }
    );
}
```

### Health Bar System

```cpp
void healthBarSystem(sgc::ecs::World& w, mitiru::Screen& screen)
{
    w.forEach<TransformComponent, HealthComponent, SpriteComponent>(
        [&screen](sgc::ecs::Entity, const TransformComponent& t,
                  const HealthComponent& h, const SpriteComponent& s)
        {
            if (h.maxHp <= 0) return;
            const float barWidth = s.width;
            const float barHeight = 4.0f;
            const float fillRatio = static_cast<float>(h.hp) / static_cast<float>(h.maxHp);

            // Background
            screen.fillRect(
                static_cast<int>(t.x - barWidth * 0.5f),
                static_cast<int>(t.y - s.height * 0.5f - 8.0f),
                static_cast<int>(barWidth),
                static_cast<int>(barHeight),
                {0.3f, 0.0f, 0.0f, 0.8f}
            );

            // Fill
            screen.fillRect(
                static_cast<int>(t.x - barWidth * 0.5f),
                static_cast<int>(t.y - s.height * 0.5f - 8.0f),
                static_cast<int>(barWidth * fillRatio),
                static_cast<int>(barHeight),
                {0.0f, 0.8f, 0.2f, 0.9f}
            );
        }
    );
}
```

---

## Step 5: Wire Everything Together

```cpp
class EcsGame : public mitiru::Game
{
public:
    EcsGame()
    {
        setupEntities();
    }

    void update(float dt) override
    {
        auto& w = m_world.world();

        playerInputSystem(w, input(), dt);
        enemyAISystem(w, dt);
        movementSystem(w, dt);
    }

    void draw(mitiru::Screen& screen) override
    {
        screen.clear({0.08f, 0.08f, 0.12f, 1.0f});

        auto& w = m_world.world();
        renderSystem(w, screen);
        healthBarSystem(w, screen);

        // HUD
        screen.drawText({10.0f, 10.0f},
            "Entities: " + std::to_string(m_world.entityCount()),
            {0.6f, 0.6f, 0.6f, 1.0f}, 16.0f);
    }

    [[nodiscard]] mitiru::Size layout(int, int) override
    {
        return {800, 600};
    }

private:
    mitiru::ecs::MitiruWorld m_world;

    void setupEntities()
    {
        // ... (entity creation code from Step 3)
    }
};
```

---

## Step 6: Using SystemScheduler

For ordered system execution, use `SystemScheduler`:

```cpp
#include <mitiru/ecs/SystemScheduler.hpp>
#include <mitiru/scene/SystemRunner.hpp>

// Define a system as a class
class MovementSystem : public mitiru::scene::ISystem
{
public:
    void update(float dt) override
    {
        m_world->world().forEach<TransformComponent, VelocityComponent>(
            [dt](sgc::ecs::Entity, TransformComponent& t, VelocityComponent& v)
            {
                t.x += v.vx * dt;
                t.y += v.vy * dt;
            }
        );
    }

    void setWorld(mitiru::ecs::MitiruWorld* world) { m_world = world; }

private:
    mitiru::ecs::MitiruWorld* m_world = nullptr;
};

// Register and run systems in order
mitiru::scene::SystemRunner runner;
auto moveSys = std::make_shared<MovementSystem>();
moveSys->setWorld(&m_world);
runner.addSystem(moveSys);

// In update:
runner.updateAll(dt);
```

---

## Step 7: Query Entities

### Find by Tag

```cpp
// Find the player entity by tag
std::optional<sgc::ecs::Entity> findByTag(
    mitiru::ecs::MitiruWorld& world,
    const std::string& tag)
{
    std::optional<sgc::ecs::Entity> result;
    world.world().forEach<TransformComponent>(
        [&](sgc::ecs::Entity e, TransformComponent&)
        {
            if (world.getTag(e) == tag)
            {
                result = e;
            }
        }
    );
    return result;
}

auto player = findByTag(m_world, "player");
```

### Check Component Existence

```cpp
auto& w = m_world.world();
if (w.hasComponent<HealthComponent>(entity))
{
    auto& health = w.getComponent<HealthComponent>(entity);
    health.hp -= 10;
}
```

### Destroy Entities

```cpp
// Remove dead entities
std::vector<sgc::ecs::Entity> toRemove;
w.forEach<HealthComponent>(
    [&toRemove](sgc::ecs::Entity e, HealthComponent& h)
    {
        if (h.hp <= 0)
        {
            toRemove.push_back(e);
        }
    }
);

for (auto e : toRemove)
{
    m_world.destroyEntity(e);  // Also cleans up tags and labels
}
```

---

## Step 8: JSON Snapshots

MitiruWorld can export its state as JSON, useful for debugging and save/load:

```cpp
std::string json = m_world.snapshot();
// Output:
// {"entityCount":15,"tags":{"0":"player","1":"enemy_0",...},"labels":{"0":["controllable","damageable"],...}}
```

---

## ECS vs Traditional OOP

| Aspect | Traditional OOP | ECS |
|--------|----------------|-----|
| Data layout | Scattered in objects | Contiguous arrays |
| Adding behavior | New class or inheritance | Add component + system |
| Performance | Virtual dispatch overhead | Cache-friendly iteration |
| Coupling | Classes know each other | Systems are independent |
| Serialization | Complex (deep object graphs) | Simple (flat component data) |
| Testing | Need to construct full objects | Test one system at a time |

---

## Key Classes Reference

| Class | Header | Purpose |
|-------|--------|---------|
| `MitiruWorld` | `ecs/MitiruWorld.hpp` | Extended ECS world with tags/labels |
| `sgc::ecs::World` | (from sgc) | Core ECS world (create/destroy/query) |
| `sgc::ecs::Entity` | (from sgc) | Entity handle (ID + generation) |
| `SystemScheduler` | `ecs/SystemScheduler.hpp` | Ordered system execution |
| `SystemRunner` | `scene/SystemRunner.hpp` | System lifecycle management |
| `ComponentRegistry` | `ecs/ComponentRegistry.hpp` | Runtime component type info |
| `Prefab` | `ecs/Prefab.hpp` | Entity templates for mass creation |

---

## Exercises

1. Add a **collision system** that checks overlap between player and enemies
2. Implement a **spawner system** that creates new enemies periodically
3. Add a **lifetime component** that auto-destroys entities after N seconds
4. Create a **particle effect system** with many short-lived entities
5. Serialize entity state to JSON and implement save/load

---

## Next Steps

- [Architecture Overview](../ARCHITECTURE.md) for the full module map
- [Troubleshooting](../TROUBLESHOOTING.md) for common build issues
- Explore the `sample/demo/EcsDemo.hpp` and `sample/arcade/EcsShooterGame.hpp` for complete examples
