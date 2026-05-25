// hello_window — Mode A の最小。
//
// 800x600 ウィンドウに "Hello, Mitiru!" を中央表示。ESC で終了。
// CEF なし、latin atlas のみ — cold start 1s 未満。

#include <mitiru/Mitiru.hpp>

namespace {

constexpr int kKeyEscape = 27;

class HelloWindow final : public mitiru::Game
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
        screen.clear(sgc::Colorf{0.05f, 0.07f, 0.12f, 1.0f});

        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());

        screen.drawTextInRect(
            sgc::Rectf{0.0f, h * 0.5f - 16.0f, w, 32.0f},
            "Hello, Mitiru!",
            sgc::Colorf{0.95f, 0.97f, 1.0f, 1.0f},
            28.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
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
    HelloWindow game;

    mitiru::EngineConfig cfg;
    cfg.title = "hello_window";
    cfg.windowWidth = 800;
    cfg.windowHeight = 600;
    cfg.enableCef = false;
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
