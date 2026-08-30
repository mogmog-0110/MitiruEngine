// mitiru_subsys_audio。エンジン全体なしで audio subsystem だけを起動する最小 exe。
//
// audio subsystem を単独起動する: ゲームロジック・CEF・inspector なし。
// Engine + Screen (meter HUD 用) + audio thread で SineSynth からサンプルを
// 引く miniaudio ma_device のみ。構成は mitiru_subsys_renderer に倣う:
// 単一 .cpp、150 行未満、銀灰の Saturn surface。
//
// 見えるもの:
//   - 銀灰の背景
//   - タイトル "audio subsystem - 440Hz test tone" (上)
//   - audio thread 出力の RMS で動く Saturn red の大きなレベルメーター (中央)
//   - ヒント "press ESC to quit" (下)
//
// 聞こえるもの: デフォルト出力デバイスで鳴り続ける 440Hz の sine。
//
// 操作: ESC で終了。無人キャプチャ用に 5.0s で自動終了。
//
// 存在理由 (全 system 単独起動の保証):
//   - 同じ Engine class で audio subsystem のみ。renderer subsystem 例が示す
//     のと同じ単独起動保証を、非グラフィック系で繰り返す。

#include <atomic>
#include <cmath>
#include <miniaudio.h>

// アンブレラ header は使わない。使うものだけ明示 include
#include <mitiru/core/Engine.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Config.hpp>
#include <mitiru/audio/SineSynth.hpp>

namespace {

constexpr sgc::Colorf kPaperBg     {0.784f, 0.784f, 0.784f, 1.0f};  // #c8c8c8 銀
constexpr sgc::Colorf kPaperEdge   {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 墨の縁
constexpr sgc::Colorf kInk         {0.063f, 0.063f, 0.063f, 1.0f};  // #101010
constexpr sgc::Colorf kMute        {0.290f, 0.290f, 0.290f, 1.0f};  // #4a4a4a
constexpr sgc::Colorf kAmberAccent {0.784f, 0.0f,   0.173f, 1.0f};  // #c8002c Saturn red

constexpr float kAutoExitSec = 5.0f;
constexpr int   kSampleRate  = 48000;

// audio thread (writer) と main thread (reader) で共有。
std::atomic<float> g_levelRms{0.0f};

void audioDataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
{
    auto* synth = static_cast<mitiru::audio::SineSynth*>(device->pUserData);
    auto* out   = static_cast<float*>(output);
    synth->render(out, frameCount, kSampleRate);

    // チャンクの RMS。HUD 用の軽量レベルメーター。
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
        // トラックの外枠。
        screen.drawRect(sgc::Rectf{x - 2.0f, y - 2.0f, barW + 4.0f, barH + 4.0f}, kPaperEdge);
        // トラック内部。銀の surface に対して明灰 #d8d8d8 を凹ませる。
        screen.drawRect(sgc::Rectf{x, y, barW, barH}, sgc::Colorf{0.847f, 0.847f, 0.847f, 1.0f});
        // フィル。gain 0.30 の sine でも見えるよう RMS を増幅。
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
