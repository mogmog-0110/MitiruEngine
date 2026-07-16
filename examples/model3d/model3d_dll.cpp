// model3d — 大きな 3D モデル (glTF) の中を歩く
// 実行すると: 26 万ポリゴンの宮殿 (Sponza) を一人称で歩き回れる。マウスで見回し、WASD で移動
// 関連 API: drawModel / camera3D / hud.lockMouse / in.mouseDeltaX / skybox3D

#include <cmath>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

// 宮殿は Crytek Sponza (CC BY 3.0、assets/sponza/CREDITS.md)。drawModel の
// scale 0.01 でメートル単位の世界に置き、目の高さ 1.7m で歩く。
struct Model3D
{
	float px = -14.0f, py = 1.7f, pz = 0.0f;  // 目の位置 (m)
	float yawDeg = 90.0f;                     // 視線の左右 (90 = +X の廊下方向)
	float pitchDeg = 0.0f;                    // 視線の上下

	void init()
	{
		px = -14.0f; py = 1.7f; pz = 0.0f;
		yawDeg = 90.0f; pitchDeg = 0.0f;
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }   // Esc で終わる

		hud.lockMouse();   // 毎フレーム宣言でカーソルをロック (FPS 視線)

		// マウスで見回す。上下は真上・真下の手前で止める
		constexpr float sens = 0.15f;   // 1px あたりの回転角 (度)
		yawDeg   -= in.mouseDeltaX() * sens;
		pitchDeg -= in.mouseDeltaY() * sens;
		if (pitchDeg >  89.0f) { pitchDeg =  89.0f; }
		if (pitchDeg < -89.0f) { pitchDeg = -89.0f; }

		// WASD で視線の向きへ歩く (in.move() が WASD/矢印/スティックを合成する)
		constexpr float kDeg = 3.14159265f / 180.0f;
		const float fx = std::sin(yawDeg * kDeg), fz = std::cos(yawDeg * kDeg);
		const float rx = -fz, rz = fx;   // 視線の右手方向
		const Stick m = in.move();
		const float speed = in.down(Key::Shift) ? 6.0f : 3.0f;   // m/s (Shift で走る)
		px += (fx * -m.y + rx * m.x) * speed * dt;
		pz += (fz * -m.y + rz * m.x) * speed * dt;

		// 壁の外へ出ない範囲に収める (この章は当たり判定を持たない)
		if (px < -17.5f) { px = -17.5f; }
		if (px >  16.5f) { px =  16.5f; }
		if (pz < -10.0f) { pz = -10.0f; }
		if (pz >   9.5f) { pz =   9.5f; }
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xDCE9F5));   // 3D が使えない環境 (画面なしの自動テストなど) ではこの色のまま

		// 視線の向きを目の位置 + 単位ベクトルで camera3D に渡す
		constexpr float kDeg = 3.14159265f / 180.0f;
		const float cp = std::cos(pitchDeg * kDeg);
		const float dx = std::sin(yawDeg * kDeg) * cp;
		const float dy = std::sin(pitchDeg * kDeg);
		const float dz = std::cos(yawDeg * kDeg) * cp;
		s.camera3D({px, py, pz}, {px + dx, py + dy, pz + dz}, 70.0f);
		s.light3D({-0.4f, -0.85f, -0.3f}, hex(0xFFF4E0));
		s.skybox3D(hex(0x6FA8E4), hex(0xF2F6FA));

		// 26 万ポリゴンの宮殿 (glTF) をそのまま 1 行で。初回だけ隣に変換キャッシュを
		// 作り、以後の起動はそれを読む。詳細度 (LOD) は距離から自動で決まる
		s.drawModel("model3d/assets/sponza/sponza.gltf", {0.0f, 0.0f, 0.0f}, 0.0f, 0.01f);

		chapterTitle(s, "3D Model");
		chapterControls(s, "WASD: あるく　マウス: みまわす　Shift: はしる　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe model3d/model3d.dll
MITIRU_GAME(Model3D);
