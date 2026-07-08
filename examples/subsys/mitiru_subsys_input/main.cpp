// mitiru_subsys_input — エンジン全体なしで input subsystem だけを起動する最小 exe。
//
// ゲームロジック・CEF・audio なしで input subsystem を起動する。Engine +
// Screen + 毎フレーム引く InputState のみ。HUD が生の 256-key VK テーブルを
// 観測可能にし、press / release をスクロールログにする。キーボード / マウス
// 配線の手動チェックを兼ねる exe。
//
// 見えるもの (Saturn パレット: 銀 bg / 黒 ink / Saturn red アクセント):
//   - ヘッダ "input subsystem" + 最後に押した VK コードを大きく表示
//   - 16×16 grid (256 セル) — keysDown[i] = true なら Saturn red で塗る
//   - 右パネル: mouse x/y + L/M/R ボタン状態
//   - 下: press ログのスクロール (新しい順、最大 12 行)
//
// 操作: ESC で終了。

#include <array>
#include <cstdio>
#include <string>

// アンブレラ header は使わない — 使うものだけ明示 include
#include <mitiru/core/Engine.hpp>
#include <mitiru/core/Game.hpp>
#include <mitiru/core/Config.hpp>

namespace {

constexpr sgc::Colorf kPaperBg     {0.784f, 0.784f, 0.784f, 1.0f};  // #c8c8c8 銀
constexpr sgc::Colorf kPaperEdge   {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 墨の縁
constexpr sgc::Colorf kInk         {0.063f, 0.063f, 0.063f, 1.0f};  // #101010
constexpr sgc::Colorf kMute        {0.290f, 0.290f, 0.290f, 1.0f};  // #4a4a4a
constexpr sgc::Colorf kAmberAccent {0.784f, 0.0f,   0.173f, 1.0f};  // #c8002c Saturn red
constexpr sgc::Colorf kCellEmpty   {0.870f, 0.870f, 0.870f, 1.0f};  // #dedede 凹
constexpr sgc::Colorf kPanelFill   {0.847f, 0.847f, 0.847f, 1.0f};  // #d8d8d8

constexpr int kMaxLog = 12;
constexpr int kGridCols = 16;
constexpr int kGridRows = 16;

class InputSampleGame final : public mitiru::Game
{
public:
    void update(float /*dt*/) override
    {
        if (!hasInput()) { return; }
        const mitiru::InputState& in = input();

        if (in.isKeyJustPressed(mitiru::KeyCode::Escape))
        {
            if (auto* eng = engine()) { eng->requestStop(); }
            return;
        }

        // 256-key テーブルを走査し、現在値とエッジを記録。
        for (int vk = 0; vk < mitiru::InputState::MAX_KEYS; ++vk)
        {
            m_keysDown[static_cast<std::size_t>(vk)] = in.isKeyDown(vk);
            if (in.isKeyJustPressed(vk))
            {
                m_lastVk = vk;
                pushLog("press VK_" + std::to_string(vk));
            }
            if (in.isKeyJustReleased(vk))
            {
                pushLog("release VK_" + std::to_string(vk));
            }
        }

        auto [mx, my] = in.mousePosition();
        m_mouseX = mx;
        m_mouseY = my;
        m_mouseL = in.isMouseButtonDown(mitiru::MouseButton::Left);
        m_mouseR = in.isMouseButtonDown(mitiru::MouseButton::Right);
        m_mouseM = in.isMouseButtonDown(mitiru::MouseButton::Middle);
    }

    void draw(mitiru::Screen& screen) override
    {
        m_screenW = static_cast<float>(screen.width());
        m_screenH = static_cast<float>(screen.height());
        screen.clear(kPaperBg);
        drawHeader(screen);
        drawGrid(screen);
        drawMousePanel(screen);
        drawLog(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    void pushLog(std::string line)
    {
        // 新しい順。kMaxLog で頭打ち (末尾の古いものを捨てる)。
        m_log.insert(m_log.begin(), std::move(line));
        if (static_cast<int>(m_log.size()) > kMaxLog)
        {
            m_log.pop_back();
        }
    }

    void drawHeader(mitiru::Screen& screen)
    {
        screen.drawTextInRect(
            sgc::Rectf{16.0f, 12.0f, m_screenW - 32.0f, 28.0f},
            "input subsystem", kInk, 22.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        char buf[32];
        if (m_lastVk >= 0) { std::snprintf(buf, sizeof(buf), "last: VK_%d", m_lastVk); }
        else               { std::snprintf(buf, sizeof(buf), "last: --"); }
        screen.drawTextInRect(
            sgc::Rectf{16.0f, 40.0f, m_screenW - 32.0f, 36.0f},
            buf, kAmberAccent, 28.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawGrid(mitiru::Screen& screen)
    {
        constexpr float kCell = 22.0f;
        constexpr float kGap  = 2.0f;
        const float gridX = 16.0f;
        const float gridY = 88.0f;
        for (int row = 0; row < kGridRows; ++row)
        {
            for (int col = 0; col < kGridCols; ++col)
            {
                const int   vk = row * kGridCols + col;
                const float x  = gridX + static_cast<float>(col) * (kCell + kGap);
                const float y  = gridY + static_cast<float>(row) * (kCell + kGap);
                const bool  on = m_keysDown[static_cast<std::size_t>(vk)];
                // 先に枠 (ink 1px)、次に内部塗り。
                screen.drawRect(sgc::Rectf{x, y, kCell, kCell}, kPaperEdge);
                screen.drawRect(sgc::Rectf{x + 1.0f, y + 1.0f, kCell - 2.0f, kCell - 2.0f},
                                on ? kAmberAccent : kCellEmpty);
            }
        }
    }

    void drawMousePanel(mitiru::Screen& screen)
    {
        const float panelX = 16.0f + static_cast<float>(kGridCols) * 24.0f + 24.0f;
        const float panelY = 88.0f;
        const float panelW = m_screenW - panelX - 16.0f;
        if (panelW < 40.0f) { return; }  // 窓が狭すぎ: 負幅 rect を出さず panel 描画を省く
        const float panelH = 180.0f;
        screen.drawRect(sgc::Rectf{panelX, panelY, panelW, panelH}, kPaperEdge);
        screen.drawRect(sgc::Rectf{panelX + 1.0f, panelY + 1.0f, panelW - 2.0f, panelH - 2.0f}, kPanelFill);

        char buf[48];
        std::snprintf(buf, sizeof(buf), "mouse %.0f, %.0f", m_mouseX, m_mouseY);
        screen.drawTextInRect(
            sgc::Rectf{panelX + 12.0f, panelY + 12.0f, panelW - 24.0f, 24.0f},
            buf, kInk, 18.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        drawButtonDot(screen, panelX + 12.0f,  panelY + 56.0f, "L", m_mouseL);
        drawButtonDot(screen, panelX + 12.0f,  panelY + 96.0f, "M", m_mouseM);
        drawButtonDot(screen, panelX + 12.0f,  panelY + 136.0f, "R", m_mouseR);
    }

    void drawButtonDot(mitiru::Screen& screen, float x, float y, const char* label, bool on)
    {
        constexpr float kDot = 20.0f;
        screen.drawRect(sgc::Rectf{x, y, kDot, kDot}, kPaperEdge);
        screen.drawRect(sgc::Rectf{x + 1.0f, y + 1.0f, kDot - 2.0f, kDot - 2.0f},
                        on ? kAmberAccent : kCellEmpty);
        screen.drawTextInRect(
            sgc::Rectf{x + kDot + 8.0f, y - 2.0f, 80.0f, 24.0f},
            label, kInk, 18.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    void drawLog(mitiru::Screen& screen)
    {
        const float logY = 88.0f + static_cast<float>(kGridRows) * 24.0f + 12.0f;
        screen.drawTextInRect(
            sgc::Rectf{16.0f, logY, m_screenW - 32.0f, 20.0f},
            "press log (newest first)", kMute, 14.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);

        for (std::size_t i = 0; i < m_log.size(); ++i)
        {
            const float ly = logY + 24.0f + static_cast<float>(i) * 16.0f;
            if (ly + 14.0f > m_screenH) { break; }
            screen.drawTextInRect(
                sgc::Rectf{16.0f, ly, m_screenW - 32.0f, 16.0f},
                m_log[i].c_str(), kInk, 14.0f,
                mitiru::Screen::TextAlignH::Left,
                mitiru::Screen::TextAlignV::Top);
        }
    }

    std::array<bool, mitiru::InputState::MAX_KEYS> m_keysDown{};
    std::vector<std::string> m_log{};
    int   m_lastVk{-1};
    float m_mouseX{0.0f};
    float m_mouseY{0.0f};
    bool  m_mouseL{false};
    bool  m_mouseR{false};
    bool  m_mouseM{false};
    float m_screenW{900.0f};
    float m_screenH{600.0f};
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    mitiru::Engine    engine;
    InputSampleGame   game;

    mitiru::EngineConfig cfg;
    cfg.title                = "mitiru_subsys_input";
    cfg.windowWidth          = 900;
    cfg.windowHeight         = 600;
    cfg.minWindowWidth       = 480;   // grid + mouse panel が潰れない floor (resize 安全)
    cfg.minWindowHeight      = 360;
    cfg.vsync                = true;
    cfg.enableCef            = false;
    cfg.fontAtlasRanges      = mitiru::EngineConfig::FontAtlas::Latin;
    cfg.useLogicalWindowSize = true;
    cfg.backgroundColor      = kPaperBg;

    engine.run(game, cfg);
    return 0;
}
