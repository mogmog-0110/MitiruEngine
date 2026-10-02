// action3d。「3D アクションの部品」の見本 (同じフレームで答える当たり判定、歩くキャラ、壁の手前に寄るカメラ)
//   当たり判定はこの DLL の中で解くので、問い合わせの答えは同じフレームに返る (host に頼むと 1 フレーム遅れる)。
//   ステージの箱は、描画と当たり判定で同じ表 kBlocks を使う。
// あそびかた: 箱と階段の小さな庭を、橙の箱のキャラが歩く。階段を上り、上下する桃色の足場に乗れる。
//   カメラは奥の壁や柱の手前へ寄り、壁が無くなると少し待ってから戻る。跳ぶと光の筋が残り、足場は灯りを持つ。
//   着地すると土ぼこりが舞い、床に跡が残る。高い所から落ちると画面が一瞬ぶれる。
// この章で使う関数: CollisionLevelBuilder / CollisionWorld / stepCharacter / updateCameraRig / InputBuffer /
//   camera3D (上向き・近い面・遠い面) / pointLight3D / drawTrail / decals3D / particles3D / hitFeel3D

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mitiru.hpp>
#include <mitiru/action/CameraRig.hpp>
#include <mitiru/action/CharacterController.hpp>
#include <mitiru/action/InputBuffer.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace act = mitiru::action;

struct Block { act::Vec3 lo, hi; std::uint32_t col; };
constexpr Block kBlocks[] = {
	{{-8, -1, -8}, {8, 0, 8}, 0xC9D1DC},                                    // 床
	{{-8, 0, -8}, {8, 3, -7.5f}, 0x8C9BB0},                                 // 奥の壁
	{{1.0f, 0, -1.5f}, {1.4f, 0.25f, 1.5f}, 0xB7A27F}, {{1.4f, 0, -1.5f}, {1.8f, 0.5f, 1.5f}, 0xB7A27F},
	{{1.8f, 0, -1.5f}, {2.2f, 0.75f, 1.5f}, 0xB7A27F}, {{2.2f, 0, -1.5f}, {2.6f, 1.0f, 1.5f}, 0xB7A27F},
	{{2.6f, 0, -1.5f}, {6.0f, 1.25f, 1.5f}, 0x9C8A6A},                       // 階段の上の台
	{{-8, 0, -1.5f}, {-5.5f, 2.2f, 1.5f}, 0x6F8F7A},                        // 足場で上る高い台
	{{-1.6f, 0, -6.4f}, {-0.8f, 3, -5.6f}, 0x4C73A0},                        // 柱
};

// 作ったら変えない地形。DLL を読み込むたび (ホットリロードを含む) に作り直す
const act::CollisionLevel kLevel = [] {
	act::CollisionLevelBuilder b;
	for (const Block& k : kBlocks) { b.addBox(k.lo, k.hi, 0); }
	return b.build();
}();

constexpr act::CharacterConfig kHero{};
constexpr act::CameraRigConfig kCam{};
constexpr act::ActionWindows kWindows{{6}};   // 行動 0 (跳ぶ) は 6 tick まで先に押しておける
constexpr float kRunSpeed = 4.5f, kJumpSpeed = 8.0f;
constexpr int kTrailMax = 16;   // 光の筋に残す点の数 (1 フレーム 1 点)

// 状態は数値だけ (まるごとコピーできる形)。巻き戻しと記録／再生がそのまま効く
struct Action3D
{
	act::CharacterState hero{};
	act::CameraRigState cam{};
	act::CameraView     view{};
	act::MovingBody     lift{};
	act::InputBuffer<8> buffer{};
	act::Vec3           facing{0, 0, 1};
	float               t = 0.0f;
	std::uint32_t       frame = 0;
	act::Vec3           trail[kTrailMax]{};   // 跳んでいる間の通り道 (新しい順)。着地すると古い方から消える
	std::uint32_t       trailLen = 0;
	act::Vec3           landAt{};             // 最後に着地した所と時刻、落ちてきた速さ。跡と土ぼこりはここから毎フレーム描く
	std::uint32_t       landFrame = 0;
	float               landSpeed = 0.0f;

	void init()
	{
		hero.position      = {0.0f, kHero.skinWidth, -3.0f};
		hero.grounded      = 1;
		lift.halfExtents   = {1.0f, 0.15f, 1.0f};
		lift.pose.position = lift.previousPose.position = {-4.4f, 0.15f, 0.0f};
	}

	void update(Input in, float dt)
	{
		t += dt;
		++frame;
		lift.previousPose  = lift.pose;   // 足場は上下する。乗っているキャラは足場が動いた分だけ運ばれる
		lift.pose.position = {-4.4f, 0.15f + 1.05f * (1.0f - std::cos(t * 0.9f)), 0.0f};
		const act::MovingBody     bodies[] = {lift};
		const act::CollisionWorld world(&kLevel, bodies);

		const Stick     m   = in.move();   // カメラから見た前後左右へ歩く (move の y は下が正)
		const act::Vec3 fwd = act::cameraRigForward(cam.yaw, 0.0f), right = act::cameraRigRight(fwd);
		const act::Vec3 run = (right * m.x - fwd * m.y) * kRunSpeed;
		if (act::lengthSq(run) > 0.01f) { facing = act::normalizeOr(run, facing); }
		if (in.pressed(Key::Space)) { buffer.press(0, frame); }
		const bool jump = hero.grounded != 0 && buffer.consume(0, frame, kWindows);
		const float fall = -hero.velocity.y;
		act::stepCharacter(hero, kHero, world, {run, jump ? kJumpSpeed : 0.0f}, dt);
		if ((hero.events & act::charevent::kLanded) != 0 && fall > 2.0f) { landAt = hero.position; landFrame = frame; landSpeed = fall; }
		updateTrail();

		act::CameraRigInput ci;
		ci.follow = hero.position;
		ci.orbitX = (in.down(Key::E) ? 1.0f : 0.0f) - (in.down(Key::Q) ? 1.0f : 0.0f) + in.rightStick().x;
		ci.orbitY = in.rightStick().y;
		view      = act::updateCameraRig(cam, kCam, ci, world, dt);
	}

	void updateTrail()
	{
		if (hero.grounded != 0) { trailLen = (trailLen > 0) ? trailLen - 1 : 0; return; }
		for (int i = kTrailMax - 1; i > 0; --i) { trail[i] = trail[i - 1]; }
		trail[0] = hero.position + act::Vec3{0, 0.8f, 0} - facing * 0.45f;   // 背中のすぐ後ろ
		trailLen = (trailLen < kTrailMax) ? trailLen + 1 : kTrailMax;
	}

	void drawTrail(Screen& s) const
	{
		render::TrailPointPod pts[kTrailMax];
		for (std::uint32_t i = 0; i < trailLen; ++i)
		{
			const act::Vec3 p = trail[i];
			pts[i] = {{p.x, p.y, p.z}, 0.35f, {1.0f, 0.55f, 0.15f, 1.0f}, static_cast<float>(i) / kTrailMax};
		}
		render::TrailStylePod style;
		style.blend = 1;   // 明るい空の前でも見えるよう、足し合わせでなく色を重ねる
		s.drawTrail(pts, trailLen, style);   // 2 点より少なければ何も描かない
	}

	// 跡・土ぼこり・ぶれは描くだけのもの。着地の時刻からの経過秒で毎フレーム渡すので、巻き戻すとその時刻の姿に戻る
	void drawLanding(Screen& s) const
	{
		if (landFrame == 0) { return; }
		const float age = static_cast<float>(frame - landFrame) / 60.0f;
		render::DecalDesc mark = render::decalOnSurface(landAt, {0, 1, 0}, 1.2f);
		mark.color[0] = mark.color[1] = mark.color[2] = 0.35f; mark.color[3] = 0.8f;
		mark.age = age; mark.lifetime = 6.0f; mark.fadeOut = 2.0f;
		s.decals3D(&mark, 1);
		if (age > 1.0f) { return; }
		render::ParticleEmitterDesc dust;
		dust.key = 1; dust.seed = landFrame; dust.age = age;
		dust.position[0] = landAt.x; dust.position[1] = landAt.y + 0.1f; dust.position[2] = landAt.z;
		dust.burst = 64; dust.spreadDeg = 85.0f; dust.speedMin = 1.5f; dust.speedMax = 3.5f;
		dust.lifeMin = 0.4f; dust.lifeMax = 0.9f; dust.gravity[1] = -3.0f; dust.drag = 2.5f;
		dust.size[0] = 0.2f; dust.size[1] = 0.5f; dust.size[2] = 0.7f;
		for (auto& c : dust.color) { c[0] = 0.96f; c[1] = 0.93f; c[2] = 0.86f; c[3] = 0.9f; }
		dust.color[2][3] = 0.0f;
		dust.blend = render::ParticleBlend::Alpha;
		s.particles3D(&dust, 1);
		render::HitFeel feel;   // 跳んだ高さより高い所から落ちた時だけ。速いほど強く、0.1 秒で消える
		feel.radialBlur = std::fmax(0.0f, 1.0f - age / 0.1f) * std::clamp((landSpeed - kJumpSpeed - 0.5f) / 8.0f, 0.0f, 0.4f);
		s.hitFeel3D(feel);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xEAF1F8));
		s.camera3D(view.eye, view.target, view.up, view.fovDeg, 0.1f, 80.0f);   // 庭は 20 m 四方なので遠い面は近くてよい
		s.light3D({-0.5f, -1.0f, -0.35f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		for (const Block& k : kBlocks) { s.drawMesh("cube", (k.lo + k.hi) * 0.5f, k.hi - k.lo, {0, 0, 0}, hex(k.col)); }
		s.drawMesh("cube", lift.pose.position, lift.halfExtents * 2.0f, {0, 0, 0}, hex(0xE8338A));
		s.pointLight3D(lift.pose.position + act::Vec3{0, 0.6f, 0}, 3.5f, hex(0xFF5FA8), 3.0f);
		const act::Vec3 p = hero.position;
		s.drawMesh("cube", p + act::Vec3{0, 0.8f, 0}, {0.6f, 1.6f, 0.6f}, {0, 0, 0}, hex(0xFF9500));
		s.drawMesh("cube", p + act::Vec3{0, 1.25f, 0} + facing * 0.32f, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, hex(0x1D1D1F));
		drawTrail(s);
		drawLanding(s);
		chapterTitle(s, "3D アクション");
		chapterControls(s, "矢印: あるく　Space: 跳ぶ　Q / E: カメラを回す");
	}
};

// 実行:  mitiru_host.exe action3d/action3d.dll
MITIRU_ASSERT_NO_PADDING(Action3D);
MITIRU_GAME(Action3D);
