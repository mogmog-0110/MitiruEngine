// hello_cef_overlay — Mode B. Native clear + a translucent HTML HUD overlay.
//
// The HUD is pure CSS — no JS state push from C++ — just to prove the CEF
// compositing layer sits on top of the engine's frame and renders glass HUDs
// with HTML/CSS.

#include <mitiru/Mitiru.hpp>

namespace {

constexpr int kKeyEscape = 27;

class HelloCefOverlay final : public mitiru::Game
{
public:
    void update(float /*dt*/) override
    {
        if (hasInput() && input().isKeyJustPressed(kKeyEscape))
        {
            if (auto* eng = engine()) eng->requestStop();
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        screen.clear(sgc::Colorf{0.10f, 0.15f, 0.25f, 1.0f});

        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());

        screen.drawRect(
            sgc::Rectf{w * 0.5f - 80.0f, h * 0.5f - 80.0f, 160.0f, 160.0f},
            sgc::Colorf{0.25f, 0.45f, 0.65f, 1.0f});
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }
};

}  // namespace

int main()
{
    mitiru::Engine engine;
    HelloCefOverlay game;

    mitiru::EngineConfig cfg;
    cfg.title = "hello_cef_overlay";
    cfg.windowWidth = 960;
    cfg.windowHeight = 720;
    cfg.enableCef = true;
    cfg.cefStartUrl = "assets/scene.html";
    cfg.skipDefaultFont = true;

    engine.run(game, cfg);
    return 0;
}
