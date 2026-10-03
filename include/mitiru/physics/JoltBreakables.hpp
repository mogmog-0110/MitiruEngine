#pragma once

/// @file JoltBreakables.hpp
/// @brief 割れる物 (木箱・柱)。割る前の body と、Fracture.hpp で割った破片の body を JoltGameWorld に
///        置いておき、強くぶつかるか攻撃が当たった瞬間に差し替える (ADR 0069、docs/PHYSICS_FX.md)。
/// @details 破片は組み立てで addReserveBody しておき、割れた時に spawn、寿命が来たら despawn する。
///          どの body が world にあるかと body の状態は JoltGameWorld の窓口 (ADR 0054) が保存するので、
///          巻き戻し・リプレイ・ロールバックで割れ方も戻る。割れたか・いつ割れたか・描く姿勢は BreakableState
///          (flat POD) に置き、GameMemory に持たせる。衝撃は接触の知らせで測り、step をまたいで残さない。

#ifdef MITIRU_HAS_JOLT

#include <algorithm>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>

#include <mitiru/physics/Fracture.hpp>
#include <mitiru/physics/JoltGameWorld.hpp>

namespace mitiru::physics3d
{

inline constexpr int kBreakMaxPieces = 16;

enum class BreakPhase : std::uint8_t
{
	Intact = 0,   ///< 割れる前
	Broken = 1,   ///< 破片が飛んでいる
	Gone = 2,     ///< 破片の寿命が過ぎて world から外した
};

/// @brief 描く姿勢。位置は FracturePiece::center の点 (破片の頂点の原点) の位置、回転は xyzw
struct BreakablePose
{
	sgc::Vec3f position{0, 0, 0};
	sgc::Vec4f rotation{0, 0, 0, 1};
};

/// @brief 割れる物 1 つの状態。GameMemory に置く
struct BreakableState
{
	BreakPhase phase = BreakPhase::Intact;
	std::uint8_t pieceCount = 0;
	std::uint16_t reserved = 0;
	float sinceBreak = 0.0f;            ///< 割れてからの秒 (粒や音を割れた時刻から描く)
	sgc::Vec3f hitPoint{0, 0, 0};       ///< 割った当たりの点
	float hitImpulse = 0.0f;            ///< 割った衝撃 (N·s)
	BreakablePose intact;
	BreakablePose pieces[kBreakMaxPieces];
};

static_assert(std::is_trivially_copyable_v<BreakableState>);
static_assert(sizeof(BreakableState) == 24 + 28 * (1 + kBreakMaxPieces));

/// @brief afterStep が返す「今割れた」知らせ。音と粒を出すのに使う
struct BreakEvent
{
	int index = -1;
	sgc::Vec3f point{0, 0, 0};
	float impulse = 0.0f;
};

struct BreakableDesc
{
	const FractureResult* fracture = nullptr;   ///< 割れ方 (呼び手が world より長く持つ)
	sgc::Vec3f position{0, 0, 0};               ///< 割る前の立体の原点 (割る時に使った座標の原点)
	sgc::Vec4f rotation{0, 0, 0, 1};
	bool dynamic = true;            ///< false なら割れるまで動かない (柱)
	float density = 500.0f;         ///< kg/m^3
	float breakImpulse = 30.0f;     ///< この衝撃 (N·s) を超える接触で割れる
	float debrisLifetime = 4.0f;    ///< 割れてからこの秒数で破片を world から外す
	float scatterSpeed = 1.5f;      ///< 割れた瞬間に、当たった点から外へ散らす速さ (m/s)
};

class JoltBreakables final : public JPH::ContactListener
{
public:
	/// @brief 組み立ての最初に呼ぶ。前の組み立ての分を捨て、接触の知らせを受ける
	void beginBuild(JoltGameWorld& world)
	{
		m_entries.clear();
		m_impact.clear();
		world.setContactListener(this);
	}

	/// @brief 割れる物を 1 つ置く (組み立ての中で)。添字を返す。破片は world の外に置いておく
	int add(JoltGameWorld& world, const BreakableDesc& desc)
	{
		if (desc.fracture == nullptr || desc.fracture->pieces.empty()) { return -1; }
		Entry e;
		e.desc = desc;
		const auto index = static_cast<std::uint64_t>(m_entries.size());
		const JPH::RVec3 at = toJolt(desc.position);
		const JPH::Quat rot = toJolt(desc.rotation);
		JPH::BodyCreationSettings whole(hullShape(desc.fracture->whole), at + rot * toJolt(desc.fracture->whole.center), rot,
			desc.dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
			desc.dynamic ? JoltGameWorld::kMovingLayer : JoltGameWorld::kStaticLayer);
		whole.mUserData = kUserTag | index;
		setMass(whole, desc.density * desc.fracture->whole.volume);
		e.intact = world.addBody(whole, desc.dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		const auto count = std::min<std::size_t>(desc.fracture->pieces.size(), kBreakMaxPieces);
		for (std::size_t k = 0; k < count; ++k)
		{
			const FracturePiece& p = desc.fracture->pieces[k];
			JPH::BodyCreationSettings s(hullShape(p), at + rot * toJolt(p.center), rot, JPH::EMotionType::Dynamic,
				JoltGameWorld::kMovingLayer);
			setMass(s, desc.density * p.volume);
			e.pieces.push_back(world.addReserveBody(s));
		}
		m_entries.push_back(std::move(e));
		m_impact.push_back(Impact{});
		return static_cast<int>(index);
	}

	[[nodiscard]] int count() const noexcept { return static_cast<int>(m_entries.size()); }
	[[nodiscard]] const BreakableDesc& desc(int i) const { return m_entries[static_cast<std::size_t>(i)].desc; }
	[[nodiscard]] JPH::BodyID intactBody(int i) const { return m_entries[static_cast<std::size_t>(i)].intact; }
	[[nodiscard]] std::span<const JPH::BodyID> pieceBodies(int i) const { return m_entries[static_cast<std::size_t>(i)].pieces; }

	/// @brief body がどの割れる物 (割れる前) か。違えば -1。攻撃の当たり判定から割るときに引く
	[[nodiscard]] int indexOf(const JPH::BodyID& id) const
	{
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			if (m_entries[i].intact == id) { return static_cast<int>(i); }
		}
		return -1;
	}

	/// @brief 今すぐ割る (攻撃が当たった時など)。impulse は当たった向きと強さ (N·s)
	void shatter(JoltGameWorld& world, int i, BreakableState& state, const sgc::Vec3f& point, const sgc::Vec3f& impulse)
	{
		if (i < 0 || i >= count() || state.phase != BreakPhase::Intact) { return; }
		const Entry& e = m_entries[static_cast<std::size_t>(i)];
		JPH::BodyInterface& bi = world.bodies();
		const JPH::Quat rot = bi.GetRotation(e.intact);
		const JPH::RVec3 origin = bi.GetPosition(e.intact) - rot * toJolt(e.desc.fracture->whole.center);
		const JPH::Vec3 v = bi.GetLinearVelocity(e.intact);
		const JPH::Vec3 w = bi.GetAngularVelocity(e.intact);
		const JPH::RVec3 com = bi.GetCenterOfMassPosition(e.intact);
		const float totalMass = e.desc.density * e.desc.fracture->whole.volume;
		const JPH::Vec3 push = totalMass > 0.0f ? toJolt(impulse) / totalMass : JPH::Vec3::sZero();
		world.despawn(e.intact);
		for (std::size_t k = 0; k < e.pieces.size(); ++k)
		{
			const JPH::RVec3 at = origin + rot * toJolt(e.desc.fracture->pieces[k].center);
			const JPH::Vec3 out = JPH::Vec3(at - toJolt(point));
			const float len = out.Length();
			const JPH::Vec3 scatter = len > 1e-6f ? out * (e.desc.scatterSpeed / len) : JPH::Vec3::sZero();
			world.spawn(e.pieces[k], at, rot, v + w.Cross(JPH::Vec3(at - com)) + push + scatter, w);
		}
		state.phase = BreakPhase::Broken;
		state.pieceCount = static_cast<std::uint8_t>(e.pieces.size());
		state.sinceBreak = 0.0f;
		state.hitPoint = point;
		state.hitImpulse = impulse.length();
		copyPoses(world, static_cast<std::size_t>(i), state);
	}

	/// @brief step の後に呼ぶ。強い接触で割り、破片の時刻を進め、寿命で外し、描く姿勢を states へ写す。
	/// @return events に書いた「今割れた」数 (割れたフレームの sinceBreak は 0)
	int afterStep(JoltGameWorld& world, std::span<BreakableState> states, float dt, std::span<BreakEvent> events)
	{
		int fired = 0;
		const std::size_t n = std::min(states.size(), m_entries.size());
		for (std::size_t i = 0; i < n; ++i)
		{
			BreakableState& s = states[i];
			const Impact hit = m_impact[i];
			m_impact[i] = Impact{};
			if (s.phase == BreakPhase::Broken) { age(world, i, s, dt); }
			if (s.phase == BreakPhase::Intact && hit.impulse > m_entries[i].desc.breakImpulse)
			{
				shatter(world, static_cast<int>(i), s, hit.point, hit.direction * hit.impulse);
				if (fired < static_cast<int>(events.size())) { events[static_cast<std::size_t>(fired++)] = {static_cast<int>(i), hit.point, hit.impulse}; }
			}
			copyPoses(world, i, s);
		}
		return fired;
	}

	// ── 接触の知らせ (JPH::ContactListener) ──────────────────────────────
	void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m, JPH::ContactSettings&) override
	{
		const int i1 = entryOf(b1);
		const int i2 = entryOf(b2);
		if (i1 < 0 && i2 < 0) { return; }
		const float inv1 = inverseMass(b1);
		const float inv2 = inverseMass(b2);
		if (inv1 + inv2 <= 0.0f) { return; }
		const JPH::RVec3 p = m.GetWorldSpaceContactPointOn1(0);
		const JPH::Vec3 v1 = b1.GetPointVelocityCOM(JPH::Vec3(p - b1.GetCenterOfMassPosition()));
		const JPH::Vec3 v2 = b2.GetPointVelocityCOM(JPH::Vec3(p - b2.GetCenterOfMassPosition()));
		const float approach = -(v2 - v1).Dot(m.mWorldSpaceNormal);
		if (approach <= 0.0f) { return; }
		const float impulse = approach / (inv1 + inv2);
		// 法線は b2 を押し出す向きなので、b1 は -n、b2 は +n へ押される
		record(i1, impulse, p, -m.mWorldSpaceNormal);
		record(i2, impulse, p, m.mWorldSpaceNormal);
	}

private:
	static constexpr std::uint64_t kUserTag = 0x4252454B00000000ull;   // "BREK"
	static constexpr std::uint64_t kTagMask = 0xFFFFFFFF00000000ull;

	struct Entry
	{
		BreakableDesc desc;
		JPH::BodyID intact;
		std::vector<JPH::BodyID> pieces;
	};
	struct Impact
	{
		float impulse = 0.0f;
		sgc::Vec3f point{0, 0, 0};
		sgc::Vec3f direction{0, 0, 0};
	};

	[[nodiscard]] static JPH::Vec3 toJolt(const sgc::Vec3f& v) noexcept { return {v.x, v.y, v.z}; }
	[[nodiscard]] static JPH::Quat toJolt(const sgc::Vec4f& q) noexcept { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
	[[nodiscard]] static sgc::Vec3f toSgc(JPH::Vec3Arg v) noexcept { return {v.GetX(), v.GetY(), v.GetZ()}; }

	/// @brief 破片の凸包。点は破片の重心が原点なので、body の位置が破片の頂点の原点になる
	[[nodiscard]] static JPH::RefConst<JPH::Shape> hullShape(const FracturePiece& p)
	{
		JPH::Array<JPH::Vec3> pts;
		pts.reserve(p.hull.size());
		for (const auto& h : p.hull) { pts.push_back(toJolt(h)); }
		return JPH::ConvexHullShapeSettings(pts, 0.01f).Create().Get();
	}

	static void setMass(JPH::BodyCreationSettings& s, float mass)
	{
		s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		s.mMassPropertiesOverride.mMass = std::max(mass, 0.01f);
	}

	[[nodiscard]] static float inverseMass(const JPH::Body& b)
	{
		return b.IsDynamic() ? b.GetMotionProperties()->GetInverseMass() : 0.0f;
	}

	[[nodiscard]] static int entryOf(const JPH::Body& b) noexcept
	{
		const std::uint64_t u = b.GetUserData();
		return (u & kTagMask) == kUserTag ? static_cast<int>(u & ~kTagMask) : -1;
	}

	void record(int i, float impulse, JPH::RVec3Arg p, JPH::Vec3Arg dir)
	{
		if (i < 0 || static_cast<std::size_t>(i) >= m_impact.size() || impulse <= m_impact[static_cast<std::size_t>(i)].impulse) { return; }
		m_impact[static_cast<std::size_t>(i)] = {impulse, toSgc(JPH::Vec3(p)), toSgc(dir)};
	}

	void age(JoltGameWorld& world, std::size_t i, BreakableState& s, float dt)
	{
		s.sinceBreak += dt;
		if (s.sinceBreak < m_entries[i].desc.debrisLifetime) { return; }
		for (const JPH::BodyID& id : m_entries[i].pieces) { world.despawn(id); }
		s.phase = BreakPhase::Gone;
	}

	void copyPoses(JoltGameWorld& world, std::size_t i, BreakableState& s) const
	{
		const Entry& e = m_entries[i];
		JPH::BodyInterface& bi = world.bodies();
		const auto poseOf = [&](const JPH::BodyID& id) {
			const JPH::Quat q = bi.GetRotation(id);
			return BreakablePose{toSgc(JPH::Vec3(bi.GetPosition(id))), {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}};
		};
		if (s.phase == BreakPhase::Intact) { s.intact = poseOf(e.intact); }
		if (s.phase != BreakPhase::Broken) { return; }
		for (std::size_t k = 0; k < e.pieces.size(); ++k) { s.pieces[k] = poseOf(e.pieces[k]); }
	}

	std::vector<Entry>  m_entries;
	std::vector<Impact> m_impact;   ///< この step で受けた最大の衝撃 (afterStep で空にする)
};

} // namespace mitiru::physics3d

#endif // MITIRU_HAS_JOLT
