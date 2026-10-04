// footing3d。「足を段と坂に置く」章 (足の接地と滑り止め)
// 実行すると: 人形が坂を上り、段を下りる道を往復する。両足はそれぞれ坂と段の面に乗り、着いている足は地面の上で滑らない。
//             道の端ではその場で足踏みしながら向きを変える。F で足の接地を切って比べる
// 関連 API: footLeg / stepFootPlacement (update) / footPlacementPose (draw) / FootPlacementState (GameMemory に置く)。
//           段と坂の当たり判定は CollisionLevelBuilder で組み、人形は drawModelPose で描く

#include <algorithm>
#include <cmath>
#include <mitiru.hpp>
#include <mitiru/action/FootPlacement.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace act = mitiru::action;
namespace anim = mitiru::animation;

// 人形は ragdoll3d の物 (tools/gen_mannequin.py)。walk は着いた足が 1 m/s で後ろへ動くその場の歩き、step は足踏み
constexpr const char* kPath = "footing3d/assets/mannequin/mannequin.glb";
const anim::AnimAsset kRig = anim::loadAnimAssetFile(kPath).value_or(anim::AnimAsset{});
const int kWalk = kRig.findClip("walk"), kStep = kRig.findClip("step");
// footLeg はレスト姿勢の足首の高さを測る。クリップの足首がこの高さまで下りている間、足を着いているとみなして留める
const act::FootLeg kLegs[2] = {
	act::footLeg(kRig, kRig.findNode("thigh.L"), kRig.findNode("shin.L"), kRig.findNode("foot.L")),
	act::footLeg(kRig, kRig.findNode("thigh.R"), kRig.findNode("shin.R"), kRig.findNode("foot.R"))};
constexpr act::FootPlacementConfig kFeet{};
constexpr float kWalkSpeed = 1.0f, kTurnSpeed = 2.0f, kStart = -5.5f, kEnd = 4.0f, kPi = 3.14159265f;

// 道: 平らな床、x = -4 から -1 で 0.6 m 上る坂、上の台、0.15 m ずつ 4 段下りる段
struct Block { act::Vec3 lo, hi; };
constexpr Block kBlocks[] = {{{-1.0f, 0.0f, -1.0f}, {0.5f, 0.6f, 1.0f}},  {{0.5f, 0.0f, -1.0f}, {1.0f, 0.45f, 1.0f}},
                             {{1.0f, 0.0f, -1.0f}, {1.5f, 0.3f, 1.0f}},   {{1.5f, 0.0f, -1.0f}, {2.0f, 0.15f, 1.0f}}};
const act::CollisionLevel kLevel = [] {
	act::CollisionLevelBuilder b;
	b.addBox({-20.0f, -1.0f, -20.0f}, {20.0f, 0.0f, 20.0f}, 0);
	for (const Block& k : kBlocks) { b.addBox(k.lo, k.hi, 0); }
	const act::Vec3 ramp[] = {{-4.0f, 0.0f, -1.0f}, {-1.0f, 0.6f, -1.0f}, {-1.0f, 0.6f, 1.0f}, {-4.0f, 0.0f, 1.0f}};
	const std::uint32_t tris[] = {0, 2, 1, 0, 3, 2};
	b.addTriangles(ramp, tris, 0);
	return b.build();
}();
anim::AnimPose g_pose;   // 姿勢の計算の置き場

struct Footing3D
{
	float x = kStart;              // 足元の位置 (x の上を往復する)
	float baseY = 0.0f;            // 足元の地面の高さ。段の高さへ少しずつ寄せる
	float hx = 1.0f, hz = 0.0f;    // 向き (単位ベクトル)
	float t = 0.0f;                // クリップの時刻
	float turnLeft = 0.0f;         // 残りの旋回 (ラジアン)。0 なら歩く
	float stepMix = 0.0f;          // 足踏みの重み (歩き 0、足踏み 1)
	float camX = kStart;
	std::int32_t feetOff = 0;
	act::FootPlacementState feet{};   // 足の下の地面、腰を下げる量、留めた足首の点

	// 人形を置く行列: モデルの +z を向きへ回し、足元に置く
	sgc::Mat4f place() const
	{
		sgc::Mat4f m = sgc::Mat4f::identity();
		m.m[0][0] = hz;  m.m[0][2] = hx;  m.m[2][0] = -hx;  m.m[2][2] = hz;
		m.m[0][3] = x;  m.m[1][3] = baseY;
		return m;
	}

	anim::AnimPoseParams gait() const
	{
		anim::AnimPoseParams p;
		anim::pushLayer(p, anim::AnimLayer::clipAt(kWalk, t));
		if (stepMix > 0.0f) { anim::pushLayer(p, anim::AnimLayer::clipAt(kStep, t, stepMix)); }
		return p;
	}

	void move(float dt)
	{
		if (turnLeft > 0.0f)   // 向きを a だけ回す。cos と sin は多項式で出す (CRT の三角関数は CPU で結果が変わる)
		{
			const float a = std::min(turnLeft, kTurnSpeed * dt);
			const float c = 1.0f - a * a / 2.0f, s = a - a * a * a / 6.0f;
			const float nx = hx * c - hz * s, nz = hx * s + hz * c, inv = 1.0f / std::sqrt(nx * nx + nz * nz);
			hx = nx * inv;
			hz = nz * inv;
			turnLeft -= a;
			return;
		}
		x += (hx > 0.0f ? kWalkSpeed : -kWalkSpeed) * dt;
		if ((hx > 0.0f && x > kEnd) || (hx < 0.0f && x < kStart)) { turnLeft = kPi; }
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		if (in.pressed(Key::F)) { feetOff = 1 - feetOff; feet = {}; }
		move(dt);
		const float want = turnLeft > 0.0f ? 1.0f : 0.0f;
		stepMix += std::clamp(want - stepMix, -4.0f * dt, 4.0f * dt);
		t += dt;
		const act::CollisionWorld world(&kLevel);
		const act::RayHit ground = world.raycast({x, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 6.0f);
		const float groundY = ground.hit ? ground.point.y : 0.0f;
		baseY += std::clamp(groundY - baseY, -1.2f * dt, 1.2f * dt);
		if (feetOff == 0)   // IK を掛ける前の姿勢で、両足の下の地面を測り、着いた足を留める
		{
			anim::evaluatePose(kRig, gait(), g_pose);
			act::stepFootPlacement(feet, world, g_pose, place(), kLegs, kFeet, dt);
		}
		camX += (x - camX) * std::min(1.0f, 3.0f * dt);
	}

	void drawCourse(Screen& s) const
	{
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f}, {0, 0, 0}, hex(0xBFC8D4));
		for (const Block& k : kBlocks)
		{
			s.drawMesh("cube", (k.lo + k.hi) * 0.5f, k.hi - k.lo, {0, 0, 0}, hex(0xD8C7A8));
		}
		// 坂は傾けた薄い箱。上の面が当たり判定の三角形 (長さ 3.06 m、11.3 度) に重なる
		s.drawMesh("cube", {-2.5f + 0.0098f, 0.3f - 0.049f, 0.0f}, {3.06f, 0.1f, 2.0f}, {0.0f, 0.0f, 11.31f}, hex(0xD8C7A8));
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({camX, 1.3f, 4.4f}, {camX, 0.6f, 0.0f}, Deg{45.0f});
		s.light3D({-0.4f, -0.9f, -0.5f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		drawCourse(s);
		const anim::AnimPoseParams pose = gait();
		if (feetOff != 0)
		{
			s.drawModelPose(kPath, place(), &pose);
		}
		else   // update と同じ状態から、体を下げた配置と足の IK の依頼を作って渡す
		{
			const act::FootPlacementPose fp = act::footPlacementPose(feet, place(), kLegs, kFeet);
			s.drawModelPose(kPath, fp.world, &pose, fp.ik, act::FootPlacementPose::kIkCount);
		}
		chapterTitle(s, "足を段と坂に置く");
		chapterControls(s, feetOff != 0 ? "F: 足の接地を戻す　R: さいしょから" : "F: 足の接地を切る　R: さいしょから");
	}
};

MITIRU_ASSERT_NO_PADDING(Footing3D);
MITIRU_REFLECT(Footing3D, x, baseY, t, turnLeft, stepMix, feetOff);
MITIRU_GAME(Footing3D);
