// mitiru_audio — axis 3 (per-system isolation), 2nd subsystem demo.
//
// Boots ONLY the audio subsystem (miniaudio backend) alongside a minimal
// renderer for the status HUD. No CEF, no ECS, no scene manager, no
// physics. Proves the per-system isolation pattern (introduced by
// mitiru_renderer) is repeatable for arbitrary subsystems.
//
// What you can do:
//   - launch with `mitiru audio` to confirm miniaudio inits on your machine
//   - launch with `mitiru_audio <path>` to load a .wav/.mp3/.flac on startup
//   - SPACE to (re-)play the loaded file
//   - + / - to step master volume (in 10% increments)
//   - ESC to quit
//
// Controls:  SPACE play · + - volume · ESC quit

#include <cmath>
#include <cstdio>
#include <string>

#include <mitiru/Mitiru.hpp>
#include <mitiru/audio/MiniaudioEngine.hpp>

namespace {

class AudioPlayground final : public mitiru::Game
{
public:
    explicit AudioPlayground(std::string fileArg)
        : m_filePath(std::move(fileArg))
    {
        m_audioReady = m_audio.isInitialized();
        if (m_audioReady) { m_audio.setMasterVolume(m_volume); }
    }

    void update(float dt) override
    {
        m_elapsed += dt;

        if (!hasInput()) { return; }

        if (input().isKeyJustPressed(mitiru::KeyCode::Escape))
        {
            if (auto* eng = engine()) { eng->requestStop(); }
            return;
        }

        if (input().isKeyJustPressed(mitiru::KeyCode::Space))
        {
            if (m_audioReady && !m_filePath.empty())
            {
                m_audio.playFile(m_filePath);
                m_playCount++;
                m_lastPlayT = m_elapsed;
            }
        }

        // Volume bump using the keyboard's +/- (Equal / Minus).
        if (input().isKeyJustPressed(mitiru::KeyCode::Equal))
        {
            m_volume = std::min(1.0f, m_volume + 0.10f);
            m_audio.setMasterVolume(m_volume);
        }
        if (input().isKeyJustPressed(mitiru::KeyCode::Minus))
        {
            m_volume = std::max(0.0f, m_volume - 0.10f);
            m_audio.setMasterVolume(m_volume);
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        m_screenW = static_cast<float>(screen.width());
        m_screenH = static_cast<float>(screen.height());

        screen.clear(sgc::Colorf{0.05f, 0.06f, 0.10f, 1.0f});

        drawVolumeBar(screen);
        drawStatusPanel(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    void drawVolumeBar(mitiru::Screen& screen)
    {
        const float cx = m_screenW * 0.5f;
        const float cy = m_screenH * 0.45f;
        const float barW = std::min(m_screenW * 0.6f, 520.0f);
        const float barH = 28.0f;

        // Outline.
        screen.drawRect(
            sgc::Rectf{cx - barW * 0.5f - 2.0f, cy - barH * 0.5f - 2.0f,
                       barW + 4.0f, barH + 4.0f},
            sgc::Colorf{0.18f, 0.22f, 0.32f, 1.0f});
        // Track.
        screen.drawRect(
            sgc::Rectf{cx - barW * 0.5f, cy - barH * 0.5f, barW, barH},
            sgc::Colorf{0.10f, 0.13f, 0.20f, 1.0f});
        // Fill.
        const float fillW = barW * m_volume;
        const sgc::Colorf fill = m_audioReady
            ? sgc::Colorf{0.40f, 0.85f, 0.95f, 1.0f}
            : sgc::Colorf{0.55f, 0.55f, 0.55f, 1.0f};
        screen.drawRect(
            sgc::Rectf{cx - barW * 0.5f, cy - barH * 0.5f, fillW, barH},
            fill);

        // "Vibration" overlay — bar pulses for ~0.5s after each playback to
        // give visual confirmation even without an actual audio device.
        if (m_lastPlayT > 0.0f && m_elapsed - m_lastPlayT < 0.5f)
        {
            const float t = (m_elapsed - m_lastPlayT) / 0.5f;
            const float a = (1.0f - t) * 0.35f;
            screen.drawRect(
                sgc::Rectf{cx - barW * 0.5f, cy - barH * 0.5f, barW, barH},
                sgc::Colorf{0.95f, 0.97f, 1.0f, a});
        }

        char volTxt[32];
        std::snprintf(volTxt, sizeof(volTxt), "Volume %d%%",
            static_cast<int>(m_volume * 100.0f + 0.5f));
        screen.drawTextInRect(
            sgc::Rectf{cx - 80.0f, cy - barH * 0.5f - 28.0f, 160.0f, 22.0f},
            volTxt,
            sgc::Colorf{0.78f, 0.85f, 0.95f, 1.0f},
            16.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawStatusPanel(mitiru::Screen& screen)
    {
        const float panelW = 480.0f;
        const float panelH = 138.0f;
        const float x = 18.0f;
        const float y = m_screenH - panelH - 18.0f;

        screen.drawRect(
            sgc::Rectf{x, y, panelW, panelH},
            sgc::Colorf{0.05f, 0.07f, 0.12f, 0.82f});

        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + 8.0f, panelW - 24.0f, 24.0f},
            "MitiruEngine — audio subsystem",
            sgc::Colorf{0.95f, 0.97f, 1.0f, 1.0f},
            16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        const std::string state = m_audioReady
            ? std::string{"miniaudio: initialised"}
            : std::string{"miniaudio: NOT available (headless / no device)"};
        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + 34.0f, panelW - 24.0f, 22.0f},
            state,
            m_audioReady ? sgc::Colorf{0.40f, 0.85f, 0.95f, 1.0f}
                         : sgc::Colorf{0.95f, 0.55f, 0.55f, 1.0f},
            14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        const std::string fileLine = m_filePath.empty()
            ? std::string{"file: <none>  ·  pass a .wav/.mp3 as argv[1]"}
            : std::string{"file: "} + m_filePath;
        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + 58.0f, panelW - 24.0f, 22.0f},
            fileLine.c_str(),
            sgc::Colorf{0.78f, 0.85f, 0.95f, 1.0f},
            13.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        char counters[96];
        std::snprintf(counters, sizeof(counters),
            "plays %d  ·  elapsed %.1f s",
            m_playCount, m_elapsed);
        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + 82.0f, panelW - 24.0f, 22.0f},
            counters,
            sgc::Colorf{0.60f, 0.68f, 0.82f, 1.0f},
            12.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        screen.drawTextInRect(
            sgc::Rectf{x + 12.0f, y + panelH - 28.0f, panelW - 24.0f, 22.0f},
            "SPACE play  ·  + - volume  ·  ESC quit",
            sgc::Colorf{0.50f, 0.58f, 0.72f, 1.0f},
            12.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    mitiru::audio::MiniaudioEngine m_audio;
    bool                            m_audioReady{false};
    std::string                     m_filePath;
    float                           m_volume{0.6f};
    float                           m_screenW{1280.0f};
    float                           m_screenH{720.0f};
    float                           m_elapsed{0.0f};
    float                           m_lastPlayT{-10.0f};
    int                             m_playCount{0};
};

}  // namespace

int main(int argc, char* argv[])
{
    std::string fileArg = (argc > 1 && argv[1] && *argv[1]) ? argv[1] : "";

    mitiru::Engine engine;
    AudioPlayground game(std::move(fileArg));

    mitiru::EngineConfig cfg;
    cfg.title           = "MitiruEngine — audio";
    cfg.windowWidth     = 960;
    cfg.windowHeight    = 540;
    cfg.vsync           = true;
    cfg.enableCef       = false;
    cfg.fontAtlasRanges = mitiru::EngineConfig::FontAtlas::Latin;

    engine.run(game, cfg);
    return 0;
}
