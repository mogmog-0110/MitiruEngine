// hello_input — Mode A。矢印キー / WASD で 64x64 の四角を動かす。ESC で終了。

#include <mitiru/Mitiru.hpp>

namespace {

constexpr int kKeyEscape = 27;
constexpr int kKeyLeft   = 37;
constexpr int kKeyUp     = 38;
constexpr int kKeyRight  = 39;
constexpr int kKeyDown   = 40;
constexpr int kKeyA = 'A';
constexpr int kKeyD = 'D';
constexpr int kKeyW = 'W';
constexpr int kKeyS = 'S';

class HelloInput final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        if (!hasInput()) return;

        if (input().isKeyJustPressed(kKeyEscape))
        {
            if (auto* eng = engine()) eng->requestStop();
            return;
        }

        constexpr float kSpeed = 320.0f;
        float vx = 0.0f, vy = 0.0f;
        if (input().isKeyDown(kKeyLeft)  || input().isKeyDown(kKeyA)) vx -= 1.0f;
        if (input().isKeyDown(kKeyRight) || input().isKeyDown(kKeyD)) vx += 1.0f;
        if (input().isKeyDown(kKeyUp)    || input().isKeyDown(kKeyW)) vy -= 1.0f;
        if (input().isKeyDown(kKeyDown)  || input().isKeyDown(kKeyS)) vy += 1.0f;

        m_x += vx * kSpeed * dt;
        m_y += vy * kSpeed * dt;
    }

    void draw(mitiru::Screen& screen) override
    {
        screen.clear(sgc::Colorf{0.05f, 0.07f, 0.12f, 1.0f});

        constexpr float kSize = 64.0f;
        screen.drawRect(
            sgc::Rectf{m_x - kSize * 0.5f, m_y - kSize * 0.5f, kSize, kSize},
            sgc::Colorf{0.35f, 0.85f, 0.95f, 1.0f});

        const float w = static_cast<float>(screen.width());
        screen.drawTextInRect(
            sgc::Rectf{0.0f, 16.0f, w, 24.0f},
            "Arrow keys / WASD to move, ESC to exit",
            sgc::Colorf{0.75f, 0.80f, 0.90f, 1.0f},
            18.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        if (m_x == 0.0f && m_y == 0.0f)
        {
            m_x = static_cast<float>(outsideW) * 0.5f;
            m_y = static_cast<float>(outsideH) * 0.5f;
        }
        return {outsideW, outsideH};
    }

private:
    float m_x = 0.0f;
    float m_y = 0.0f;
};

}  // namespace

int main()
{
    mitiru::Engine engine;
    HelloInput game;

    mitiru::EngineConfig cfg;
    cfg.title = "hello_input";
    cfg.windowWidth = 800;
    cfg.windowHeight = 600;
    cfg.enableCef = false;
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
