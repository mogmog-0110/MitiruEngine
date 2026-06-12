// mitiru_subsys_renderer — axis 3 (全 system 単独起動) の P3 成果物。
//
// ゲームロジック・CEF・audio・time-travel・inspector なしで renderer
// subsystem を起動する。Engine + Screen + 60Hz の update/draw loop のみ。
// 画面のテストパターンは意図的に最小で、shader / pipeline 編集時の renderer
// backend の視覚 smoke を兼ねる。
//
// 見えるもの:
//   - 銀灰の Saturn 背景 (launcher / hello_game と揃える)
//   - 64px のヘアライン grid が surface 全体を覆う
//   - 中央の 60x60 rect が水平に往復 (Saturn red のアクセント)
//   - 左上に "frame: N" カウンタ、左下にヒント行
//
// 操作: ESC で終了。
//
// 存在理由 (axis 3 / 全 system 単独起動):
//   - 同じ Engine class が gameplay 層なしで動く — host-game 境界が実在し、
//     ゲームコードに依存していないことを示す。
//   - cold-start 予算 < 1s (CEF init なし、実描画する Latin 範囲を超える font
//     atlas の暖機なし)。

#include <cmath>
#include <cstdio>

// アンブレラ廃止 (リファクタ P2) — 使うものだけ明示 include
#include <mitiru/core/Engine.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Config.hpp>

namespace {

constexpr sgc::Colorf kPaperBg     {0.784f, 0.784f, 0.784f, 1.0f};  // #c8c8c8 銀
constexpr sgc::Colorf kPaperEdge   {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 墨の縁
constexpr sgc::Colorf kInk         {0.063f, 0.063f, 0.063f, 1.0f};  // #101010
constexpr sgc::Colorf kMute        {0.290f, 0.290f, 0.290f, 1.0f};  // #4a4a4a 中間灰
constexpr sgc::Colorf kAmberAccent {0.784f, 0.0f,   0.173f, 1.0f};  // #c8002c Saturn red

class RendererSampleGame final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        m_elapsed += dt;
        ++m_frame;

        if (hasInput() &&
            input().isKeyJustPressed(mitiru::KeyCode::Escape))
        {
            if (auto* eng = engine()) { eng->requestStop(); }
        }
    }

    void draw(mitiru::Screen& screen) override
    {
        m_screenW = static_cast<float>(screen.width());
        m_screenH = static_cast<float>(screen.height());

        screen.clear(kPaperBg);

        drawGrid(screen);
        drawCenterRect(screen);
        drawFrameLabel(screen);
        drawHint(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    void drawGrid(mitiru::Screen& screen)
    {
        constexpr float kStep = 64.0f;
        // 縦線。
        for (float x = kStep; x < m_screenW; x += kStep)
        {
            screen.drawRect(sgc::Rectf{x, 0.0f, 1.0f, m_screenH}, kPaperEdge);
        }
        // 横線。
        for (float y = kStep; y < m_screenH; y += kStep)
        {
            screen.drawRect(sgc::Rectf{0.0f, y, m_screenW, 1.0f}, kPaperEdge);
        }
    }

    void drawCenterRect(mitiru::Screen& screen)
    {
        // 画面中心を水平に往復する 60x60 rect。真の回転は避け (Screen に今は
        // transform API なし)、smoke 用に renderer が時間駆動だと見せる。
        constexpr float kSize     = 60.0f;
        const float     amplitude = std::min(m_screenW * 0.30f, 200.0f);
        const float     cx        = m_screenW * 0.5f
                                  + std::sin(m_elapsed * 2.0f) * amplitude;
        const float     cy        = m_screenH * 0.5f;
        screen.drawRect(
            sgc::Rectf{cx - kSize * 0.5f, cy - kSize * 0.5f, kSize, kSize},
            kAmberAccent);
    }

    void drawFrameLabel(mitiru::Screen& screen)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "frame: %llu",
                      static_cast<unsigned long long>(m_frame));
        screen.drawTextInRect(
            sgc::Rectf{16.0f, 12.0f, m_screenW - 32.0f, 32.0f},
            buf,
            kInk,
            24.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawHint(mitiru::Screen& screen)
    {
        screen.drawTextInRect(
            sgc::Rectf{16.0f, m_screenH - 28.0f, m_screenW - 32.0f, 20.0f},
            "renderer subsystem - no game logic, no CEF",
            kMute,
            16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    float         m_screenW{800.0f};
    float         m_screenH{500.0f};
    float         m_elapsed{0.0f};
    std::uint64_t m_frame{0};
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    mitiru::Engine        engine;
    RendererSampleGame    game;

    mitiru::EngineConfig cfg;
    cfg.title              = "mitiru_subsys_renderer";
    cfg.windowWidth        = 800;
    cfg.windowHeight       = 500;
    cfg.vsync              = true;
    cfg.enableCef          = false;
    cfg.fontAtlasRanges    = mitiru::EngineConfig::FontAtlas::Latin;
    cfg.useLogicalWindowSize = true;
    // 銀灰の Saturn surface — host 側 clear は draw 内の screen.clear() と
    // 一致させる。最初のフレーム (draw 実行前) を黒にしないため。
    cfg.backgroundColor    = kPaperBg;

    engine.run(game, cfg);
    return 0;
}
