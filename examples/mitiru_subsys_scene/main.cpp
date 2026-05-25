// mitiru_subsys_scene — axis 3 (全 system 単独起動) の P3 成果物。
//
// ゲームロジック依存・CEF・audio なしで scene loop を起動する。
// 12 entity の最小 "scene" を毎フレーム更新:
//   - 各 entity は独立した (vel, angularSpeed) を持つ
//   - 位置を積分し、playfield rect の縁で反射
//   - 角度を積分し、内側の ink dot のオフセットで表現
//
// 狙いは完全な ECS ではなく、エンジンの Game/update/draw 契約だけで、
// 残りの stack が無くても per-frame の scene loop が回ること。
//
// 操作: ESC で終了。

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <mitiru/Mitiru.hpp>

namespace {

// ── Saturn パレット (他の subsys デモと揃える) ─────────────────────────────
constexpr sgc::Colorf kPaperBg     {0.784f, 0.784f, 0.784f, 1.0f};  // #c8c8c8
constexpr sgc::Colorf kPaperHi     {0.878f, 0.878f, 0.878f, 1.0f};  // #e0e0e0 entity 塗り
constexpr sgc::Colorf kPaperEdge   {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 墨の縁
constexpr sgc::Colorf kInk         {0.063f, 0.063f, 0.063f, 1.0f};  // #101010 文字/dot
constexpr sgc::Colorf kMute        {0.290f, 0.290f, 0.290f, 1.0f};  // #4a4a4a
constexpr sgc::Colorf kBevelHi     {1.000f, 1.000f, 1.000f, 1.0f};  // 白の凸
constexpr sgc::Colorf kBevelLo     {0.510f, 0.510f, 0.510f, 1.0f};  // #828282 凹
constexpr sgc::Colorf kSaturnRed   {0.784f, 0.0f,   0.173f, 1.0f};  // #c8002c

constexpr int   kEntityCount = 12;
constexpr float kHeaderH     = 56.0f;
constexpr float kFooterH     = 32.0f;
constexpr float kEntitySize  = 56.0f;

struct SceneEntity
{
    float x{0.0f};
    float y{0.0f};
    float vx{0.0f};
    float vy{0.0f};
    float angle{0.0f};
    float rotSpeed{0.0f};
};

// 決定的 LCG。実行間でレイアウトを安定させる (<random> 依存なし)。
struct Rng
{
    std::uint32_t state{0x12345u};
    float next01()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0xFFFFFFu) / 16777216.0f;
    }
    float range(float lo, float hi) { return lo + (hi - lo) * next01(); }
};

class SceneSampleGame final : public mitiru::Game
{
public:
    void update(float dt) override
    {
        if (!m_spawned) { spawn(); m_spawned = true; }

        if (hasInput() && input().isKeyJustPressed(mitiru::KeyCode::Escape))
        {
            if (auto* eng = engine()) { eng->requestStop(); }
            return;
        }

        const float left   = 12.0f;
        const float top    = kHeaderH;
        const float right  = m_screenW - 12.0f - kEntitySize;
        const float bottom = m_screenH - kFooterH - kEntitySize;

        for (auto& e : m_entities)
        {
            e.x += e.vx * dt;
            e.y += e.vy * dt;
            if (e.x < left)   { e.x = left;   e.vx = -e.vx; }
            if (e.x > right)  { e.x = right;  e.vx = -e.vx; }
            if (e.y < top)    { e.y = top;    e.vy = -e.vy; }
            if (e.y > bottom) { e.y = bottom; e.vy = -e.vy; }
            e.angle += e.rotSpeed * dt;
        }
        ++m_frame;
    }

    void draw(mitiru::Screen& screen) override
    {
        m_screenW = static_cast<float>(screen.width());
        m_screenH = static_cast<float>(screen.height());

        screen.clear(kPaperBg);
        drawHeader(screen);
        for (const auto& e : m_entities) { drawEntity(screen, e); }
        drawFooter(screen);
    }

    [[nodiscard]] mitiru::Size layout(int outsideW, int outsideH) override
    {
        return {outsideW, outsideH};
    }

private:
    void spawn()
    {
        m_entities.clear();
        m_entities.reserve(kEntityCount);
        Rng rng;
        for (int i = 0; i < kEntityCount; ++i)
        {
            SceneEntity e;
            e.x = rng.range(20.0f, m_screenW - 20.0f - kEntitySize);
            e.y = rng.range(kHeaderH + 8.0f,
                            m_screenH - kFooterH - 8.0f - kEntitySize);
            e.vx = rng.range(-60.0f, 60.0f);
            e.vy = rng.range(-50.0f, 50.0f);
            e.angle    = rng.range(0.0f, 6.2831f);
            e.rotSpeed = rng.range(-2.0f, 2.0f);
            m_entities.push_back(e);
        }
    }

    void drawEntity(mitiru::Screen& screen, const SceneEntity& e)
    {
        const sgc::Rectf body{e.x, e.y, kEntitySize, kEntitySize};
        // Saturn red の外枠、paper-hi の内塗り、bevel の凹凸。
        screen.drawRect(body, kSaturnRed);
        screen.drawRect(sgc::Rectf{e.x + 2, e.y + 2,
                                   kEntitySize - 4, kEntitySize - 4},
                        kPaperHi);
        // 上/左に白 1px、下/右に灰 1px の凹凸 (クロームの bevel)
        screen.drawRect(sgc::Rectf{e.x + 2, e.y + 2, kEntitySize - 4, 1}, kBevelHi);
        screen.drawRect(sgc::Rectf{e.x + 2, e.y + 2, 1, kEntitySize - 4}, kBevelHi);
        screen.drawRect(sgc::Rectf{e.x + 2, e.y + kEntitySize - 3,
                                   kEntitySize - 4, 1}, kBevelLo);
        screen.drawRect(sgc::Rectf{e.x + kEntitySize - 3, e.y + 2,
                                   1, kEntitySize - 4}, kBevelLo);
        // 向きの指標: angle の (cos,sin) だけずらした ink dot。
        const float cx = e.x + kEntitySize * 0.5f;
        const float cy = e.y + kEntitySize * 0.5f;
        const float dotR = 3.0f;
        const float armLen = kEntitySize * 0.30f;
        const float dx = std::cos(e.angle) * armLen;
        const float dy = std::sin(e.angle) * armLen;
        screen.drawRect(sgc::Rectf{cx + dx - dotR, cy + dy - dotR,
                                   dotR * 2.0f, dotR * 2.0f}, kInk);
    }

    void drawHeader(mitiru::Screen& screen)
    {
        char title[64];
        std::snprintf(title, sizeof(title),
                      "scene subsystem - %d entities", kEntityCount);
        screen.drawTextInRect(
            sgc::Rectf{16.0f, 14.0f, m_screenW - 32.0f, 24.0f},
            title, kInk, 24.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
        // ヘッダの下線。
        screen.drawRect(sgc::Rectf{0.0f, kHeaderH - 1.0f, m_screenW, 1.0f},
                        kPaperEdge);
    }

    void drawFooter(mitiru::Screen& screen)
    {
        screen.drawRect(sgc::Rectf{0.0f, m_screenH - kFooterH, m_screenW, 1.0f},
                        kPaperEdge);
        screen.drawTextInRect(
            sgc::Rectf{16.0f, m_screenH - kFooterH + 8.0f,
                       m_screenW - 32.0f, 16.0f},
            "press ESC to quit",
            kMute, 16.0f,
            mitiru::Screen::TextAlignH::Left,
            mitiru::Screen::TextAlignV::Top);
    }

    std::vector<SceneEntity> m_entities;
    float         m_screenW{900.0f};
    float         m_screenH{600.0f};
    std::uint64_t m_frame{0};
    bool          m_spawned{false};
};

}  // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    mitiru::Engine     engine;
    SceneSampleGame    game;

    mitiru::EngineConfig cfg;
    cfg.title                = "mitiru_subsys_scene";
    cfg.windowWidth          = 900;
    cfg.windowHeight         = 600;
    cfg.vsync                = true;
    cfg.enableCef            = false;
    cfg.fontAtlasRanges      = mitiru::EngineConfig::FontAtlas::Latin;
    cfg.useLogicalWindowSize = true;
    cfg.backgroundColor      = kPaperBg;

    engine.run(game, cfg);
    return 0;
}
