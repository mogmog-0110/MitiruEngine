// effekseer_fx。「Effekseer で作ったエフェクトを、時刻を渡して描く」章
//   エフェクト (assets/Laser01.efkefc) は Effekseer のエディタ (別のツール) で作ったもの。
//   ゲームはいつ・どこで出したかを GameMemory に覚え、毎フレーム「出してから何秒か」を drawModel に渡す。
//   姿は経過秒だけで決まるので、巻き戻すと過去の時刻のエフェクトがそのまま出る。
// あそびかた: Space でカーソルの位置から 1 本撃つ。矢印でカーソルを動かす。放っておいても 1 秒ごとに撃つ。
// この章で使う関数: drawModel (時刻つき版で .efkefc を描く) / drawMesh / camera3D
// ビルド: -DMITIRU_WITH_EFFEKSEER=ON の時だけ作られる (docs/EFFEKSEER.md)

#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

namespace
{
constexpr int kShots = 6;          // 同時に出ている発射の上限 (古いものから上書き)
constexpr float kLifeSec = 2.0f;   // 1 発を描く長さ
constexpr float kAutoSec = 1.0f;
// 発射ごとに別の key を渡すと、同じエフェクトを同時に何本出しても、前フレームの続きと取り違えない
constexpr const char* kShotKey[kShots] = {"0", "1", "2", "3", "4", "5"};
} // namespace

struct EffekseerFx
{
	float t = 0.0f;
	float nextAuto = 0.2f;
	float cursorX = 0.0f, cursorZ = 0.0f;
	float shotX[kShots] = {}, shotZ[kShots] = {}, shotYaw[kShots] = {};
	float shotAt[kShots] = {-100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f};   // 撃った時刻
	int nextShot = 0;

	void init() { *this = EffekseerFx{}; }

	void fire(float x, float z, float yawDeg)
	{
		shotX[nextShot] = x; shotZ[nextShot] = z; shotYaw[nextShot] = yawDeg;
		shotAt[nextShot] = t;
		nextShot = (nextShot + 1) % kShots;
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		t += dt;
		constexpr float speed = 4.0f;
		if (in.down(Key::Left))  { cursorX -= speed * dt; }
		if (in.down(Key::Right)) { cursorX += speed * dt; }
		if (in.down(Key::Up))    { cursorZ -= speed * dt; }
		if (in.down(Key::Down))  { cursorZ += speed * dt; }
		if (in.pressed(Key::Space)) { fire(cursorX, cursorZ, 0.0f); }
		if (t >= nextAuto)
		{
			// 自動の発射は円周上を回る。位置も向きも t だけから決まる
			const float a = nextAuto * 1.7f;
			fire(std::cos(a) * 3.5f, std::sin(a) * 3.5f, a * 57.29578f);
			nextAuto += kAutoSec;
		}
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0x10141C));
		s.camera3D({0.0f, 7.5f, 11.0f}, {0.0f, 0.5f, 0.0f}, Deg{50.0f});
		s.light3D({-0.4f, -0.9f, -0.3f}, hex(0xC8D2E6));
		s.skybox3D(hex(0x0E1420), hex(0x1C2230));
		s.drawMesh("plane", {0.0f, 0.0f, 0.0f}, {16.0f, 1.0f, 16.0f}, {}, hex(0x2A3240));
		s.drawMesh("cube", {0.0f, 0.75f, -2.5f}, {1.5f, 1.5f, 1.5f}, {}, hex(0x5A6478));   // 奥行きの手がかり
		s.drawMesh("cube", {cursorX, 0.05f, cursorZ}, {0.5f, 0.1f, 0.5f}, {}, hex(0xF2C14E));

		for (int i = 0; i < kShots; ++i)
		{
			const float age = t - shotAt[i];
			if (age < 0.0f || age > kLifeSec) { continue; }
			s.drawModel("effekseer_fx/assets/Laser01.efkefc", {shotX[i], 0.5f, shotZ[i]}, Deg{shotYaw[i]}, 0.25f,
			            kShotKey[i], age);
		}

		chapterTitle(s, "Effekseer");
		chapterControls(s, "矢印: カーソル　Space: 撃つ　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe effekseer_fx/effekseer_fx.dll
MITIRU_REFLECT_AUTO(EffekseerFx);

MITIRU_ASSERT_NO_PADDING(EffekseerFx);
MITIRU_GAME(EffekseerFx);
