#pragma once

/// @file JoltPhysicsWorld3D.hpp
/// @brief Jolt Physics を `IPhysicsWorld3D` として使う 3D 物理ワールド (エンジン既定のバックエンド)
///
/// 決定論について:
///   - Jolt は CMake の CROSS_PLATFORM_DETERMINISTIC で build する (CMakeLists.txt)。同じ順で
///     API を呼べば、コンパイラ・OS・CPU を跨いでも step の結果が bit 一致する。
///   - Jolt はスレッド数に依らず同じ結果を出す。ただし問い合わせ結果とコールバックの並びは
///     保証しないので、raycastAll / overlap* / contacts() はここで距離 → BodyId の順へ並べ直す。
///   - `step(dt)` は内部に時間の貯金を持たない。同じ dt の列を渡せば同じ状態になる。
///
/// レイヤー/マスクは問い合わせだけでなく衝突解決にも適用される (CollisionGroup + GroupFilter)。
///
/// `MITIRU_HAS_JOLT` が未定義のビルドではこのクラス自体が存在しない
/// （呼び出し側は `#ifdef MITIRU_HAS_JOLT` で存在チェックすること）。

#include "mitiru/physics/IPhysicsWorld3D.hpp"

#ifdef MITIRU_HAS_JOLT

#include <algorithm>
#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include "mitiru/debug/TracyZones.hpp"
#include "mitiru/physics/detail/JoltBackend.hpp"

namespace mitiru::physics3d
{

/// @brief JoltPhysicsWorld3D の構築時設定
struct JoltWorldConfig
{
	std::uint32_t maxBodies{65536};
	std::uint32_t maxBodyPairs{65536};
	std::uint32_t maxContactConstraints{10240};
	sgc::Vec3f gravity{0.0f, -9.81f, 0.0f};
	float maxCollisionStepDt{1.0f / 60.0f};  ///< step(dt) をこれ以下の衝突ステップに刻む
	int workerThreads{0};                    ///< 0 は呼び出しスレッドだけで解く。増やしても結果は変わらない
};

class JoltPhysicsWorld3D final : public IPhysicsWorld3D
{
public:
	explicit JoltPhysicsWorld3D(const JoltWorldConfig& config = {})
		: m_config(config)
		, m_gravity(config.gravity)
		, m_temp(10u * 1024u * 1024u)
		, m_groupFilter(new detail::jolt::LayerMaskGroupFilter())
	{
		if (config.workerThreads > 0)
			m_jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
				config.workerThreads);
		else
			m_jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);

		m_system = std::make_unique<JPH::PhysicsSystem>();
		m_system->Init(config.maxBodies, 0, config.maxBodyPairs, config.maxContactConstraints,
			m_bpLayers, m_objVsBp, m_objPairs);
		m_system->SetGravity(detail::jolt::toJolt(config.gravity));
		m_system->SetContactListener(&m_recorder);
	}

	~JoltPhysicsWorld3D() override = default;

	JoltPhysicsWorld3D(const JoltPhysicsWorld3D&) = delete;
	JoltPhysicsWorld3D& operator=(const JoltPhysicsWorld3D&) = delete;
	JoltPhysicsWorld3D(JoltPhysicsWorld3D&&) = delete;
	JoltPhysicsWorld3D& operator=(JoltPhysicsWorld3D&&) = delete;

	// ── IPhysicsWorld3D 実装 ──────────────────────────────────

	void step(float dt) noexcept override
	{
		MITIRU_ZONE_PHYSICS("JoltPhysicsWorld3D::step");
		if (!(dt > 0.0f)) return;
		const float perStep = std::max(m_config.maxCollisionStepDt, 1e-4f);
		const int collisionSteps = std::max(1, static_cast<int>(std::ceil(dt / perStep - 1e-4f)));
		m_system->Update(dt, collisionSteps, &m_temp, m_jobs.get());
		m_recorder.finishStep(m_system->GetBodyLockInterface(), m_contacts);
	}

	void setGravity(const sgc::Vec3f& gravity) noexcept override
	{
		m_gravity = gravity;
		m_system->SetGravity(detail::jolt::toJolt(gravity));
	}

	[[nodiscard]] const sgc::Vec3f& gravity() const noexcept override { return m_gravity; }

	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist, std::uint32_t mask) const noexcept override
	{
		m_scratchHits.clear();
		raycastAll(ray, maxDist, mask, m_scratchHits);
		if (m_scratchHits.empty()) return std::nullopt;
		return m_scratchHits.front();
	}

	/// @details 近い順。同じ距離なら BodyId の小さい順。
	void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept override
	{
		const std::size_t first = outHits.size();
		collectRayHits(ray, maxDist, mask, outHits);
		std::sort(outHits.begin() + static_cast<std::ptrdiff_t>(first), outHits.end(),
			[](const RayHit3D& a, const RayHit3D& b) noexcept {
				return (a.distance != b.distance) ? a.distance < b.distance : a.bodyId < b.bodyId;
			});
	}

	/// @details BodyId の小さい順、1 ボディ 1 件。
	void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		if (!(radius > 0.0f)) return;
		JPH::SphereShape shape(radius);
		shape.SetEmbedded();  // スタック上の形状なので参照カウントで解放させない
		collectOverlaps(shape, center, mask, outResults);
	}

	/// @details BodyId の小さい順、1 ボディ 1 件。
	void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		const sgc::Vec3f half = aabb.halfExtents();
		const float thinnest = std::fmin(half.x, std::fmin(half.y, half.z));
		if (!(thinnest > 0.0f)) return;
		JPH::BoxShape shape(detail::jolt::toJolt(half), std::fmin(JPH::cDefaultConvexRadius, thinnest * 0.5f));
		shape.SetEmbedded();
		collectOverlaps(shape, aabb.center(), mask, outResults);
	}

	[[nodiscard]] BodyId addBody(const BodyDesc& desc) noexcept override
	{
		if (desc.shape == BodyDesc::Shape::Mesh && desc.type == BodyDesc::Type::Dynamic)
		{
			debug::warnOnce("physics.jolt.mesh.dynamic",
				"メッシュの当たり判定は Static か Kinematic のボディにしか付けられないので、このボディは作りません。"
				"Dynamic のボディには箱か球かカプセルを使ってください。");
			return kInvalidBodyId;
		}
		const JPH::RefConst<JPH::Shape> shape = detail::jolt::makeShape(desc);
		if (shape == nullptr) return kInvalidBodyId;

		const bool moving = desc.type != BodyDesc::Type::Static;
		JPH::BodyCreationSettings settings(shape, detail::jolt::toJoltR(desc.position),
			detail::jolt::toJolt(desc.rotation), motionTypeOf(desc.type),
			moving ? detail::jolt::Layers::kMoving : detail::jolt::Layers::kNonMoving);
		settings.mCollisionGroup = JPH::CollisionGroup(m_groupFilter, desc.layer & 31u, desc.mask);
		if (desc.type == BodyDesc::Type::Dynamic)
		{
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = (desc.mass > 0.0f) ? desc.mass : 1.0f;
		}

		const JPH::BodyID id = m_system->GetBodyInterface().CreateAndAddBody(
			settings, moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		return detail::jolt::toBodyId(id);
	}

	void removeBody(BodyId id) noexcept override
	{
		auto& bi = m_system->GetBodyInterface();
		const JPH::BodyID jid = detail::jolt::toJoltId(id);
		if (jid.IsInvalid() || !bi.IsAdded(jid)) return;
		bi.RemoveBody(jid);
		bi.DestroyBody(jid);
	}

	[[nodiscard]] std::optional<BodyTransform> bodyTransform(BodyId id) const noexcept override
	{
		const JPH::BodyID jid = detail::jolt::toJoltId(id);
		if (jid.IsInvalid()) return std::nullopt;
		const JPH::BodyLockRead lock(m_system->GetBodyLockInterface(), jid);
		if (!lock.Succeeded()) return std::nullopt;
		const JPH::Body& body = lock.GetBody();
		return BodyTransform{detail::jolt::fromJolt(body.GetPosition()), detail::jolt::fromJolt(body.GetRotation())};
	}

	// ── Jolt 固有 ─────────────────────────────────────────────

	/// @brief 姿勢を瞬間移動で書き換える (Static / 外から位置を決めるボディ向け)
	void setTransform(BodyId id, const sgc::Vec3f& position, const sgc::Quaternionf& rotation) noexcept
	{
		m_system->GetBodyInterface().SetPositionAndRotation(detail::jolt::toJoltId(id),
			detail::jolt::toJoltR(position), detail::jolt::toJolt(rotation), JPH::EActivation::Activate);
	}

	/// @brief Kinematic を dt 秒かけて目標へ動かす。含意速度が出るので乗っている物も運ばれる
	void moveKinematic(BodyId id, const sgc::Vec3f& position, const sgc::Quaternionf& rotation, float dt) noexcept
	{
		auto& bi = m_system->GetBodyInterface();
		const JPH::BodyID jid = detail::jolt::toJoltId(id);
		if (!(dt > 0.0f) || jid.IsInvalid() || bi.GetMotionType(jid) != JPH::EMotionType::Kinematic) return;
		bi.MoveKinematic(jid, detail::jolt::toJoltR(position), detail::jolt::toJolt(rotation), dt);
	}

	void setVelocity(BodyId id, const sgc::Vec3f& linear, const sgc::Vec3f& angular) noexcept
	{
		m_system->GetBodyInterface().SetLinearAndAngularVelocity(detail::jolt::toJoltId(id),
			detail::jolt::toJolt(linear), detail::jolt::toJolt(angular));
	}

	[[nodiscard]] sgc::Vec3f linearVelocity(BodyId id) const noexcept
	{
		return detail::jolt::fromJolt(m_system->GetBodyInterface().GetLinearVelocity(detail::jolt::toJoltId(id)));
	}

	[[nodiscard]] sgc::Vec3f angularVelocity(BodyId id) const noexcept
	{
		return detail::jolt::fromJolt(m_system->GetBodyInterface().GetAngularVelocity(detail::jolt::toJoltId(id)));
	}

	void setMaterial(BodyId id, float friction, float restitution) noexcept
	{
		auto& bi = m_system->GetBodyInterface();
		bi.SetFriction(detail::jolt::toJoltId(id), friction);
		bi.SetRestitution(detail::jolt::toJoltId(id), restitution);
	}

	/// @brief 減衰は動くボディにしか無い (Static では何もしない)
	void setDamping(BodyId id, float linear, float angular) noexcept
	{
		JPH::BodyLockWrite lock(m_system->GetBodyLockInterface(), detail::jolt::toJoltId(id));
		if (!lock.Succeeded() || lock.GetBody().IsStatic()) return;
		JPH::MotionProperties* motion = lock.GetBody().GetMotionProperties();
		motion->SetLinearDamping(linear);
		motion->SetAngularDamping(angular);
	}

	/// @brief 所属レイヤー・衝突マスク・センサー (押し返さず接触だけ報告する) を変える
	void setFilter(BodyId id, std::uint32_t layer, std::uint32_t mask, bool isSensor) noexcept
	{
		JPH::BodyLockWrite lock(m_system->GetBodyLockInterface(), detail::jolt::toJoltId(id));
		if (!lock.Succeeded()) return;
		lock.GetBody().SetCollisionGroup(JPH::CollisionGroup(m_groupFilter, layer & 31u, mask));
		lock.GetBody().SetIsSensor(isSensor);
	}

	/// @brief 直近の step() 後に触れている接触。bodyA → bodyB の順で並ぶ
	[[nodiscard]] const std::vector<BodyContact3D>& contacts() const noexcept { return m_contacts; }

	[[nodiscard]] std::size_t bodyCount() const noexcept { return m_system->GetNumBodies(); }

	/// @brief 静的な地形をまとめて足した後に呼ぶと問い合わせが速くなる
	void optimizeBroadPhase() noexcept { m_system->OptimizeBroadPhase(); }

private:
	[[nodiscard]] static JPH::EMotionType motionTypeOf(BodyDesc::Type type) noexcept
	{
		switch (type)
		{
		case BodyDesc::Type::Static:    return JPH::EMotionType::Static;
		case BodyDesc::Type::Kinematic: return JPH::EMotionType::Kinematic;
		case BodyDesc::Type::Dynamic:   return JPH::EMotionType::Dynamic;
		}
		return JPH::EMotionType::Static;
	}

	void collectRayHits(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept
	{
		const float len = ray.direction.length();
		if (!(len > 1e-8f) || !(maxDist > 0.0f)) return;

		const JPH::RRayCast jray(detail::jolt::toJoltR(ray.origin), detail::jolt::toJolt(ray.direction) * (maxDist / len));
		JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
		const detail::jolt::QueryMaskFilter filter(mask);
		m_system->GetNarrowPhaseQuery().CastRay(jray, JPH::RayCastSettings(), collector, {}, {}, filter);

		for (const JPH::RayCastResult& hit : collector.mHits)
		{
			const JPH::BodyLockRead lock(m_system->GetBodyLockInterface(), hit.mBodyID);
			if (!lock.Succeeded()) continue;
			const JPH::Body& body = lock.GetBody();
			const JPH::RVec3 point = jray.GetPointOnRay(hit.mFraction);
			outHits.push_back(RayHit3D{detail::jolt::fromJolt(point),
				detail::jolt::fromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point)),
				hit.mFraction * maxDist, detail::jolt::toBodyId(hit.mBodyID), detail::jolt::layerOf(body)});
		}
	}

	void collectOverlaps(const JPH::Shape& shape, const sgc::Vec3f& center, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept
	{
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		const detail::jolt::QueryMaskFilter filter(mask);
		m_system->GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sOne(),
			JPH::RMat44::sTranslation(detail::jolt::toJoltR(center)), JPH::CollideShapeSettings(),
			JPH::RVec3::sZero(), collector, {}, {}, filter);

		m_scratchIds.clear();
		for (const auto& hit : collector.mHits) m_scratchIds.push_back(detail::jolt::toBodyId(hit.mBodyID2));
		std::sort(m_scratchIds.begin(), m_scratchIds.end());
		m_scratchIds.erase(std::unique(m_scratchIds.begin(), m_scratchIds.end()), m_scratchIds.end());
		for (const BodyId id : m_scratchIds) outResults.push_back(OverlapResult3D{id, 0});
	}

	// 宣言順 = 構築順。Runtime (Jolt の型登録) を最初に作り最後に破棄する
	detail::jolt::Runtime m_runtime;
	JoltWorldConfig m_config;
	sgc::Vec3f m_gravity;
	JPH::TempAllocatorImpl m_temp;
	std::unique_ptr<JPH::JobSystem> m_jobs;
	detail::jolt::BroadPhaseLayers m_bpLayers;
	detail::jolt::ObjectVsBroadPhase m_objVsBp;
	detail::jolt::ObjectPairs m_objPairs;
	JPH::Ref<JPH::GroupFilter> m_groupFilter;
	detail::jolt::ContactRecorder m_recorder;
	std::unique_ptr<JPH::PhysicsSystem> m_system;

	std::vector<BodyContact3D> m_contacts;
	mutable std::vector<RayHit3D> m_scratchHits;   ///< raycast の作業領域 (問い合わせ毎の確保を避ける)
	mutable std::vector<BodyId> m_scratchIds;      ///< overlap の作業領域
};

} // namespace mitiru::physics3d

#endif // MITIRU_HAS_JOLT
