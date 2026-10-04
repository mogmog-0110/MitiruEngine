// ragdoll3d。「殴られてよろけ、倒れて起き上がる」章 (アニメと物理の人形を混ぜる)
// 実行すると: 人形が立って息をしている。Space で胸を殴ると上半身だけがよろけて戻る。K で倒れ、2 秒後に起き上がる
// 関連 API: PoweredRagdoll / loadRagdollRig (<モデル>.ragdoll.json) / blendRagdollPose / drawModelNodeMatrices /
//           MITIRU_SIDE_STATE (物理の世界ごと巻き戻す)

#include <vector>
#include <mitiru.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include <mitiru/module/SideState.hpp>
#include <mitiru/physics/JoltPoweredRagdoll.hpp>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include "../common/chapter_hud.hpp"  // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;
namespace anim = mitiru::animation;
namespace phys = mitiru::physics3d;

// 人形の骨格・クリップと、骨に付けるカプセル (隣の mannequin.ragdoll.json) を読み込みの時に組む
constexpr const char* kPath = "ragdoll3d/assets/mannequin/mannequin.glb";
const anim::AnimAsset kRig = anim::loadAnimAssetFile(kPath).value_or(anim::AnimAsset{});
const phys::RagdollRig kDoll = phys::loadRagdollRig(kRig, kPath);
const int kIdle = kRig.findClip("idle");
const int kChest = kDoll.findPart("chest");
constexpr int kParts = phys::kRagdollMaxParts;

anim::AnimPoseParams idleAt(float t)
{
	anim::AnimPoseParams p;
	anim::pushLayer(p, anim::AnimLayer::clipAt(kIdle, t));
	return p;
}

struct Ragdoll3D;
phys::PoweredRagdoll g_doll;
// 姿勢の計算と、混ぜた骨の行列の置き場。読み込みの時に骨の数だけ取っておく
anim::AnimPose g_pose;
std::vector<sgc::Mat4f> g_nodes(kRig.nodes.size());
phys::RagdollBlendScratch g_blend{std::vector<anim::NodeTRS>(kRig.nodes.size()), std::vector<anim::NodeTRS>(kRig.nodes.size())};

// 物理の世界: 床と人形。作り直すたびに、人形を今のアニメの姿勢に置く
void buildWorld(phys::JoltGameWorld& world, const void* memory);
phys::JoltGameWorld g_world(&buildWorld);
MITIRU_SIDE_STATE("jolt", 1, g_world);

struct Ragdoll3D
{
	float animT = 0.0f;               // 待機のアニメの時刻 (秒)
	float x = 0.0f, z = 0.0f;         // 人形の足元 (起き上がると、倒れた所へ移る)
	phys::RagdollState doll{};        // アニメ / よろけ / 倒れ / 起き上がり
	phys::RagdollPartPose parts[kParts]{};   // 物理の部位 (描くときに読む)
	int hits = 0;

	sgc::Mat4f world() const { return sgc::Mat4f::translation({x, 0.0f, z}); }

	// 今のアニメの姿勢から、部位が向かう位置と向きを出す
	void targets(phys::RagdollPartPose* out) const
	{
		anim::evaluatePose(kRig, idleAt(animT), g_pose);
		phys::ragdollTargetsFromPose(kDoll, g_pose.model, world(), {out, static_cast<std::size_t>(kParts)});
	}

	void init() { g_world.reset(); *this = Ragdoll3D{}; }

	void update(Input in, Hud hud, float dt)
	{
		g_world.ensureBuilt(this);
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		if (in.pressed(Key::Space) && doll.mode != phys::RagdollMode::Dead)   // 胸を正面から殴る
		{
			phys::ragdollHit(doll);
			g_world.bodies().AddImpulse(g_doll.body(kChest), JPH::Vec3(0.0f, 0.0f, -90.0f));
			++hits;
		}
		if (in.pressed(Key::K)) { phys::ragdollKill(doll); g_world.bodies().AddImpulse(g_doll.body(kChest), JPH::Vec3(0.0f, 20.0f, -70.0f)); }
		if (doll.mode == phys::RagdollMode::Dead && doll.timer > 2.0f)   // 倒れた腰の真上から起き上がる
		{
			phys::ragdollGetUp(doll);
			x = parts[0].position.x;
			z = parts[0].position.z;
		}
		const bool recovered = phys::ragdollAdvance(doll, dt);
		animT += dt;
		phys::RagdollPartPose goal[kParts];
		targets(goal);
		if (recovered) { g_doll.teleport(g_world, goal); }   // 起き上がり終えたら、部位をアニメの姿勢へ置く
		g_doll.drive(g_world, goal, phys::ragdollDriveFor(doll, kDoll), dt);
		g_world.step(dt);
		g_doll.read(g_world, parts);
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({2.6f, 1.7f, 4.2f}, {x * 0.5f, 0.8f, z * 0.5f}, Deg{50.0f});
		s.light3D({-0.5f, -0.9f, -0.4f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {16.0f, 1.0f, 16.0f}, {0, 0, 0}, hex(0xBFC8D4));

		// アニメの姿勢と物理の部位を、部位ごとの重みで混ぜた骨の行列で描く
		float weights[kParts];
		phys::ragdollWeightsFor(doll, kDoll, weights);
		anim::evaluatePose(kRig, idleAt(animT), g_pose);
		phys::blendRagdollPose(kRig, kDoll, g_pose, parts, weights, world(), g_blend, g_nodes);
		s.drawModelNodeMatrices(kPath, world(), g_nodes.data(), static_cast<std::uint32_t>(g_nodes.size()));

		static constexpr const char* kMode[] = {"アニメ", "よろけ (上半身が物理)", "倒れ (全身が物理)", "起き上がり"};
		const Rect badge{1280.0f - 300.0f, 14.0f, 284.0f, 36.0f};
		s.drawRect(badge, rgba(226, 231, 240, 215));
		s.drawTextInRect(badge, kMode[static_cast<int>(doll.mode)], theme::kInk, 18.0f, Screen::TextAlignH::Center,
		                 Screen::TextAlignV::Middle);
		chapterTitle(s, "ラグドール");
		chapterControls(s, "Space: 殴る　K: 倒す　R: さいしょから");
	}
};

void buildWorld(phys::JoltGameWorld& world, const void* memory)
{
	world.addBody(JPH::BodyCreationSettings(new JPH::BoxShape(JPH::Vec3(8.0f, 0.5f, 8.0f)), JPH::RVec3(0, -0.5f, 0),
		JPH::Quat::sIdentity(), JPH::EMotionType::Static, phys::JoltGameWorld::kStaticLayer), JPH::EActivation::DontActivate);
	g_doll.create(world, kDoll, sgc::Mat4f::identity(), 1);
	phys::RagdollPartPose goal[kParts];
	static_cast<const Ragdoll3D*>(memory)->targets(goal);
	g_doll.teleport(world, goal);
}

MITIRU_ASSERT_NO_PADDING(Ragdoll3D);
MITIRU_REFLECT(Ragdoll3D, animT, x, z, hits);
MITIRU_GAME(Ragdoll3D);
