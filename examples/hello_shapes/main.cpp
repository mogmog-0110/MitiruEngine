// hello_shapes — Mode A. Five colored rectangles + two circles.
// One circle pulses with sin(t).

#include <cmath>
#include <mitiru/Mitiru.hpp>

namespace {

constexpr int kKeyEscape = 27;

class HelloShapes final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        m_t += dt;
        if (hasInput() && input().isKeyJustPressed(kKeyEscape))
        {
            if (auto* eng = engine()) eng->requestStop();
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        screen.clear(sgc::Colorf{0.07f, 0.07f, 0.10f, 1.0f});

        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());

        const sgc::Colorf colors[5] = {
            {0.95f, 0.35f, 0.40f, 1.0f},
            {0.95f, 0.70f, 0.30f, 1.0f},
            {0.80f, 0.90f, 0.35f, 1.0f},
            {0.35f, 0.85f, 0.95f, 1.0f},
            {0.65f, 0.45f, 0.95f, 1.0f},
        };

        constexpr float kRectW = 80.0f;
        constexpr float kRectH = 80.0f;
        constexpr float kGap   = 24.0f;
        const float row1Y = h * 0.30f;
        const float totalW = 5.0f * kRectW + 4.0f * kGap;
        const float startX = (w - totalW) * 0.5f;

        for (int i = 0; i < 5; ++i)
        {
            const float x = startX + i * (kRectW + kGap);
            screen.drawRect(
                sgc::Rectf{x, row1Y, kRectW, kRectH},
                colors[i]);
        }

        const float row2Y = h * 0.65f;
        const float pulse = 0.5f + 0.5f * std::sin(m_t * 2.0f);
        const float radius = 40.0f + pulse * 20.0f;

        screen.drawCircle(
            sgc::Vec2f{w * 0.40f, row2Y},
            48.0f,
            sgc::Colorf{0.80f, 0.85f, 0.95f, 1.0f});

        screen.drawCircle(
            sgc::Vec2f{w * 0.60f, row2Y},
            radius,
            sgc::Colorf{0.95f, 0.60f, 0.75f, 1.0f});

        screen.drawTextInRect(
            sgc::Rectf{0.0f, 16.0f, w, 24.0f},
            "drawRect / drawCircle — ESC to exit",
            sgc::Colorf{0.75f, 0.80f, 0.90f, 1.0f},
            18.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    float m_t = 0.0f;
};

}  // namespace

int main()
{
    mitiru::Engine engine;
    HelloShapes game;

    mitiru::EngineConfig cfg;
    cfg.title = "hello_shapes";
    cfg.windowWidth = 800;
    cfg.windowHeight = 600;
    cfg.enableCef = false;
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
