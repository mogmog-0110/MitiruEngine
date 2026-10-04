// physics_rewind。「物理エンジンの世界ごと巻き戻す」章
// 実行すると: 積んだ箱の上に人形が落ちて崩れる。Space で人形を蹴る (--inspect rewind で崩れる前へ戻せる)
// 関連 API: JoltGameWorld / MITIRU_SIDE_STATE / makeHumanoidRagdollSettings (状態の外に持つ物理の世界)

#include <mitiru.hpp>
#include <mitiru/module/SideState.hpp>
#include <mitiru/physics/JoltGameWorld.hpp>
#include <mitiru/physics/JoltHumanoidRagdoll.hpp>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include "../common/chapter_hud.hpp"  // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;
using physics3d::JoltGameWorld;

constexpr int kBoxes = 45;                               // 箱の数 (5 段のピラミッドを 3 列)
constexpr int kLimbs = physics3d::kHumanoidPartCount;    // 人形の部位の数

JPH::BodyID   g_boxes[kBoxes];
JPH::Ragdoll* g_doll = nullptr;

// 物理の世界を組み立てる。何度呼んでも同じ物を同じ順で置く (巻き戻しやホットリロードで作り直すため)。
void buildWorld(JoltGameWorld& world, const void* /*state*/)
{
	world.addBody(JPH::BodyCreationSettings(new JPH::BoxShape(JPH::Vec3(8.0f, 0.5f, 8.0f)), JPH::RVec3(0, -0.5f, 0),
		JPH::Quat::sIdentity(), JPH::EMotionType::Static, JoltGameWorld::kStaticLayer), JPH::EActivation::DontActivate);
	int n = 0;
	for (int row = 0; row < 3; ++row)
		for (int level = 0; level < 5; ++level)
			for (int i = 0; i < 5 - level; ++i, ++n)
			{
				const JPH::RVec3 pos(-2.0f + i + 0.5f * level, 0.4f + 0.8f * level, -1.2f + 1.2f * row);
				JPH::BodyCreationSettings box(new JPH::BoxShape(JPH::Vec3::sReplicate(0.4f)), pos, JPH::Quat::sIdentity(),
					JPH::EMotionType::Dynamic, JoltGameWorld::kMovingLayer);
				box.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				box.mMassPropertiesOverride.mMass = 2.0f;   // 人形がぶつかると崩れるくらい軽い箱
				g_boxes[n] = world.addBody(box);
			}
	g_doll = world.addRagdoll(*physics3d::makeHumanoidRagdollSettings(JPH::RVec3(0.3f, 7.0f, 0.0f), JoltGameWorld::kMovingLayer), 1);
}

// 物理の世界はゲームの状態 (下の struct) の外にある。保存と復元の窓口として預けると、
// 巻き戻し・セーブ・ホットリロードのたびに、状態と一緒に戻してもらえる。
JoltGameWorld g_world(&buildWorld);
MITIRU_SIDE_STATE("jolt", 1, g_world);

// 描くための位置と向き。物理の世界から毎フレーム写す (描くときは物理の世界に触らない)。
struct Pose { sgc::Vec3f pos, rotDeg; };

Pose poseOf(const JPH::BodyID& id)
{
	const JPH::RVec3 p = g_world.bodies().GetCenterOfMassPosition(id);
	return {{float(p.GetX()), float(p.GetY()), float(p.GetZ())}, physics3d::drawMeshRotationDeg(g_world.bodies().GetRotation(id))};
}

struct PhysicsRewind
{
	Pose boxes[kBoxes]{};
	Pose limbs[kLimbs]{};
	int  kicks = 0;   // 蹴った回数

	void init() { g_world.reset(); }   // さいしょから: 物理の世界も作り直す

	void update(Input in, Hud hud, float dt)
	{
		g_world.ensureBuilt(this);
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		if (in.pressed(Key::Space))   // 胸を奥へ蹴り上げる
		{
			g_world.bodies().AddImpulse(g_doll->GetBodyID(1), JPH::Vec3(0.0f, 250.0f, -420.0f));
			++kicks;
		}
		g_world.step(dt);
		for (int i = 0; i < kBoxes; ++i) { boxes[i] = poseOf(g_boxes[i]); }
		for (int i = 0; i < kLimbs; ++i) { limbs[i] = poseOf(g_doll->GetBodyID(i)); }
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({7.0f, 5.5f, 9.0f}, {0.0f, 1.2f, 0.0f}, Deg{50.0f});
		s.light3D({-0.5f, -0.9f, -0.4f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {16.0f, 1.0f, 16.0f}, {0, 0, 0}, hex(0xBFC8D4));
		for (int i = 0; i < kBoxes; ++i)
		{
			const Color c = (i % 3 == 0) ? theme::kOrange : (i % 3 == 1) ? theme::kBlue : theme::kGreen;
			s.drawMesh("cube", boxes[i].pos, {0.8f, 0.8f, 0.8f}, boxes[i].rotDeg, c);
		}
		for (int i = 0; i < kLimbs; ++i)   // 部位はカプセル。球を縦に伸ばして描く
		{
			const auto size = physics3d::humanoidPartSize(i);
			s.drawMesh("sphere", limbs[i].pos, {2 * size.radius, size.length, 2 * size.radius}, limbs[i].rotDeg, theme::kPink);
		}
		chapterTitle(s, "物理の巻き戻し");
		chapterControls(s, "Space: 人形を蹴る　R: さいしょから");
	}
};

MITIRU_REFLECT(PhysicsRewind, kicks);
MITIRU_GAME(PhysicsRewind);
