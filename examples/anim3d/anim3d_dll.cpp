// anim3d — キャラクターを歩かせる (骨格アニメーション)
// 実行すると: 草原にキツネが立つ。WASD で歩かせると待機と歩きが滑らかに混ざる
// 関連 API: drawModelBlend / camera3D / light3D / skybox3D

#include <cmath>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

// キツネは Khronos サンプルの Fox (assets/fox/CREDITS.md、顔は +z 向き)。glb の中の
// クリップ名 ("Survey" = 待機、"Walk" = 歩き) を時間 (秒) と一緒に渡すだけで動く。
// アニメの時間は自分の状態で持って毎フレーム足す — これが唯一の約束。
struct Anim3D
{
	float px = 0.0f, pz = 0.0f;      // キツネの位置 (m)
	float yawDeg = 180.0f;           // 体の向き
	float animT = 0.0f;              // アニメ時間 (秒)。自分で t += dt する
	float walkMix = 0.0f;            // 0 = 待機だけ、1 = 歩きだけ
	float camX = 0.0f, camZ = 0.0f;  // カメラの注視点 (少し遅れて追う)

	void init() { *this = Anim3D{}; }

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }   // Esc で終わる

		// WASD (in.move() が WASD/矢印/スティックを合成)。カメラが斜め上から
		// 見るので、入力の上下左右を画面の向きに合わせて世界の軸へ 45° 回す
		const Stick m = in.move();
		const bool moving = (m.x * m.x + m.y * m.y) > 0.01f;
		if (moving)
		{
			constexpr float k = 0.70710678f;
			const float vx = (m.x + m.y) * k;
			const float vz = (-m.x + m.y) * k;
			constexpr float speed = 1.4f;   // m/s (Walk クリップの歩幅に合わせた速さ)
			px += vx * speed * dt;
			pz += vz * speed * dt;

			// 進む方向へ体を回す。一瞬で向きを変えず、1 秒 540 度までの旋回にする
			constexpr float kRad2Deg = 180.0f / 3.14159265f;
			float d = std::atan2(vx, vz) * kRad2Deg - yawDeg;
			while (d > 180.0f) { d -= 360.0f; }
			while (d < -180.0f) { d += 360.0f; }
			const float turn = 540.0f * dt;
			yawDeg += (d > turn) ? turn : (d < -turn) ? -turn : d;
		}

		// 待機 ↔ 歩き をおよそ 0.25 秒でなめらかに切り替える
		const float target = moving ? 1.0f : 0.0f;
		const float step = dt * 4.0f;
		const float diff = target - walkMix;
		walkMix += (diff > step) ? step : (diff < -step) ? -step : diff;

		// カメラの注視点はキツネを少し遅れて追う (歩き出しが画面で分かる)
		const float chase = (dt * 5.0f > 1.0f) ? 1.0f : dt * 5.0f;
		camX += (px - camX) * chase;  camZ += (pz - camZ) * chase;

		// アニメ時間を進める。状態はこの数字たちだけなので、巻き戻しても正しく戻る
		animT += dt;

		// 草原の外へ出ない
		px = (px < -8.0f) ? -8.0f : (px > 8.0f) ? 8.0f : px;
		pz = (pz < -8.0f) ? -8.0f : (pz > 8.0f) ? 8.0f : pz;
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xDCE9F5));   // 3D が使えない環境 (画面なしの自動テストなど) ではこの色のまま

		s.camera3D({camX + 2.8f, 2.0f, camZ + 2.8f}, {camX, 0.5f, camZ}, 50.0f);
		s.light3D({0.4f, -0.8f, 0.35f}, hex(0xFFF4E0));
		s.skybox3D(hex(0x6FA8E4), hex(0xF2F6FA));

		// 草原 (薄い箱。上面が y=0 — 影を受ける面) と、移動が分かる目印の岩
		s.drawMesh("cube", {0.0f, -0.15f, 0.0f}, {20.0f, 0.3f, 20.0f}, {0, 0, 0}, hex(0x9CC69B));
		struct Rock { float x, z, r; };
		static constexpr Rock kRocks[] = {{-5.5f, -3.0f, 0.7f}, {4.0f, -6.0f, 0.5f},
		                                  {6.5f, 2.5f, 0.9f},   {-3.0f, 5.5f, 0.6f},
		                                  {1.5f, 7.0f, 0.4f},   {-7.0f, 1.0f, 0.5f},
		                                  {2.5f, -2.0f, 0.35f}, {-1.5f, -6.5f, 0.55f}};
		for (const auto& r : kRocks)
		{
			s.drawMesh("cube", {r.x, r.r * 0.35f, r.z}, {r.r, r.r * 0.7f, r.r},
			           {0.0f, 25.0f, 0.0f}, hex(0x8FA08F));
		}

		// 待機 (Survey) と歩き (Walk) を walkMix で混ぜて描く。クリップ名は
		// Blender の Action 名がそのまま使える。時間はループ再生される
		s.drawModelBlend("anim3d/assets/fox/fox.glb", {px, 0.0f, pz}, yawDeg, 0.01f,
		                 "Survey", animT, "Walk", animT, walkMix);

		chapterTitle(s, "3D Character");
		chapterControls(s, "WASD: あるかせる　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe anim3d/anim3d.dll
MITIRU_GAME(Anim3D);
