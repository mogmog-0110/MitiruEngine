// scene3d ―「3D シーンを描く」章
//   3D の描画も、2D とまったく同じ draw(Screen&) の中で書ける。
//   ここでは太陽のような一方向の光と影、重なっても正しく見える半透明、空の背景を出す。
// あそびかた: 昼の空の下、床の上を矢印キーで動く赤い立方体。球 2 つが周りを回り、半透明の箱 3 枚が重なる。
// この章で使う関数: camera3D (カメラ) / light3D (光) / skybox3D (空) / drawMesh (立体を描く)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"  // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;

// 奥行きを感じさせるために散らして立てる柱 (x, z, 高さ, 色)。
struct Pillar { float x, z, h; std::uint32_t col; };
constexpr Pillar kPillars[] = {
	{-4.2f, -3.0f, 2.8f, 0x4F8F86}, { 4.6f, -1.9f, 1.8f, 0x6F5FA0},
	{-3.4f,  3.6f, 1.3f, 0x8C8060}, { 3.7f,  3.2f, 2.2f, 0x4C73A0},
	{-5.4f,  0.5f, 1.1f, 0x84707A}, { 5.4f,  1.2f, 1.7f, 0x607048},
};

// 状態は数値だけ。毎回同じ結果になるので、ホットリロードも記録／再生もそのまま使える。
struct Scene3D
{
	float t = 0.0f;                    // 経過時間 (カメラの周回・立方体の回転・球の動きに使う)
	float cubeX = 0.0f, cubeZ = 0.0f;  // 主役の立方体の位置

	void init() { t = 0.0f; cubeX = 0.0f; cubeZ = 0.0f; }

	void update(Input in, float dt)
	{
		t += dt;
		constexpr float speed = 4.5f, limit = 3.8f;   // 移動の速さと、床からはみ出さないための可動範囲
		if (in.down(Key::Left))  { cubeX -= speed * dt; }
		if (in.down(Key::Right)) { cubeX += speed * dt; }
		if (in.down(Key::Up))    { cubeZ -= speed * dt; }
		if (in.down(Key::Down))  { cubeZ += speed * dt; }
		cubeX = std::clamp(cubeX, -limit, limit);
		cubeZ = std::clamp(cubeZ, -limit, limit);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xEAF1F8));   // 3D が使えない環境 (画面なしの自動テストなど) ではこの色のままになる (白っぽい色)

		// カメラは床のまわりをゆっくり周回する。光は斜め上から差す昼白色で、影はその反対側に落ちる。
		const float yaw = t * 0.30f;
		s.camera3D({std::sin(yaw) * 13.0f, 8.5f, std::cos(yaw) * 13.0f}, {0.0f, 0.4f, 0.0f}, 54.0f);
		s.light3D({-0.6f, -0.95f, -0.45f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));   // 背景の空: 頭上は空色 → 地平線は白

		// 床 (14x14 の薄い板)。上面がちょうど y=0 になるよう少し沈めてある ― 影を受ける面。
		s.drawMesh("cube", {0.0f, -0.15f, 0.0f}, {14.0f, 0.3f, 14.0f}, {0, 0, 0}, hex(0xBFC8D4));
		for (const Pillar& p : kPillars)
		{
			s.drawMesh("cube", {p.x, p.h * 0.5f, p.z}, {0.9f, p.h, 0.9f}, {0, 0, 0}, hex(p.col));
		}

		// 主役の立方体 ― プレイヤーが動かし、Y 軸まわりに回転し、床に接して立つ。
		constexpr float cs = 1.4f;
		s.drawMesh("cube", {cubeX, cs * 0.5f, cubeZ}, {cs, cs, cs},
		           {0.0f, t * 60.0f, 0.0f}, hex(0xEA5535));

		for (int i = 0; i < 2; ++i)        // 立方体を追いかけて周りを回る球 (青・黄)
		{
			const float a   = t * 1.2f + static_cast<float>(i) * 3.14159265f;
			const float bob = 0.9f + std::sin(t * 2.0f + static_cast<float>(i)) * 0.35f;
			s.drawMesh("sphere", {cubeX + std::cos(a) * 2.7f, bob, cubeZ + std::sin(a) * 2.7f},
			           {1.2f, 1.2f, 1.2f}, {0, 0, 0}, (i == 0) ? hex(0x4C95F2) : hex(0xF5C842));
		}

		// 半透明の箱 3 枚 ― 透けている面は、重なっても描いた順番に関係なく正しい色に合成される
		// (手前にある不透明な床や柱には、奥行きに従ってちゃんと隠される)。
		s.drawMesh("cube", {-0.7f, 2.6f,  0.3f}, {1.9f, 1.9f, 1.9f}, {0, 0, 0}, hexa(0xE03B3B82));
		s.drawMesh("cube", { 0.0f, 2.6f,  0.0f}, {1.9f, 1.9f, 1.9f}, {0, 0, 0}, hexa(0x3BE05B82));
		s.drawMesh("cube", { 0.7f, 2.6f, -0.3f}, {1.9f, 1.9f, 1.9f}, {0, 0, 0}, hexa(0x3B7BE082));

		// 2D の HUD は 3D の絵の上に重ねて描かれる。
		chapterTitle(s, "3D Scene");
		chapterControls(s, "矢印: 立方体をうごかす");
	}
};

// 実行:  mitiru_host.exe scene3d/scene3d.dll
MITIRU_GAME(Scene3D);
