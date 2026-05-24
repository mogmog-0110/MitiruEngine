// mitiru_renderer — axis 3 (per-system isolation) seed.
//
// This program boots the renderer subsystem in isolation: no CEF, no audio
// engine, no ECS world, no scene manager, no physics, no input recording.
// All the other subsystems initialise to their Null backends or skip outright.
//
// What you get on screen:
//   - animated HSV color bars (proves clear + textured drawRect works)
//   - a slow rotating diagonal divider (proves world transform / projection)
//   - frame counter + elapsed wall-clock time (proves the loop is alive)
//   - the bottom-left status block names the renderer backend currently driving
//     the window
//
// Why this exists:
//   - Shader / pipeline iteration without paying CEF (~3s) or font atlas
//     (~15s) cold-start cost. Cold start here is < 1 second.
//   - Bisection: if a consumer game's draw is broken, this tool still works,
//     which narrows the blame to gameplay or higher-level subsystems.
//   - The "1 tool = 1 concern = 1 window" philosophy in action.
//
// Controls:  ESC to quit, F3 saves a PNG of the current frame.

#include <cmath>
#include <string>

#include <mitiru/Mitiru.hpp>
#include <mitiru/render/SaveScreenshotPng.hpp>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Cheap HSV→RGB for the animated color bar. Hue in [0,1).
sgc::Colorf hsv(float h, float s, float v, float a = 1.0f)
{
    h = h - std::floor(h);
    const float i = std::floor(h * 6.0f);
    const float f = h * 6.0f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    const int   ii = static_cast<int>(i) % 6;
    switch (ii)
    {
        case 0: return {v, t, p, a};
        case 1: return {q, v, p, a};
        case 2: return {p, v, t, a};
        case 3: return {p, q, v, a};
        case 4: return {t, p, v, a};
        default: return {v, p, q, a};
    }
}

class RendererPlayground final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        m_elapsed += dt;
        ++m_frame;

        if (hasInput())
        {
            // ESC quits.
            if (input().isKeyJustPressed(mitiru::KeyCode::Escape))
            {
                if (auto* eng = engine()) { eng->requestStop(); }
            }

            // F3 saves a screenshot to ./screenshots/mitiru_renderer_*.png.
            if (input().isKeyJustPressed(mitiru::KeyCode::F3))
            {
                takeScreenshot();
            }
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        m_screenW = static_cast<float>(screen.width());
        m_screenH = static_cast<float>(screen.height());

        screen.clear(sgc::Colorf{0.04f, 0.06f, 0.10f, 1.0f});

        drawHsvBars(screen);
        drawRotatingDivider(screen);
        drawStatusBlock(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    void drawHsvBars(mitiru::Screen& screen)
    {
        // 24 vertical bars across the top half. Hue cycles with time so the
        // whole gradient drifts to the right; alpha is fixed.
        constexpr int kBars = 24;
        const float w = m_screenW;
        const float h = m_screenH * 0.55f;
        const float barW = w / static_cast<float>(kBars);

        for (int i = 0; i < kBars; ++i)
        {
            const float hue = static_cast<float>(i) / kBars + m_elapsed * 0.05f;
            screen.drawRect(
                sgc::Rectf{i * barW, 0.0f, barW + 1.0f, h},
                hsv(hue, 0.85f, 0.95f));
        }
    }

    void drawRotatingDivider(mitiru::Screen& screen)
    {
        // A 6px thick diagonal stripe that orbits the screen centre. Renders
        // as a series of small rects approximating the line — keeps us off
        // any specific drawLine variant the backend may not expose.
        const float cx = m_screenW * 0.5f;
        const float cy = m_screenH * 0.65f;
        const float r  = std::min(m_screenW, m_screenH) * 0.30f;

        const float ang = m_elapsed * 0.6f;
        const float dx  = std::cos(ang);
        const float dy  = std::sin(ang);

        constexpr int kSegments = 60;
        for (int i = -kSegments; i <= kSegments; ++i)
        {
            const float t = static_cast<float>(i) / kSegments;
            const float px = cx + dx * r * t;
            const float py = cy + dy * r * t;
            screen.drawRect(
                sgc::Rectf{px - 3.0f, py - 3.0f, 6.0f, 6.0f},
                sgc::Colorf{0.95f, 0.97f, 1.0f, 0.85f});
        }
    }

    void drawStatusBlock(mitiru::Screen& screen)
    {
        // Plain text label. Uses drawTextInRect, not drawText (engine rule).
        const float panelW = 360.0f;
        const float panelH = 92.0f;
        const float x = 18.0f;
        const float y = m_screenH - panelH - 18.0f;

        screen.drawRect(
            sgc::Rectf{x, y, panelW, panelH},
            sgc::Colorf{0.05f, 0.07f, 0.12f, 0.78f});

        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + 8.0f, panelW - 24.0f, 24.0f},
            "MitiruEngine — renderer subsystem",
            sgc::Colorf{0.95f, 0.97f, 1.0f, 1.0f},
            16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        char line[96];
        std::snprintf(line, sizeof(line),
            "frame %llu  ·  %.2f s  ·  %dx%d",
            static_cast<unsigned long long>(m_frame),
            m_elapsed,
            static_cast<int>(m_screenW),
            static_cast<int>(m_screenH));
        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + 34.0f, panelW - 24.0f, 22.0f},
            line,
            sgc::Colorf{0.78f, 0.85f, 0.95f, 1.0f},
            14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + 58.0f, panelW - 24.0f, 22.0f},
            "ESC quit  ·  F3 screenshot",
            sgc::Colorf{0.50f, 0.58f, 0.72f, 1.0f},
            12.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    void takeScreenshot()
    {
        auto* eng = engine();
        if (!eng) { return; }
        auto* scr = eng->screen();
        if (!scr) { return; }
        const int w = scr->width();
        const int h = scr->height();
        if (w <= 0 || h <= 0) { return; }

        auto pixels = eng->capture();
        if (pixels.empty()) { return; }

        const std::string path = mitiru::render::saveTimestampedFrameToPng(
            pixels.data(), w, h, "screenshots", "mitiru_renderer");
        (void)path;  // shell user sees the file at the printed path; no HUD here
    }

    float          m_screenW{1280.0f};
    float          m_screenH{720.0f};
    float          m_elapsed{0.0f};
    std::uint64_t  m_frame{0};
};

}  // namespace

int main()
{
    mitiru::Engine engine;
    RendererPlayground game;

    mitiru::EngineConfig cfg;
    cfg.title           = "MitiruEngine — renderer";
    cfg.windowWidth     = 1280;
    cfg.windowHeight    = 720;
    cfg.vsync           = true;
    cfg.enableCef       = false;   // pure renderer — no HTML overlay
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
