#pragma once

/// @file JoltPhysicsWorld3D.hpp
/// @brief Jolt Physics を `IPhysicsWorld3D` として使うアダプタ
///
/// 既存の `mitiru::physics::JoltPhysicsWorld`（`init/createBoxShape/createDynamicBody`
/// という別API形）をラップし、内蔵 `PhysicsWorld3D` と同じ I/F で
/// step/raycast/overlap/addBody を呼べるようにする。
///
/// レイヤー/マスクは Jolt 側の broadphase レイヤー分け（static/moving の2値、実際の
/// 衝突解決に使う）とは独立に、アダプタが `BodyId -> layer` を自前で保持し、
/// クエリ時に `layerMatchesQuery()` で post-filter する。Jolt本体のレイヤー機構を
/// 32値へ拡張しない代わりに、既存の衝突解決（static/moving判定）には一切手を触れない。
///
/// `MITIRU_HAS_JOLT` が未定義のビルドではこのクラス自体が存在しない
/// （呼び出し側は `#ifdef MITIRU_HAS_JOLT` で存在チェックすること）。

#include "mitiru/physics/IPhysicsWorld3D.hpp"
#include "mitiru/physics/JoltPhysics.hpp"

#ifdef MITIRU_HAS_JOLT

#include <algorithm>
#include <unordered_map>

#include <Jolt/Physics/Collision/CollideShape.h>

namespace mitiru::physics3d
{

class JoltPhysicsWorld3D : public IPhysicsWorld3D
{
public:
	explicit JoltPhysicsWorld3D(const physics::JoltPhysicsConfig& config = {})
		: m_gravity(config.gravity)
	{
		m_world.init(config);
	}

	// ── IPhysicsWorld3D 実装 ──────────────────────────────────

	void step(float dt) noexcept override { m_world.update(dt); }

	void setGravity(const sgc::Vec3f& gravity) noexcept override
	{
		m_gravity = gravity;
		if (auto* system = m_world.rawPhysicsSystem())
			system->SetGravity(physics::toJolt(gravity));
	}

	[[nodiscard]] const sgc::Vec3f& gravity() const noexcept override { return m_gravity; }

	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist, std::uint32_t mask) const noexcept override
	{
		std::vector<RayHit3D> hits;
		raycastAll(ray, maxDist, mask, hits);
		if (hits.empty()) return std::nullopt;

		return *std::min_element(hits.begin(), hits.end(),
			[](const RayHit3D& a, const RayHit3D& b) noexcept { return a.distance < b.distance; });
	}

	void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept override
	{
		auto* system = m_world.rawPhysicsSystem();
		if (!system) return;

		const JPH::Vec3 dir = physics::toJolt(ray.direction).Normalized();
		const JPH::RRayCast jphRay(JPH::RVec3(physics::toJolt(ray.origin)), dir * maxDist);

		JPH::RayCastSettings settings;
		JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
		system->GetNarrowPhaseQuery().CastRay(jphRay, settings, collector);

		for (const auto& hit : collector.mHits)
		{
			if (!layerMatchesQuery(bodyLayer(hit.mBodyID.GetIndexAndSequenceNumber()), mask)) continue;

			RayHit3D out;
			out.distance = hit.mFraction * maxDist;
			out.point = physics::fromJolt(JPH::Vec3(jphRay.GetPointOnRay(hit.mFraction)));

			JPH::BodyLockRead lock(system->GetBodyLockInterface(), hit.mBodyID);
			if (lock.Succeeded())
				out.normal = physics::fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(
					hit.mSubShapeID2, jphRay.GetPointOnRay(hit.mFraction)));

			outHits.push_back(out);
		}
	}

	void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		auto* system = m_world.rawPhysicsSystem();
		if (!system) return;

		JPH::SphereShape shape(radius);
		collectOverlaps(*system, shape, center, mask, outResults);
	}

	void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		auto* system = m_world.rawPhysicsSystem();
		if (!system) return;

		JPH::BoxShape shape(physics::toJolt(aabb.halfExtents()));
		collectOverlaps(*system, shape, aabb.center(), mask, outResults);
	}

	[[nodiscard]] BodyId addBody(const BodyDesc& desc) noexcept override
	{
		physics::JoltShapeId shapeId = physics::INVALID_JOLT_SHAPE_ID;
		switch (desc.shape)
		{
		case BodyDesc::Shape::Sphere:  shapeId = m_world.createSphereShape(desc.radius); break;
		case BodyDesc::Shape::Box:     shapeId = m_world.createBoxShape(desc.halfExtents); break;
		case BodyDesc::Shape::Capsule: shapeId = m_world.createCapsuleShape(desc.halfHeight, desc.radius); break;
		}
		if (shapeId == physics::INVALID_JOLT_SHAPE_ID) return kInvalidBodyId;

		physics::JoltBodyId bodyId = physics::INVALID_JOLT_BODY_ID;
		switch (desc.type)
		{
		case BodyDesc::Type::Static:
			bodyId = m_world.createStaticBody(shapeId, desc.position, desc.rotation);
			break;
		case BodyDesc::Type::Kinematic:
			bodyId = m_world.createKinematicBody(shapeId, desc.position, desc.rotation);
			break;
		case BodyDesc::Type::Dynamic:
			bodyId = m_world.createDynamicBody(shapeId, desc.position, desc.rotation, desc.mass);
			break;
		}
		if (bodyId == physics::INVALID_JOLT_BODY_ID) return kInvalidBodyId;

		m_bodyLayers[bodyId] = desc.layer;
		return static_cast<BodyId>(bodyId);
	}

	void removeBody(BodyId id) noexcept override
	{
		m_world.removeBody(static_cast<physics::JoltBodyId>(id));
		m_bodyLayers.erase(static_cast<physics::JoltBodyId>(id));
	}

	[[nodiscard]] std::optional<BodyTransform> bodyTransform(BodyId id) const noexcept override
	{
		const auto jid = static_cast<physics::JoltBodyId>(id);
		if (m_bodyLayers.find(jid) == m_bodyLayers.end()) return std::nullopt;
		return BodyTransform{m_world.getPosition(jid), m_world.getRotation(jid)};
	}

private:
	/// @brief `addBody()` で登録した所属レイヤーを返す（未登録なら0）
	[[nodiscard]] std::uint32_t bodyLayer(physics::JoltBodyId id) const noexcept
	{
		const auto it = m_bodyLayers.find(id);
		return (it != m_bodyLayers.end()) ? it->second : 0u;
	}

	/// @brief シェイプ形状（球/箱）を `center` に置いた形でオーバーラップ問い合わせを行う共通実装
	template <typename ShapeT>
	void collectOverlaps(const JPH::PhysicsSystem& system, const ShapeT& shape, const sgc::Vec3f& center,
		std::uint32_t mask, std::vector<OverlapResult3D>& outResults) const noexcept
	{
		const JPH::RMat44 transform = JPH::RMat44::sTranslation(JPH::RVec3(physics::toJolt(center)));
		JPH::CollideShapeSettings settings;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		system.GetNarrowPhaseQuery().CollideShape(
			&shape, JPH::Vec3::sOne(), transform, settings, JPH::RVec3::sZero(), collector);

		for (const auto& hit : collector.mHits)
		{
			const physics::JoltBodyId bodyId = hit.mBodyID2.GetIndexAndSequenceNumber();
			if (!layerMatchesQuery(bodyLayer(bodyId), mask)) continue;
			outResults.push_back(OverlapResult3D{static_cast<BodyId>(bodyId), 0});
		}
	}

	physics::JoltPhysicsWorld m_world;
	sgc::Vec3f m_gravity{};

	/// @brief `addBody()` で作ったボディの所属レイヤー（クエリのpost-filter専用。
	/// Jolt本体の衝突解決レイヤー（static/moving）とは別軸）
	std::unordered_map<physics::JoltBodyId, std::uint32_t> m_bodyLayers;
};

} // namespace mitiru::physics3d

#endif // MITIRU_HAS_JOLT
