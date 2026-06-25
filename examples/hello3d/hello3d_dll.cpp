// hello3d — 3D ゲームのデモ。
//
// 3D は draw(Screen&) の中で s.camera3D → s.drawMesh を呼ぶだけ。エンジンの GPU
// 3D レンダラー (MSAA・影・トゥーン調) が描き、2D の文字 (HUD) はその上に重なる。
// 状態は「数値だけ」(時刻と立方体の位置) なので決定的 — ホットリロード・録画も効く。
//
// 実行:  mitiru_host.exe hello3d/hello3d.dll
// 操作:  矢印キーで中央の立方体を動かす。

#include <algorithm>
#include <cmath>

#include <mitiru.hpp>

using namespace mitiru;

namespace {
constexpr float kScreenH = 720.0f;
}  // namespace

// ── ゲームの状態 (flat POD — 数値だけ) ──────────────────────────────────
struct Hello3D
{
    float t     = 0.0f;   // 経過時間 (カメラ周回・回転・球の位相)
    float cubeX = 0.0f;   // 主役の立方体の位置
    float cubeZ = 0.0f;

    void init() { t = 0.0f; cubeX = 0.0f; cubeZ = 0.0f; }

    void update(Input in, float dt)
    {
        t += dt;
        constexpr float speed = 4.5f;   // ワールド単位/秒
        constexpr float limit = 3.8f;   // 足場からはみ出さない範囲
        if (in.down(Key::Left))  { cubeX -= speed * dt; }
        if (in.down(Key::Right)) { cubeX += speed * dt; }
        if (in.down(Key::Up))    { cubeZ -= speed * dt; }
        if (in.down(Key::Down))  { cubeZ += speed * dt; }
        cubeX = std::clamp(cubeX, -limit, limit);
        cubeZ = std::clamp(cubeZ, -limit, limit);
    }

    void draw(Screen& s)
    {
        s.clear(hex(0x111521));   // 3D の背景 (足場の周りの闇)

        // カメラ: 足場のまわりをゆっくり周回し、見下ろす。
        const float yaw = t * 0.30f;
        s.camera3D({std::sin(yaw) * 13.0f, 8.5f, std::cos(yaw) * 13.0f},  // 視点
                   {0.0f, 0.4f, 0.0f},                                    // 注視点
                   54.0f);                                                // 縦画角(度)
        s.light3D({-0.6f, -0.95f, -0.45f}, hex(0xFFF4E6));               // 斜め上から暖色光 (影が出る角度)

        // 足場 (14x14 の薄い板)。plane は片面なので閉じた cube を薄く使う。
        // 周りは闇 — 浮島のように見える。上面が y=0 になるよう少し沈める。
        s.drawMesh("cube", {0.0f, -0.15f, 0.0f}, {14.0f, 0.3f, 14.0f},
                   {0.0f, 0.0f, 0.0f}, hex(0x3A4250));

        // 奥行きを出す散在ピラー (立方体を縦に伸ばす)。
        struct Pillar { float x, z, h; uint32_t col; };
        static const Pillar pillars[] = {
            {-4.2f, -3.0f, 2.8f, 0x4F8F86}, { 4.6f, -1.9f, 1.8f, 0x6F5FA0},
            {-3.4f,  3.6f, 1.3f, 0x8C8060}, { 3.7f,  3.2f, 2.2f, 0x4C73A0},
            {-5.4f,  0.5f, 1.1f, 0x84707A}, { 5.4f,  1.2f, 1.7f, 0x607048},
        };
        for (const auto& p : pillars)
        {
            s.drawMesh("cube", {p.x, p.h * 0.5f, p.z}, {0.9f, p.h, 0.9f},
                       {0.0f, 0.0f, 0.0f}, hexa((p.col << 8) | 0xFF));
        }

        // 主役の立方体 — プレイヤー操作で移動、Y 軸まわりに回転、接地。
        constexpr float cubeSize = 1.4f;
        s.drawMesh("cube", {cubeX, cubeSize * 0.5f, cubeZ},
                   {cubeSize, cubeSize, cubeSize},
                   {0.0f, t * 60.0f, 0.0f}, hex(0xEA5535));

        // 立方体を追従して周回する球 2 つ (青・黄)。
        for (int i = 0; i < 2; ++i)
        {
            const float a = t * 1.2f + static_cast<float>(i) * 3.14159265f;
            const float bob = 0.9f + std::sin(t * 2.0f + static_cast<float>(i)) * 0.35f;
            const uint32_t col = (i == 0) ? 0x4C95F2 : 0xF5C842;
            s.drawMesh("sphere",
                       {cubeX + std::cos(a) * 2.7f, bob, cubeZ + std::sin(a) * 2.7f},
                       {1.2f, 1.2f, 1.2f}, {0.0f, 0.0f, 0.0f},
                       hexa((col << 8) | 0xFF));
        }

        // ── 半透明キューブ 3 枚 (OIT デモ) ──
        // alpha<1 なので Weighted-Blended OIT パスへ回り、重なっても描画順に依存せず
        // 正しく合成される。不透明（足場・ピラー）には深度で正しく遮蔽される。
        s.drawMesh("cube", {-0.7f, 2.6f,  0.3f}, {1.9f, 1.9f, 1.9f}, {0,0,0}, hexa(0xE03B3B82)); // 赤
        s.drawMesh("cube", { 0.0f, 2.6f,  0.0f}, {1.9f, 1.9f, 1.9f}, {0,0,0}, hexa(0x3BE05B82)); // 緑
        s.drawMesh("cube", { 0.7f, 2.6f, -0.3f}, {1.9f, 1.9f, 1.9f}, {0,0,0}, hexa(0x3B7BE082)); // 青

        // ── 2D の HUD (3D の上に重なる) ──
        s.text("MitiruEngine 3D", 28, 22, color::White, 30);
        s.text("arrow keys move the cube", 28,
               static_cast<int>(kScreenH) - 40, rgba(200, 210, 225, 255), 18);
    }
};

MITIRU_GAME(Hello3D)
