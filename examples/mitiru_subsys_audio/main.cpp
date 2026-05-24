// mitiru_subsys_audio — axis 3 (per-system isolation) P3 deliverable.
//
// Boots the audio subsystem with no game logic, no CEF, no inspector — just
// Engine + Screen for the meter HUD + miniaudio ma_device pulling samples
// from SineSynth on the audio thread. Mirrors mitiru_subsys_renderer in
// structure: one .cpp, < 150 lines, silver-gray Saturn surface.
//
// What you see:
//   - silver-gray background
//   - title "audio subsystem - 440Hz test tone" (top)
//   - large Saturn red level meter bar (center) driven by RMS of audio thread output
//   - hint "press ESC to quit" (bottom)
//
// What you hear: a continuous 440Hz sine on the default output device.
//
// Controls: ESC quits. Auto-exits at 5.0s for unattended capture.
//
// Why this exists (axis 3 / "全 system 単独起動"):
//   - Same Engine class, audio subsystem only — same isolation guarantee the
//     renderer subsystem example proves, repeated for a non-graphics system.

#include <atomic>
#include <cmath>
#include <miniaudio.h>

#include <mitiru/Mitiru.hpp>
#include <mitiru/audio/SineSynth.hpp>

namespace {

constexpr sgc::Colorf kPaperBg     {0.784f, 0.784f, 0.784f, 1.0f};  // #c8c8c8 silver
constexpr sgc::Colorf kPaperEdge   {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 ink border
constexpr sgc::Colorf kInk         {0.063f, 0.063f, 0.063f, 1.0f};  // #101010
constexpr sgc::Colorf kMute        {0.290f, 0.290f, 0.290f, 1.0f};  // #4a4a4a
constexpr sgc::Colorf kAmberAccent {0.784f, 0.0f,   0.173f, 1.0f};  // #c8002c Saturn red

constexpr float kAutoExitSec = 5.0f;
constexpr int   kSampleRate  = 48000;

// Shared between audio thread (writer) and main thread (reader).
std::atomic<float> g_levelRms{0.0f};

void audioDataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
{
    auto* synth = static_cast<mitiru::audio::SineSynth*>(device->pUserData);
    auto* out   = static_cast<float*>(output);
    synth->render(out, frameCount, kSampleRate);

    // RMS over the chunk — cheap level meter for the HUD.
    float sumSq = 0.0f;
    for (ma_uint32 i = 0; i < frameCount; ++i) { sumSq += out[i] * out[i]; }
    const float rms = std::sqrt(sumSq / static_cast<float>(frameCount));
    g_levelRms.store(rms, std::memory_order_relaxed);
}

class AudioSampleGame final : public mitiru::Game
{
public:
    AudioSampleGame()
    {
        m_synth.setGain(0.30f);
        m_synth.setEnvelope(0.02f, 0.05f, 1.0f, 0.20f);
        m_voice = m_synth.noteOn(69, 1.0f);  // MIDI 69 = A4 = 440Hz

        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format    = ma_format_f32;
        cfg.playback.channels  = 1;
        cfg.sampleRate         = kSampleRate;
        cfg.dataCallback       = &audioDataCallback;
        cfg.pUserData          = &m_synth;
        if (ma_device_init(nullptr, &cfg, &m_device) == MA_SUCCESS)
        {
            m_deviceUp = (ma_device_start(&m_device) == MA_SUCCESS);
        }
    }

    void update(float dt) override
    {
        m_elapsed += dt;
        m_level    = g_levelRms.load(std::memory_order_relaxed);

        const bool escPressed = hasInput()
            && input().isKeyJustPressed(mitiru::KeyCode::Escape);
        if (escPressed || m_elapsed >= kAutoExitSec)
        {
            if (auto* eng = engine()) { eng->requestStop(); }
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        m_screenW = static_cast<float>(screen.width());
        m_screenH = static_cast<float>(screen.height());
        screen.clear(kPaperBg);
        drawTitle(screen);
        drawMeter(screen);
        drawHint(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

    ~AudioSampleGame() override
    {
        if (m_deviceUp) { ma_device_uninit(&m_device); }
    }

private:
    void drawTitle(mitiru::Screen& screen)
    {
        screen.drawTextInRect(
            sgc::Rectf{16.0f, 24.0f, m_screenW - 32.0f, 32.0f},
            "audio subsystem - 440Hz test tone",
            kInk, 24.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawMeter(mitiru::Screen& screen)
    {
        const float barW  = std::min(m_screenW * 0.72f, 640.0f);
        const float barH  = 56.0f;
        const float x     = (m_screenW - barW) * 0.5f;
        const float y     = (m_screenH - barH) * 0.5f;
        // Track outline.
        screen.drawRect(sgc::Rectf{x - 2.0f, y - 2.0f, barW + 4.0f, barH + 4.0f}, kPaperEdge);
        // Track interior — light gray #d8d8d8 inset against silver surface.
        screen.drawRect(sgc::Rectf{x, y, barW, barH}, sgc::Colorf{0.847f, 0.847f, 0.847f, 1.0f});
        // Fill — RMS amplified so a 0.30-gain sine still visibly travels.
        const float norm = std::clamp(m_level * 4.0f, 0.0f, 1.0f);
        screen.drawRect(sgc::Rectf{x, y, barW * norm, barH}, kAmberAccent);
    }

    void drawHint(mitiru::Screen& screen)
    {
        const char* line = m_deviceUp
            ? "press ESC to quit (auto-exit at 5s)"
            : "audio device unavailable - meter silent";
        screen.drawTextInRect(
            sgc::Rectf{16.0f, m_screenH - 32.0f, m_screenW - 32.0f, 20.0f},
            line, kMute, 16.0f,
            mitiru::Screen::TextAlignH::Center,
            mitiru::Screen::TextAlignV::Top);
    }

    mitiru::audio::SineSynth m_synth{4, kSampleRate, 1};
    ma_device                m_device{};
    bool                     m_deviceUp{false};
    mitiru::audio::VoiceHandle m_voice{0};
    float                    m_screenW{800.0f};
    float                    m_screenH{500.0f};
    float                    m_elapsed{0.0f};
    float                    m_level{0.0f};
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    mitiru::Engine    engine;
    AudioSampleGame   game;

    mitiru::EngineConfig cfg;
    cfg.title                = "mitiru_subsys_audio";
    cfg.windowWidth          = 800;
    cfg.windowHeight         = 500;
    cfg.vsync                = true;
    cfg.enableCef            = false;
    cfg.fontAtlasRanges      = mitiru::EngineConfig::FontAtlas::Latin;
    cfg.useLogicalWindowSize = true;
    cfg.backgroundColor      = kPaperBg;

    engine.run(game, cfg);
    return 0;
}
