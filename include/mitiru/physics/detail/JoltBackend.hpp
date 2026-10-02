#pragma once

/// @file JoltBackend.hpp
/// @brief JoltPhysicsWorld3D の部品 (Jolt の初期化・レイヤー表・フィルタ・接触の記録・形状の生成)。
/// @details MITIRU_HAS_JOLT のときだけ中身がある。利用者は JoltPhysicsWorld3D.hpp だけを include する。

#ifdef MITIRU_HAS_JOLT

#include <array>
#include <cmath>
#include <iterator>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "mitiru/debug/WarnOnce.hpp"
#include "mitiru/physics/IPhysicsWorld3D.hpp"

namespace mitiru::physics3d::detail::jolt
{

[[nodiscard]] inline JPH::Vec3 toJolt(const sgc::Vec3f& v) noexcept { return JPH::Vec3(v.x, v.y, v.z); }

[[nodiscard]] inline JPH::RVec3 toJoltR(const sgc::Vec3f& v) noexcept { return JPH::RVec3(v.x, v.y, v.z); }

[[nodiscard]] inline sgc::Vec3f fromJolt(JPH::Vec3Arg v) noexcept { return {v.GetX(), v.GetY(), v.GetZ()}; }

#ifdef JPH_DOUBLE_PRECISION
[[nodiscard]] inline sgc::Vec3f fromJolt(JPH::RVec3Arg v) noexcept
{
	return {static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
}
#endif

/// @brief 長さ 0 の回転を渡されると Jolt は NaN の姿勢で解き続けるので、単位回転に置き換える。
[[nodiscard]] inline JPH::Quat toJolt(const sgc::Quaternionf& q) noexcept
{
	const JPH::Quat jq(q.x, q.y, q.z, q.w);
	const float len = jq.Length();
	return (len > 1e-6f) ? jq / len : JPH::Quat::sIdentity();
}

[[nodiscard]] inline sgc::Quaternionf fromJolt(JPH::QuatArg q) noexcept
{
	return sgc::Quaternionf{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

/// @brief BodyId 0 は kInvalidBodyId に予約済みなので、Jolt の番号 (0 始まり) を 1 ずらして使う。
[[nodiscard]] inline BodyId toBodyId(const JPH::BodyID& id) noexcept
{
	return id.IsInvalid() ? kInvalidBodyId : static_cast<BodyId>(id.GetIndexAndSequenceNumber() + 1u);
}

[[nodiscard]] inline JPH::BodyID toJoltId(BodyId id) noexcept
{
	return (id == kInvalidBodyId) ? JPH::BodyID() : JPH::BodyID(static_cast<JPH::uint32>(id - 1u));
}

/// @brief 所属レイヤーは CollisionGroup の GroupID に、衝突マスクは SubGroupID に持たせる。
[[nodiscard]] inline std::uint32_t layerOf(const JPH::Body& body) noexcept
{
	return body.GetCollisionGroup().GetGroupID() & 31u;
}

// ── レイヤー表 ───────────────────────────────────────────────────
// 静的同士を broadphase の段階で除外するためだけの 2 層。利用者のレイヤー/マスクは GroupFilter が見る。

namespace Layers
{
inline constexpr JPH::ObjectLayer kNonMoving = 0;
inline constexpr JPH::ObjectLayer kMoving = 1;
inline constexpr JPH::uint kCount = 2;
} // namespace Layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
	JPH::uint GetNumBroadPhaseLayers() const override { return Layers::kCount; }

	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
	{
		return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
	{
		return static_cast<JPH::BroadPhaseLayer::Type>(layer) == Layers::kNonMoving ? "NON_MOVING" : "MOVING";
	}
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
	{
		return layer == Layers::kMoving || static_cast<JPH::BroadPhaseLayer::Type>(bp) == Layers::kMoving;
	}
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
	{
		return a == Layers::kMoving || b == Layers::kMoving;
	}
};

/// @brief 利用者のレイヤー/マスクを、衝突解決にもそのまま適用する (双方向判定)。
class LayerMaskGroupFilter final : public JPH::GroupFilter
{
public:
	bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override
	{
		return layersCollide(a.GetGroupID(), a.GetSubGroupID(), b.GetGroupID(), b.GetSubGroupID());
	}
};

/// @brief 問い合わせの mask を body の所属レイヤーで判定する。
class QueryMaskFilter final : public JPH::BodyFilter
{
public:
	explicit QueryMaskFilter(std::uint32_t mask) noexcept : m_mask(mask) {}

	bool ShouldCollideLocked(const JPH::Body& body) const override
	{
		return layerMatchesQuery(layerOf(body), m_mask);
	}

private:
	std::uint32_t m_mask;
};

// ── 接触の記録 ───────────────────────────────────────────────────

/// @brief 接触を (body1, subShape1, body2, subShape2) をキーに持ち続ける台帳。
/// @details Jolt はコールバックを複数スレッドから順不同で呼ぶ。キー順の std::map に溜めて、
///          step の後にキー順で書き出すので、並びはスレッドの都合に依らない。
///          Jolt は body が眠ると、まだ触れている接触にも Removed を出す。そのまま消すと眠った
///          瞬間に「離れた」ことになるので、眠りで消えた接触は残し、起きた次の step でも
///          報告が無ければ (= 離れている) 消す。
class ContactRecorder final : public JPH::ContactListener
{
public:
	void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m,
		JPH::ContactSettings&) override
	{
		record(b1, b2, m);
	}

	void OnContactPersisted(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m,
		JPH::ContactSettings&) override
	{
		record(b1, b2, m);
	}

	void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_removed.push_back(Key{pair.GetBody1ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID1().GetValue(),
			pair.GetBody2ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID2().GetValue()});
	}

	/// @brief PhysicsSystem::Update の後に 1 回呼ぶ。消えた接触を仕分けてから、台帳をキー順で out へ書き出す
	void finishStep(const JPH::BodyLockInterface& locks, std::vector<BodyContact3D>& out)
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		for (const Key& key : m_removed) settleRemoved(locks, key);
		m_removed.clear();
		for (auto it = m_contacts.begin(); it != m_contacts.end();)
		{
			it = keepSleeping(locks, it->first, it->second) ? std::next(it) : m_contacts.erase(it);
		}

		out.clear();
		for (const auto& entry : m_contacts) out.push_back(entry.second.contact);
		++m_step;
	}

private:
	using Key = std::array<std::uint32_t, 4>;

	struct Entry
	{
		BodyContact3D contact{};
		std::uint64_t seenStep{0};   ///< 最後に Added / Persisted が来た step
		std::uint64_t wokeStep{0};   ///< 眠っていた組が起きた step (0 = まだ起きていない)
		bool asleep{false};
	};

	enum class PairState { Gone, Asleep, Awake };

	/// @brief 1 体ずつ見る (Jolt は 2 体のロックを同時に取ると順序検査で止まる)。眠っているのは動く body だけ
	[[nodiscard]] static PairState classifyBody(const JPH::BodyLockInterface& locks, std::uint32_t id)
	{
		const JPH::BodyLockRead lock(locks, JPH::BodyID(id));
		if (!lock.Succeeded()) return PairState::Gone;
		const JPH::Body& body = lock.GetBody();
		return (!body.IsStatic() && !body.IsActive()) ? PairState::Asleep : PairState::Awake;
	}

	/// @brief 組のどちらかが消えていれば Gone、どちらかが眠っていれば Asleep
	[[nodiscard]] static PairState classify(const JPH::BodyLockInterface& locks, const Key& key)
	{
		const PairState a = classifyBody(locks, key[0]);
		const PairState b = classifyBody(locks, key[2]);
		if (a == PairState::Gone || b == PairState::Gone) return PairState::Gone;
		return (a == PairState::Asleep || b == PairState::Asleep) ? PairState::Asleep : PairState::Awake;
	}

	void settleRemoved(const JPH::BodyLockInterface& locks, const Key& key)
	{
		const auto it = m_contacts.find(key);
		if (it == m_contacts.end() || it->second.seenStep == m_step) return;   // 同じ step に報告し直された
		if (classify(locks, key) == PairState::Asleep) it->second.asleep = true;
		else m_contacts.erase(it);
	}

	/// @brief 眠りで残した組を、消えたら捨て、起きた次の step でも報告が無ければ捨てる
	[[nodiscard]] bool keepSleeping(const JPH::BodyLockInterface& locks, const Key& key, Entry& e) const
	{
		if (!e.asleep) return true;
		switch (classify(locks, key))
		{
		case PairState::Gone:   return false;
		case PairState::Asleep: e.wokeStep = 0; return true;
		case PairState::Awake:
			if (e.wokeStep == 0) { e.wokeStep = m_step; return true; }
			return m_step <= e.wokeStep;
		}
		return false;
	}

	void record(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m)
	{
		Entry e;
		e.contact.bodyA = toBodyId(b1.GetID());
		e.contact.bodyB = toBodyId(b2.GetID());
		e.contact.normal = fromJolt(m.mWorldSpaceNormal);
		e.contact.depth = m.mPenetrationDepth;
		e.contact.isSensor = b1.IsSensor() || b2.IsSensor();
		if (!m.mRelativeContactPointsOn1.empty())
			e.contact.point = fromJolt(m.GetWorldSpaceContactPointOn1(0));

		const Key key{b1.GetID().GetIndexAndSequenceNumber(), m.mSubShapeID1.GetValue(),
			b2.GetID().GetIndexAndSequenceNumber(), m.mSubShapeID2.GetValue()};
		const std::lock_guard<std::mutex> lock(m_mutex);
		e.seenStep = m_step;
		m_contacts[key] = e;
	}

	std::mutex m_mutex;
	std::map<Key, Entry> m_contacts;
	std::vector<Key> m_removed;   ///< この step に Removed が来たキー (step の後で仕分ける)
	std::uint64_t m_step{1};
};

// ── 形状 ─────────────────────────────────────────────────────────

[[nodiscard]] inline JPH::RefConst<JPH::Shape> finishShape(const JPH::ShapeSettings::ShapeResult& result)
{
	if (result.HasError())
	{
		debug::warnOnce("physics.jolt.shape", std::string("Jolt が形状を作れない: ") + result.GetError().c_str());
		return nullptr;
	}
	return result.Get();
}

[[nodiscard]] inline JPH::RefConst<JPH::Shape> makeMeshShape(const BodyDesc& desc)
{
	const bool valid = desc.meshVertices != nullptr && desc.meshIndices != nullptr &&
		desc.meshIndexCount >= 3 && desc.meshIndexCount % 3 == 0;
	if (!valid)
	{
		debug::warnOnce("physics.jolt.mesh", "メッシュの当たり判定に頂点か三角形の添字 (3 の倍数) が無い");
		return nullptr;
	}

	JPH::VertexList vertices;
	vertices.reserve(desc.meshVertexCount);
	for (std::size_t i = 0; i < desc.meshVertexCount; ++i)
	{
		const sgc::Vec3f& v = desc.meshVertices[i];
		vertices.push_back(JPH::Float3(v.x, v.y, v.z));
	}

	JPH::IndexedTriangleList triangles;
	triangles.reserve(desc.meshIndexCount / 3);
	for (std::size_t i = 0; i < desc.meshIndexCount; i += 3)
	{
		const std::uint32_t a = desc.meshIndices[i], b = desc.meshIndices[i + 1], c = desc.meshIndices[i + 2];
		if (a >= desc.meshVertexCount || b >= desc.meshVertexCount || c >= desc.meshVertexCount)
		{
			debug::warnOnce("physics.jolt.mesh.index", "メッシュの添字が頂点数を超えている");
			return nullptr;
		}
		triangles.push_back(JPH::IndexedTriangle(a, b, c));
	}
	return finishShape(JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create());
}

/// @brief Jolt の箱は角を convexRadius で丸めるので、薄い箱では半径を半分の厚み以下へ縮める。
[[nodiscard]] inline JPH::RefConst<JPH::Shape> makeBoxShape(const sgc::Vec3f& halfExtents)
{
	const float thinnest = std::fmin(halfExtents.x, std::fmin(halfExtents.y, halfExtents.z));
	if (!(thinnest > 0.0f))
	{
		debug::warnOnce("physics.jolt.box", "箱の当たり判定の半径サイズに 0 以下の軸がある");
		return nullptr;
	}
	const float radius = std::fmin(JPH::cDefaultConvexRadius, thinnest * 0.5f);
	return finishShape(JPH::BoxShapeSettings(toJolt(halfExtents), radius).Create());
}

[[nodiscard]] inline JPH::RefConst<JPH::Shape> makeShape(const BodyDesc& desc)
{
	switch (desc.shape)
	{
	case BodyDesc::Shape::Sphere:
		return finishShape(JPH::SphereShapeSettings(desc.radius).Create());
	case BodyDesc::Shape::Box:
		return makeBoxShape(desc.halfExtents);
	case BodyDesc::Shape::Capsule:
		// 胴の長さ 0 のカプセルを Jolt は受け付けないが、形としては球と同じ
		if (desc.halfHeight <= 0.0f) return finishShape(JPH::SphereShapeSettings(desc.radius).Create());
		return finishShape(JPH::CapsuleShapeSettings(desc.halfHeight, desc.radius).Create());
	case BodyDesc::Shape::Mesh:
		return makeMeshShape(desc);
	}
	return nullptr;
}

// ── Jolt 全体の登録 ──────────────────────────────────────────────

/// @brief Factory と型登録はプロセスで 1 回だけ要るので、生きているワールドの数で数える。
class Runtime
{
public:
	Runtime()
	{
		const std::lock_guard<std::mutex> lock(mutex());
		if (count()++ == 0)
		{
			JPH::RegisterDefaultAllocator();
			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();
		}
	}

	~Runtime()
	{
		const std::lock_guard<std::mutex> lock(mutex());
		if (--count() == 0)
		{
			JPH::UnregisterTypes();
			delete JPH::Factory::sInstance;
			JPH::Factory::sInstance = nullptr;
		}
	}

	Runtime(const Runtime&) = delete;
	Runtime& operator=(const Runtime&) = delete;

private:
	static std::mutex& mutex() { static std::mutex m; return m; }
	static int& count() { static int n = 0; return n; }
};

} // namespace mitiru::physics3d::detail::jolt

#endif // MITIRU_HAS_JOLT
