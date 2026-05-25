// hello_scene — Mode A。1 つの Game に 2 シーン: Title -> Game。
// Title で Space を押すと進む。どこでも ESC で終了。
// あえて SceneRouter でなく素の enum を使う — あれは別の大きな話題。

#include <mitiru/Mitiru.hpp>

namespace {

constexpr int kKeyEscape = 27;
constexpr int kKeySpace  = 32;

enum class Scene
{
    Title,
    Game,
};

class HelloScene final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        m_elapsed += dt;

        if (!hasInput()) return;

        if (input().isKeyJustPressed(kKeyEscape))
        {
            if (auto* eng = engine()) eng->requestStop();
            return;
        }

        if (m_scene == Scene::Title && input().isKeyJustPressed(kKeySpace))
        {
            m_scene = Scene::Game;
            m_elapsed = 0.0f;
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        if (m_scene == Scene::Title)
            drawTitle(screen);
        else
            drawGame(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    void drawTitle(mitiru::Screen& screen) const
    {
        screen.clear(sgc::Colorf{0.07f, 0.05f, 0.12f, 1.0f});

        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());

        screen.drawTextInRect(
            sgc::Rectf{0.0f, h * 0.4f, w, 40.0f},
            "MitiruEngine",
            sgc::Colorf{0.95f, 0.97f, 1.0f, 1.0f},
            36.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);

        screen.drawTextInRect(
            sgc::Rectf{0.0f, h * 0.55f, w, 24.0f},
            "Press SPACE to start",
            sgc::Colorf{0.75f, 0.80f, 0.90f, 1.0f},
            20.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawGame(mitiru::Screen& screen) const
    {
        screen.clear(sgc::Colorf{0.05f, 0.10f, 0.07f, 1.0f});

        const float w = static_cast<float>(screen.width());
        const float h = static_cast<float>(screen.height());

        screen.drawTextInRect(
            sgc::Rectf{0.0f, h * 0.45f, w, 32.0f},
            "Now playing — press ESC to quit",
            sgc::Colorf{0.90f, 0.95f, 0.85f, 1.0f},
            24.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    Scene m_scene = Scene::Title;
    float m_elapsed = 0.0f;
};

}  // namespace

int main()
{
    mitiru::Engine engine;
    HelloScene game;

    mitiru::EngineConfig cfg;
    cfg.title = "hello_scene";
    cfg.windowWidth = 800;
    cfg.windowHeight = 600;
    cfg.enableCef = false;
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
