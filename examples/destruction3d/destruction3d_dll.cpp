// destruction3d。「物を割る」章 (読み込みの時に割っておいた破片へ差し替える)
// 実行すると: 木箱 3 つと石の柱 2 本が並ぶ。Space で鉄の玉を投げて割り、F で手前の物を叩き割る。破片は 4 秒で消える
// 関連 API: fractureConvex / JoltBreakables (破片は addReserveBody で置いておく) / BreakableState / registerMesh3D /
//           drawMeshPieces (破片をまとめて描く) / particles3D / hud.play / MITIRU_SIDE_STATE (割れ方ごと巻き戻す)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mitiru.hpp>
#include <mitiru/module/SideState.hpp>
#include <mitiru/physics/JoltBreakables.hpp>
#include <mitiru/render/GpuParticles.hpp>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace phys = mitiru::physics3d;

// 割れ方は読み込みの時に 1 回だけ作る (種が同じなら毎回同じ破片)。柱は正八角柱
constexpr float kOct = 0.70710678f * 0.35f;
constexpr sgc::Vec2f kOctagon[8] = {{0.35f, 0}, {kOct, kOct}, {0, 0.35f}, {-kOct, kOct}, {-0.35f, 0}, {-kOct, -kOct}, {0, -0.35f}, {kOct, -kOct}};
const phys::FractureResult kCrate = phys::fractureConvex(phys::ConvexPolyhedron::box({0.4f, 0.4f, 0.4f}),
	{.pieces = 12, .seed = 5, .outer = hex(0xB07A45), .inner = hex(0xE3C38E)});
const phys::FractureResult kPillar = phys::fractureConvex(phys::ConvexPolyhedron::prism(kOctagon, 0.0f, 2.4f),
	{.pieces = 14, .seed = 9, .outer = hex(0x9A9A92), .inner = hex(0xC9C4B6)});

struct Thing { const phys::FractureResult* f; sgc::Vec3f at; };
constexpr int kThings = 5;
const Thing kPlaces[kThings] = {{&kCrate, {-2.2f, 0.4f, 0.3f}}, {&kPillar, {-0.9f, 0.0f, -0.8f}}, {&kCrate, {0.2f, 0.4f, 0.4f}},
	{&kPillar, {1.4f, 0.0f, -0.8f}}, {&kCrate, {2.5f, 0.4f, 0.2f}}};
constexpr int kMaxPieces = 14;   // 木箱 12、柱 14
constexpr int kBalls = 4;
constexpr float kLifetime = 4.0f;

phys::JoltBreakables g_breakables;
JPH::BodyID g_balls[kBalls];

void buildWorld(phys::JoltGameWorld& world, const void* /*memory*/)
{
	g_breakables.beginBuild(world);
	world.addBody(JPH::BodyCreationSettings(new JPH::BoxShape(JPH::Vec3(10.0f, 0.5f, 10.0f)), JPH::RVec3(0, -0.5f, 0),
		JPH::Quat::sIdentity(), JPH::EMotionType::Static, phys::JoltGameWorld::kStaticLayer), JPH::EActivation::DontActivate);
	for (const Thing& t : kPlaces)   // 木箱は動き、柱は割れるまで動かない
	{
		const bool crate = t.f == &kCrate;
		g_breakables.add(world, {.fracture = t.f, .position = t.at, .dynamic = crate, .density = crate ? 300.0f : 2400.0f,
			.breakImpulse = crate ? 60.0f : 200.0f, .debrisLifetime = kLifetime, .scatterSpeed = crate ? 1.5f : 0.8f});
	}
	for (JPH::BodyID& b : g_balls)   // 投げる鉄の玉 (40 kg) も world の外に置いておく
	{
		JPH::BodyCreationSettings s(new JPH::SphereShape(0.22f), JPH::RVec3(0, -10, 0), JPH::Quat::sIdentity(),
			JPH::EMotionType::Dynamic, phys::JoltGameWorld::kMovingLayer);
		s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		s.mMassPropertiesOverride.mMass = 40.0f;
		b = world.addReserveBody(s);
	}
}

phys::JoltGameWorld g_world(&buildWorld);
MITIRU_SIDE_STATE("jolt", 1, g_world);

struct Destruction3D
{
	phys::BreakableState things[kThings]{};   // 割れたか・いつ・どこで、描く姿勢
	sgc::Vec3f balls[kBalls]{};               // 玉の位置 (y < -5 は投げていない)
	int thrown = 0;                           // 投げた数 (次に狙う物も決める)

	void init() { g_world.reset(); *this = Destruction3D{}; std::fill(std::begin(balls), std::end(balls), sgc::Vec3f{0, -10, 0}); }

	void update(Input in, Hud hud, float dt)
	{
		g_world.ensureBuilt(this);
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		if (in.pressed(Key::Space))   // 手前から次の物を狙って投げる。着くまでに落ちる分だけ上へ投げる
		{
			const JPH::BodyID id = g_balls[thrown % kBalls];
			const sgc::Vec3f from{0.0f, 2.0f, 6.0f}, d = kPlaces[thrown++ % kThings].at + sgc::Vec3f{0, 0.5f, 0} - from;
			const float t = d.length() / 18.0f;
			const sgc::Vec3f v = d / t + sgc::Vec3f{0.0f, 4.905f * t, 0.0f};
			g_world.despawn(id);
			g_world.spawn(id, JPH::RVec3(from.x, from.y, from.z), JPH::Quat::sIdentity(), JPH::Vec3(v.x, v.y, v.z));
		}
		if (in.pressed(Key::F)) { strikeNearest(hud); }
		g_world.step(dt);
		phys::BreakEvent events[kThings];   // 強くぶつかって割れた物
		const int n = g_breakables.afterStep(g_world, things, dt, events);
		for (int i = 0; i < n; ++i) { hud.play("crash", events[i].impulse > 150.0f ? 1.0f : 0.7f); }
		for (int i = 0; i < kBalls; ++i)
		{
			const JPH::RVec3 p = g_world.bodies().GetPosition(g_balls[i]);
			balls[i] = g_world.isSpawned(g_balls[i]) ? sgc::Vec3f{p.GetX(), p.GetY(), p.GetZ()} : sgc::Vec3f{0, -10, 0};
		}
	}

	// 攻撃が当たった時と同じに、手前 (z が大きい) の割れていない物を奥へ叩き割る
	void strikeNearest(Hud hud)
	{
		int best = -1;
		for (int i = 0; i < kThings; ++i)
		{
			if (things[i].phase == phys::BreakPhase::Intact && (best < 0 || kPlaces[i].at.z > kPlaces[best].at.z)) { best = i; }
		}
		if (best < 0) { return; }
		g_breakables.shatter(g_world, best, things[best], things[best].intact.position + sgc::Vec3f{0, 0.3f, 0.4f}, {0, 40, -250});
		hud.play("crash");
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({0.0f, 3.2f, 8.5f}, {0.0f, 0.9f, 0.0f}, 50.0f);
		s.light3D({-0.5f, -0.9f, -0.4f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f}, {0, 0, 0}, hex(0xBFC8D4));
		render::ParticleEmitterDesc dust[kThings];
		render::PieceInstancePod pieces[kThings * (kMaxPieces + 1)];
		int dustCount = 0, pieceCount = 0;
		for (int i = 0; i < kThings; ++i)
		{
			addPieces(s, i, things[i], pieces, pieceCount);
			if (things[i].phase != phys::BreakPhase::Intact && things[i].sinceBreak < 2.0f) { dust[dustCount++] = dustOf(i); }
		}
		s.drawMeshPieces(pieces, pieceCount);
		s.particles3D(dust, dustCount);
		for (const sgc::Vec3f& b : balls)
		{
			if (b.y > -5.0f) { s.drawMesh("sphere", b, {0.44f, 0.44f, 0.44f}, {0, 0, 0}, hex(0x3A3F4A)); }
		}
		chapterTitle(s, "物を割る");
		chapterControls(s, "Space: 玉を投げる　F: 手前を叩き割る　R: さいしょから");
	}

	// 割る前は立体を 1 つ、割れた後は破片を積む。全部の物の破片を drawMeshPieces にまとめて渡すと、同じ形の破片 (3 つの木箱の
	// k 番目どうし) が 1 回の instanced draw になる。motionKey は物と破片ごとに決め、消えた破片の後ろが隣と取り違えられない
	static void addPieces(Screen& s, int i, const phys::BreakableState& st, render::PieceInstancePod* out, int& count)
	{
		const phys::FractureResult& f = *kPlaces[i].f;
		const auto put = [&](int k, const phys::BreakablePose& pose, float scale) {
			const sgc::Mat4f m = sgc::Mat4f::translation(pose.position) * render::quatToMat4(pose.rotation) * sgc::Mat4f::scaling(scale);
			render::PieceInstancePod& p = out[count++];
			p.meshId = meshOf(s, f, k);
			p.motionKey = static_cast<std::uint32_t>(i * 32 + k + 2);
			std::memcpy(p.world, m.m, sizeof(p.world));   // 色は頂点に焼いてある (外は木や石、切り口は明るく)
		};
		if (st.phase == phys::BreakPhase::Intact) { put(-1, st.intact, 1.0f); }
		if (st.phase != phys::BreakPhase::Broken) { return; }
		const float shrink = std::min(1.0f, (kLifetime - st.sinceBreak) / 0.6f);   // 消える前の 0.6 秒で小さくする
		for (int k = 0; k < st.pieceCount; ++k) { put(k, st.pieces[k], shrink); }
	}

	// 破片のメッシュは形が変わらないので、host に無い時だけ登録し、返った番号を覚えておく (k = -1 は割る前の立体)
	static std::uint32_t meshOf(Screen& s, const phys::FractureResult& f, int k)
	{
		static std::uint32_t ids[2][kMaxPieces + 1] = {};
		static char names[2][kMaxPieces + 1][16] = {};
		const int which = &f == &kCrate ? 0 : 1;
		std::uint32_t& id = ids[which][k + 1];
		char (&name)[16] = names[which][k + 1];
		if (name[0] == '\0') { std::snprintf(name, sizeof(name), "%s.%d", which == 0 ? "crate" : "pillar", k); }
		if (id != 0 && s.hasMesh3D(name)) { return id; }
		const phys::FracturePiece& p = k < 0 ? f.whole : f.pieces[static_cast<std::size_t>(k)];
		id = s.registerMesh3D(name, p.vertices.data(), static_cast<int>(p.vertices.size()), p.indices.data(), static_cast<int>(p.indices.size()));
		return id;
	}

	// 割れた所から出る土ぼこり。割れてからの秒を渡すので、巻き戻すとその時刻の粒が出る
	render::ParticleEmitterDesc dustOf(int i) const
	{
		const sgc::Vec3f p = things[i].hitPoint;
		return {.key = static_cast<std::uint32_t>(i + 1), .seed = static_cast<std::uint32_t>(7 * i + 1), .age = things[i].sinceBreak,
			.duration = 0.05f, .position = {p.x, p.y, p.z}, .spawnRadius = 0.3f, .spreadDeg = 75.0f, .speedMin = 1.0f, .speedMax = 3.5f,
			.lifeMin = 0.6f, .lifeMax = 1.4f, .burst = 64, .gravity = {0.0f, -4.0f, 0.0f}, .drag = 1.5f, .size = {0.10f, 0.22f, 0.30f},
			.color = {{0.85f, 0.78f, 0.66f, 0.9f}, {0.80f, 0.74f, 0.64f, 0.6f}, {0.78f, 0.74f, 0.68f, 0.0f}},
			.lit = 0.6f, .blend = render::ParticleBlend::Alpha};
	}
};

MITIRU_ASSERT_NO_PADDING(Destruction3D);
MITIRU_REFLECT(Destruction3D, thrown);
MITIRU_GAME(Destruction3D);
