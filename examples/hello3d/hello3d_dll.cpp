// hello3d — 3D ゲームのデモ (CPU ソフトレンダラーを 2D に合成する)。
//
// MitiruEngine の正面は 2D + HTML/CSS ですが、3D も「2D の上にのせる絵」として
// 出せます。3D シーンを CPU のソフトレンダラー (DeferredPipeline) で 1 枚の画像に
// 焼き、それを Screen::drawPixelGrid で画面へ貼るだけ。GPU の重い 3D 経路に頼らず、
// 2D と同じ並びで 3D が混ざります。
//
// 状態は「数値だけ」(時刻と立方体の位置) なので決定的 — 録画・巻き戻しもそのまま
// 効きます。3D を描くための道具 (メッシュ・パイプライン) は GameMemory に入れられない
// ので、DLL のグローバルに置きます (state ではなく「描く部品」)。
//
// 実行:  mitiru_host.exe hello3d/hello3d.dll
// 操作:  矢印キーで中央の立方体を動かす。

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <mitiru.hpp>                          // 作者向けの正面 API (Input/Key/Screen/color/MITIRU_GAME)
#include <mitiru/render/DeferredPipeline.hpp>  // CPU ソフトレンダラー (shadow + GBuffer + lighting)
#include <mitiru/render/RenderTexture.hpp>     // ソフトレンダラーの出力先 (CPU ピクセル)
#include <mitiru/render/Scene3D.hpp>           // メッシュ + ライトのコンテナ
#include <mitiru/render/Mesh.hpp>              // createCube / createSphere / createPlane
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/ShadowMap.hpp>

using namespace mitiru;
namespace r = mitiru::render;

namespace {

// 画面は標準の 1280x720。3D は内部 512x288 (16:9) で焼いて画面へ拡大する。
constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;
constexpr int   kRtW     = 512;
constexpr int   kRtH     = 288;
constexpr sgc::Colorf kSky{0.09f, 0.10f, 0.13f, 1.0f};

// ── 3D を描くための道具 (state ではない → グローバルに置く) ───────────────
r::DeferredPipeline g_pipe;
r::RenderTexture    g_frame{kRtW, kRtH};
r::Scene3D          g_scene;
const r::Mesh       g_cube   = r::Mesh::createCube(1.0f);
const r::Mesh       g_sphere = r::Mesh::createSphere(0.55f, 24);
const r::Mesh       g_ground = r::Mesh::createPlane(14.0f, 14.0f);
bool                g_ready  = false;

// パイプラインを 1 回だけ初期化する。影マップは足場をちょうど覆う大きさに絞り、
// 解像度を上げてくっきりさせる。
void ensurePipeline()
{
    if (g_ready) { return; }
    g_pipe.initialize(kRtW, kRtH, r::ShadowMapConfig{1024, 12.0f, 0.1f, 100.0f});
    g_pipe.setClearColor(kSky);
    g_ready = true;
}

// 時刻 t と立方体位置からシーンを組み、g_frame にソフトレンダリングする。
// 同じ引数なら必ず同じ絵になる (決定的)。
void renderScene(float t, float cubeX, float cubeZ)
{
    // カメラ: 原点まわりをゆっくり周回し、見下ろす。
    r::Camera3D cam;
    const float yaw = t * 0.30f;
    constexpr float radius = 11.0f;
    cam.setPosition({std::sin(yaw) * radius, 6.5f, std::cos(yaw) * radius});
    cam.setTarget({0.0f, 0.8f, 0.0f});
    cam.setFov(1.0f);
    cam.setAspectRatio(static_cast<float>(kRtW) / static_cast<float>(kRtH));
    cam.setNearClip(0.1f);
    cam.setFarClip(100.0f);

    g_scene.clear();

    // 地面 (足場)。これより外へカメラが回らないので near クリップ問題が起きない。
    r::Material ground = r::Material::defaultMaterial();
    ground.diffuse = sgc::Colorf{0.34f, 0.37f, 0.44f, 1.0f};
    g_scene.addObject({&g_ground, ground, {0.0f, 0.0f, 0.0f},
                       {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});

    // 奥行きを出す散在ピラー (立方体を縦に伸ばして使い回す)。地面に影を落とす。
    struct Pillar { float x, z, h; sgc::Colorf col; };
    static const Pillar pillars[] = {
        {-4.0f, -3.0f, 2.6f, {0.32f, 0.55f, 0.52f, 1.0f}},
        { 4.5f, -1.8f, 1.7f, {0.45f, 0.40f, 0.62f, 1.0f}},
        {-3.2f,  3.6f, 1.2f, {0.55f, 0.50f, 0.40f, 1.0f}},
        { 3.6f,  3.2f, 2.1f, {0.30f, 0.45f, 0.60f, 1.0f}},
        {-5.4f,  0.4f, 1.0f, {0.50f, 0.42f, 0.45f, 1.0f}},
    };
    for (const auto& p : pillars)
    {
        r::Material m = r::Material::defaultMaterial();
        m.diffuse = p.col;
        g_scene.addObject({&g_cube, m, {p.x, p.h * 0.5f, p.z},
                           {0.0f, 0.0f, 0.0f}, {0.9f, p.h, 0.9f}});
    }

    // 主役の立方体 — プレイヤー操作で移動、Y 軸まわりに回転、地面に接地。
    r::Material cube = r::Material::defaultMaterial();
    cube.diffuse = sgc::Colorf{0.93f, 0.33f, 0.24f, 1.0f};
    constexpr float cubeSize = 1.3f;
    g_scene.addObject({&g_cube, cube, {cubeX, cubeSize * 0.5f, cubeZ},
                       {0.0f, t * 1.0f, 0.0f},
                       {cubeSize, cubeSize, cubeSize}});

    // 立方体を追従して周回する球 2 つ (青・黄)。上下にも揺れる。
    for (int i = 0; i < 2; ++i)
    {
        const float a = t * 1.2f + static_cast<float>(i) * 3.14159265f;
        r::Material s = r::Material::defaultMaterial();
        s.diffuse = (i == 0) ? sgc::Colorf{0.30f, 0.58f, 0.95f, 1.0f}
                             : sgc::Colorf{0.96f, 0.82f, 0.27f, 1.0f};
        g_scene.addObject({&g_sphere, s,
                           {cubeX + std::cos(a) * 2.9f,
                            0.9f + std::sin(t * 2.0f + static_cast<float>(i)) * 0.35f,
                            cubeZ + std::sin(a) * 2.9f},
                           {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
    }

    // 斜め上からの主光源 (やや暖色)。
    g_scene.addLight(r::Light::directional(
        {-0.5f, -1.1f, -0.4f}, sgc::Colorf{1.0f, 0.96f, 0.88f, 1.0f}));

    g_pipe.render(g_scene, cam, g_frame);
}

}  // namespace

// ── ゲームの状態 (flat POD — 数値だけ) ──────────────────────────────────
struct Hello3D
{
    float t     = 0.0f;   // 経過時間 (カメラ周回・回転・球の位相を決める)
    float cubeX = 0.0f;   // 主役の立方体の位置
    float cubeZ = 0.0f;

    void init()
    {
        t = 0.0f; cubeX = 0.0f; cubeZ = 0.0f;
    }

    void update(Input in, float dt)
    {
        t += dt;
        constexpr float speed = 4.5f;   // ワールド単位/秒
        constexpr float limit = 3.5f;   // 足場からはみ出さない範囲
        if (in.down(Key::Left))  { cubeX -= speed * dt; }
        if (in.down(Key::Right)) { cubeX += speed * dt; }
        if (in.down(Key::Up))    { cubeZ -= speed * dt; }
        if (in.down(Key::Down))  { cubeZ += speed * dt; }
        cubeX = std::clamp(cubeX, -limit, limit);
        cubeZ = std::clamp(cubeZ, -limit, limit);
    }

    void draw(Screen& s)
    {
        ensurePipeline();
        renderScene(t, cubeX, cubeZ);

        // 焼いた 3D を画面へ。drawPixelGrid は毎フレーム変わる CPU バッファ向け
        // (内容ハッシュで自動再アップロード)。これで 3D が 2D の 1 枚絵になる。
        s.drawPixelGrid(
            Rect{0.0f, 0.0f, kScreenW, kScreenH},
            reinterpret_cast<const std::uint32_t*>(g_frame.pixels().data()),
            g_frame.width(), g_frame.height());

        // HUD は 2D。3D の上に普通に重なる。
        s.text("MitiruEngine 3D", 28, 22, color::White, 30);
        s.text("arrow keys move the cube", 28,
               static_cast<int>(kScreenH) - 40, rgba(190, 200, 215, 255), 18);
    }
};

MITIRU_GAME(Hello3D)
