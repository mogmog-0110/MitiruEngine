// cloth3d。「布」章 (マントと旗を物理の布で揺らす)
// 実行すると: マントの人形が輪を描いて歩き、裾が遅れてはためく。奥の旗は風で波打つ。Space で風を強める / 弱める
// 関連 API: JoltCloth (留めた頂点を骨に付ける) / KinematicCapsule (布が体に当たる) / buildClothVertices + registerMesh3D / updateMesh3D
//           (毎フレームの形) / MITIRU_SIDE_STATE (布の頂点ごと巻き戻す)

#include <array>
#include <cmath>
#include <vector>
#include <mitiru.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include <mitiru/module/SideState.hpp>
#include <mitiru/physics/ClothMesh.hpp>
#include <mitiru/physics/JoltCloth.hpp>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace anim = mitiru::animation;
namespace phys = mitiru::physics3d;

constexpr const char* kPath = "cloth3d/assets/mannequin/mannequin.glb";
const anim::AnimAsset kMan = anim::loadAnimAssetFile(kPath).value_or(anim::AnimAsset{});
const int kChest = kMan.findNode("chest");
// 布が当たる体のカプセル (骨から骨まで)。胴・太もも・すね
constexpr int kBody = 5;
const int kBones[kBody][2] = {{kMan.findNode("hips"), kMan.findNode("neck")}, {kMan.findNode("thigh.L"), kMan.findNode("shin.L")},
	{kMan.findNode("thigh.R"), kMan.findNode("shin.R")}, {kMan.findNode("shin.L"), kMan.findNode("foot.L")},
	{kMan.findNode("shin.R"), kMan.findNode("foot.R")}};
constexpr float kBoneRadius[kBody] = {0.16f, 0.08f, 0.08f, 0.06f, 0.06f};

// マントは胸の骨の後ろ (-z) に上の行で、旗は竿の上に左の列で留める
const phys::ClothSheetDesc kCape{.columns = 9, .rows = 13, .width = 0.5f, .height = 0.95f, .bendCompliance = 2e-2f};
const phys::ClothSheetDesc kFlag{.columns = 13, .rows = 9, .width = 1.2f, .height = 0.75f, .pin = phys::ClothPin::LeftColumn};
constexpr int kVerts = 9 * 13;   // どちらの布も 117 頂点
const sgc::Mat4f kCapeOnChest = sgc::Mat4f::translation({0.0f, 0.14f, -0.2f});
const sgc::Vec3f kPole{0.4f, 0.0f, -3.0f};

// 半径 1.6 m の輪を 1 m/s で回る。向きは毎フレーム一定の角だけ回す (三角関数を使わず、どの PC でも同じビットになる)
constexpr float kTurn = 1.0f / 60.0f / 1.6f;
constexpr float kTurnCos = 1.0f - kTurn * kTurn / 2.0f, kTurnSin = kTurn - kTurn * kTurn * kTurn / 6.0f;

phys::JoltCloth g_cape, g_flag;
phys::KinematicCapsule g_body[kBody];
anim::AnimPose g_pose;   // 姿勢の計算の置き場 (骨の数だけ使い回す)
void buildWorld(phys::JoltGameWorld& world, const void* memory);
phys::JoltGameWorld g_world(&buildWorld);
MITIRU_SIDE_STATE("jolt", 1, g_world);

struct Cloth3D
{
	float t = 0.0f;
	float hx = 1.0f, hz = 0.0f;     // 歩く向き
	float windScale = 1.0f;
	sgc::Vec3f cape[kVerts]{};      // 布の頂点 (描くときに読む)
	sgc::Vec3f flag[kVerts]{};

	void init() { g_world.reset(); *this = Cloth3D{}; }

	// 人形を置く行列: モデルの +z を歩く向きへ回し、輪の上に置く
	sgc::Mat4f place() const
	{
		sgc::Mat4f m = sgc::Mat4f::identity();
		m.m[0][0] = hz;  m.m[0][2] = hx;  m.m[2][0] = -hx;  m.m[2][2] = hz;
		m.m[0][3] = -1.2f + 1.6f * hz;  m.m[2][3] = -1.6f * hx;
		return m;
	}

	anim::AnimPoseParams walk() const
	{
		anim::AnimPoseParams p;
		p.flags = anim::kAnimPoseInPlace;
		anim::pushLayer(p, anim::AnimLayer::clipAt(kMan.findClip("walk"), t));
		return p;
	}

	// 胸の骨のワールド行列。ends に体のカプセルの両端を書く
	sgc::Mat4f chest(sgc::Vec3f* ends) const
	{
		const sgc::Mat4f w = place();
		anim::evaluatePose(kMan, walk(), g_pose);
		for (int i = 0; i < 2 * kBody; ++i) { ends[i] = w.transformPoint(anim::modelPosition(g_pose, kBones[i / 2][i % 2])); }
		return w * g_pose.model[static_cast<std::size_t>(kChest)];
	}

	// 風: 向きは一定で、強さは 2.6 秒の三角波で揺れる
	sgc::Vec3f wind() const
	{
		const float phase = t / 2.6f - static_cast<float>(static_cast<int>(t / 2.6f));
		return sgc::Vec3f{3.0f, 0.0f, 1.2f} * ((0.6f + 0.8f * (phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f)) * windScale);
	}

	void update(Input in, Hud hud, float dt)
	{
		g_world.ensureBuilt(this);
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		if (in.pressed(Key::Space)) { windScale = windScale > 1.5f ? 0.3f : windScale + 0.9f; }
		t += dt;
		const float x = hx * kTurnCos - hz * kTurnSin, z = hx * kTurnSin + hz * kTurnCos;
		hx = x / std::sqrt(x * x + z * z);
		hz = z / std::sqrt(x * x + z * z);
		sgc::Vec3f ends[2 * kBody];
		g_cape.follow(g_world, chest(ends));   // 留めた頂点を胸の骨へ運ぶ
		for (int i = 0; i < kBody; ++i) { g_body[i].follow(g_world, ends[2 * i], ends[2 * i + 1], dt); }
		g_cape.wind(g_world, wind(), 1.0f, dt);
		g_flag.wind(g_world, wind(), 4.0f, dt);
		g_world.step(dt);
		g_cape.readPositions(g_world, cape);
		g_flag.readPositions(g_world, flag);
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({0.6f, 2.3f, 5.0f}, {-0.4f, 1.0f, -0.8f}, 55.0f);
		s.light3D({-0.5f, -0.9f, -0.4f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f}, {0, 0, 0}, hex(0xBFC8D4));
		s.drawMesh("cube", kPole + sgc::Vec3f{0.0f, 1.6f, 0.0f}, {0.08f, 3.2f, 0.08f}, {0, 0, 0}, hex(0x6B5A48));
		const auto pose = walk();
		s.drawModelPose(kPath, place(), &pose);
		drawSheet(s, "cape", kCape, cape, hex(0xB0303A));
		drawSheet(s, "flag", kFlag, flag, hex(0x2F6FD0));
		chapterTitle(s, "布");
		chapterControls(s, windScale > 1.5f ? "Space: 風を弱める　R: さいしょから" : "Space: 風を強める　R: さいしょから");
	}

	// 布の形は毎フレーム変わるので、最初に登録した後は updateMesh3D で頂点だけ送る。前のフレームの形からの動きが TAA と
	// 動きのぼけに渡り、揺れる裾に残像が残らない (GPU のバッファは作り直さない)
	static void drawSheet(Screen& s, const char* name, const phys::ClothSheetDesc& d, const sgc::Vec3f* pos, const Color& color)
	{
		static std::array<render::Vertex3D, 2 * kVerts> verts;
		static const auto kIdx = [](const phys::ClothSheetDesc& c) {
			std::vector<std::uint32_t> idx(static_cast<std::size_t>(phys::clothMeshIndexCount(c.columns, c.rows)));
			phys::buildClothIndices(c.columns, c.rows, idx);
			return idx;
		};
		static const std::vector<std::uint32_t> kCapeIdx = kIdx(kCape), kFlagIdx = kIdx(kFlag);
		const auto& idx = d.columns == kCape.columns ? kCapeIdx : kFlagIdx;
		phys::buildClothVertices(d.columns, d.rows, {pos, static_cast<std::size_t>(kVerts)}, Color{1, 1, 1, 1}, verts);
		if (s.hasMesh3D(name)) { s.updateMesh3D(name, verts.data(), 2 * kVerts); }
		else { s.registerMesh3D(name, verts.data(), 2 * kVerts, idx.data(), static_cast<int>(idx.size())); }
		s.drawMesh(name, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, color);
	}
};

void buildWorld(phys::JoltGameWorld& world, const void* memory)
{
	world.addBody(JPH::BodyCreationSettings(new JPH::BoxShape(JPH::Vec3(10.0f, 0.5f, 10.0f)), JPH::RVec3(0, -0.5f, 0),
		JPH::Quat::sIdentity(), JPH::EMotionType::Static, phys::JoltGameWorld::kStaticLayer), JPH::EActivation::DontActivate);
	world.addBody(JPH::BodyCreationSettings(new JPH::BoxShape(JPH::Vec3(0.04f, 1.6f, 0.04f)), JPH::RVec3(kPole.x, 1.6f, kPole.z),
		JPH::Quat::sIdentity(), JPH::EMotionType::Static, phys::JoltGameWorld::kStaticLayer), JPH::EActivation::DontActivate);
	sgc::Vec3f ends[2 * kBody];
	const sgc::Mat4f chest = static_cast<const Cloth3D*>(memory)->chest(ends);
	for (int i = 0; i < kBody; ++i) { g_body[i].create(world, ends[2 * i], ends[2 * i + 1], kBoneRadius[i]); }
	g_cape.create(world, kCape, kCapeOnChest, chest);
	g_flag.create(world, kFlag, sgc::Mat4f::identity(), sgc::Mat4f::translation(kPole + sgc::Vec3f{0.6f, 3.1f, 0.0f}));
}

MITIRU_ASSERT_NO_PADDING(Cloth3D);
MITIRU_REFLECT(Cloth3D, t, windScale);
MITIRU_GAME(Cloth3D);
