#pragma once

/// @file PhysicsSystem3D.hpp
/// @brief RigidBodyComponent3D を Jolt Physics (JoltPhysicsWorld3D) で解く ECS システム
///
/// SystemRunner に足して使う。TransformComponent + RigidBodyComponent3D を持つエンティティを
/// ボディへ写し、固定ステップで進めて姿勢と速度を書き戻す (NativePhysicsSystem と同じ契約)。
/// 接触は毎フレーム CollisionEvent3D として、前フレームとの差分は Enter / Stay / Exit として通知する。
///
/// 決定論: エンティティは EntityId 順にボディへ写す (Jolt は API を呼ぶ順が同じなら結果が bit 一致する)。
/// Jolt を build していない構成では何もしない (warnOnce を 1 回出す)。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/math/Quaternion.hpp"
#include "sgc/math/Vec3.hpp"
#include "mitiru/debug/TracyZones.hpp"
#include "mitiru/debug/WarnOnce.hpp"
#include "mitiru/physics/IPhysicsWorld3D.hpp"
#include "mitiru/physics/JoltPhysicsWorld3D.hpp"
#include "mitiru/physics/RigidBodyComponent3D.hpp"
#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SystemRunner.hpp"

namespace mitiru::physics3d
{

/// @brief 物理システム設定
struct PhysicsSystem3DConfig
{
	sgc::Vec3f gravity{0.0f, -9.81f, 0.0f};  ///< 重力加速度
	float fixedTimeStep{1.0f / 60.0f};        ///< 固定タイムステップ（秒）
	int maxSubSteps{8};                        ///< 1 回の update で進める最大ステップ数
};

/// @brief 衝突イベント（そのフレームに触れている接触ごとに 1 件）
struct CollisionEvent3D
{
	scene::EntityId entityA{scene::INVALID_ENTITY};
	scene::EntityId entityB{scene::INVALID_ENTITY};
	sgc::Vec3f point{};
	sgc::Vec3f normal{};
	float depth{0.0f};
	bool isTrigger{false};  ///< どちらかがトリガー（押し返し無し）
};

using CollisionCallback3DEcs = std::function<void(const CollisionEvent3D&)>;

/// @brief 接触の推移段階
enum class ContactPhase3D
{
	Enter,  ///< このフレームで新たに接触が始まった
	Stay,   ///< 前フレームから引き続き接触している
	Exit    ///< このフレームで接触が終わった
};

/// @brief 接触イベント（エンティティの組ごとに 1 件）。Exit の point/normal/depth は既定値のまま
struct ContactEvent3D
{
	scene::EntityId entityA{scene::INVALID_ENTITY};
	scene::EntityId entityB{scene::INVALID_ENTITY};
	sgc::Vec3f point{};
	sgc::Vec3f normal{};
	float depth{0.0f};
	bool isTrigger{false};
	ContactPhase3D phase{ContactPhase3D::Enter};
};

using ContactEventCallback3D = std::function<void(const ContactEvent3D&)>;

class PhysicsSystem3D : public scene::ISystem
{
public:
	explicit PhysicsSystem3D(const PhysicsSystem3DConfig& config = {})
		: m_config(config)
	{
#ifdef MITIRU_HAS_JOLT
		m_world.setGravity(config.gravity);
#endif
	}

	[[nodiscard]] std::string name() const override { return "PhysicsSystem3D"; }

	void update(scene::GameWorld& world, float dt) override
	{
		MITIRU_ZONE_PHYSICS("PhysicsSystem3D::update");
#ifdef MITIRU_HAS_JOLT
		pruneRemoved(world);
		m_accumulator += dt;
		const int steps = plannedSteps();
		const float simulated = static_cast<float>(steps) * m_config.fixedTimeStep;
		syncToPhysics(world, simulated);
		for (int i = 0; i < steps; ++i) m_world.step(m_config.fixedTimeStep);
		m_accumulator -= simulated;
		// 1 歩も進めなかったフレームは物理側に新しい姿勢も接触も無い。書き戻すと、game が動かした
		// Kinematic を古い姿勢で上書きし、消した entity との接触を前回の台帳から通知してしまう
		if (steps == 0) return;
		syncFromPhysics(world);
		gatherContacts();
		notifyCollisions();
		notifyContactEvents();
#else
		(void)world;
		(void)dt;
		debug::warnOnce("physics.system3d.nojolt", "PhysicsSystem3D は Jolt を build していないので何もしない");
#endif
	}

	/// @brief 物理は Sim 固定であることを SystemRunner の既定値に依存させず明示する
	[[nodiscard]] scene::UpdatePhase defaultPhase() const noexcept override { return scene::UpdatePhase::Sim; }

	void setGravity(const sgc::Vec3f& gravity) noexcept
	{
		m_config.gravity = gravity;
#ifdef MITIRU_HAS_JOLT
		m_world.setGravity(gravity);
#endif
	}

	[[nodiscard]] const sgc::Vec3f& gravity() const noexcept { return m_config.gravity; }

	[[nodiscard]] const PhysicsSystem3DConfig& config() const noexcept { return m_config; }

	void registerCollisionCallback(CollisionCallback3DEcs callback) { m_collisionCallbacks.push_back(std::move(callback)); }

	void registerContactEventCallback(ContactEventCallback3D callback) { m_contactCallbacks.push_back(std::move(callback)); }

	/// @brief ボディを持っているエンティティの数
	[[nodiscard]] std::size_t bodyCount() const noexcept { return m_bodyToEntity.size(); }

	/// @brief 直近の update 後に触れている接触の数
	[[nodiscard]] std::size_t lastContactCount() const noexcept { return m_frameContacts.size(); }

	/// @brief 問い合わせ結果の BodyId からエンティティを引く（知らないボディなら INVALID_ENTITY）
	[[nodiscard]] scene::EntityId entityOf(BodyId body) const noexcept
	{
		const auto it = m_bodyToEntity.find(body);
		return (it != m_bodyToEntity.end()) ? it->second : scene::INVALID_ENTITY;
	}

	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist = 1e6f, std::uint32_t mask = 0xFFFFFFFFu) const noexcept
	{
#ifdef MITIRU_HAS_JOLT
		return m_world.raycast(ray, maxDist, mask);
#else
		(void)ray; (void)maxDist; (void)mask;
		return std::nullopt;
#endif
	}

	void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask, std::vector<RayHit3D>& outHits) const noexcept
	{
#ifdef MITIRU_HAS_JOLT
		m_world.raycastAll(ray, maxDist, mask, outHits);
#else
		(void)ray; (void)maxDist; (void)mask; (void)outHits;
#endif
	}

	void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept
	{
#ifdef MITIRU_HAS_JOLT
		m_world.overlapSphere(center, radius, mask, outResults);
#else
		(void)center; (void)radius; (void)mask; (void)outResults;
#endif
	}

	void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask, std::vector<OverlapResult3D>& outResults) const noexcept
	{
#ifdef MITIRU_HAS_JOLT
		m_world.overlapBox(aabb, mask, outResults);
#else
		(void)aabb; (void)mask; (void)outResults;
#endif
	}

#ifdef MITIRU_HAS_JOLT
	/// @brief バックエンドへの直接アクセス（メッシュ地形を足す等）
	[[nodiscard]] JoltPhysicsWorld3D& backend() noexcept { return m_world; }
#endif

private:
	/// @brief エンティティ 1 体ぶんの対応。last は最後にボディへ流し込んだ値（差分だけを Jolt へ送る）
	struct Tracked
	{
		BodyId body{kInvalidBodyId};
		RigidBodyComponent3D last{};
		scene::TransformComponent lastTransform{};
	};

	/// @brief 接触の組。a <= b に揃え、(a, b, isTrigger) で並べる
	struct PairRecord
	{
		scene::EntityId a{scene::INVALID_ENTITY};
		scene::EntityId b{scene::INVALID_ENTITY};
		sgc::Vec3f point{};
		sgc::Vec3f normal{};
		float depth{0.0f};
		bool isTrigger{false};

		[[nodiscard]] static bool less(const PairRecord& l, const PairRecord& r) noexcept
		{
			if (l.a != r.a) return l.a < r.a;
			if (l.b != r.b) return l.b < r.b;
			return l.isTrigger < r.isTrigger;
		}
	};

	[[nodiscard]] int plannedSteps() const noexcept
	{
		if (!(m_config.fixedTimeStep > 0.0f)) return 0;
		const int due = static_cast<int>(m_accumulator / m_config.fixedTimeStep);
		return std::clamp(due, 0, std::max(0, m_config.maxSubSteps));
	}

	[[nodiscard]] static sgc::Quaternionf toQuat(const sgc::Vec3f& euler) noexcept
	{
		return sgc::Quaternionf::fromEuler(euler.x, euler.y, euler.z);
	}

	[[nodiscard]] static bool isStatic(const RigidBodyComponent3D& rb) noexcept
	{
		return !rb.isKinematic && rb.mass <= 0.0f;
	}

	/// @brief 形・質量・動き方が変わったらボディを作り直す（Jolt のボディは形を後から変えられない）。
	/// メッシュは Transform の scale を頂点へ焼き込むので、scale の変化も作り直しになる
	[[nodiscard]] static bool needsRebuild(const Tracked& tracked, const scene::TransformComponent& t,
		const RigidBodyComponent3D& b) noexcept
	{
		const RigidBodyComponent3D& a = tracked.last;
		const sgc::Vec3f& s0 = tracked.lastTransform.scale;
		const bool meshScaled = b.colliderType == ColliderType3D::Mesh &&
			(s0.x != t.scale.x || s0.y != t.scale.y || s0.z != t.scale.z);
		return a.colliderType != b.colliderType || a.colliderRadius != b.colliderRadius ||
			a.colliderHalfExtents.x != b.colliderHalfExtents.x || a.colliderHalfExtents.y != b.colliderHalfExtents.y ||
			a.colliderHalfExtents.z != b.colliderHalfExtents.z || a.capsuleHeight != b.capsuleHeight ||
			a.colliderMesh != b.colliderMesh || meshScaled || a.isKinematic != b.isKinematic || a.mass != b.mass;
	}

	/// @brief Mesh の頂点は m_meshScratch へ scale を掛けて写し、BodyDesc はそれを指す (addBody の中で写し取られる)
	[[nodiscard]] BodyDesc describe(const scene::TransformComponent& t, const RigidBodyComponent3D& rb)
	{
		BodyDesc d;
		d.type = rb.isKinematic ? BodyDesc::Type::Kinematic
			: (rb.mass <= 0.0f ? BodyDesc::Type::Static : BodyDesc::Type::Dynamic);
		switch (rb.colliderType)
		{
		case ColliderType3D::Sphere:  d.shape = BodyDesc::Shape::Sphere; d.radius = rb.colliderRadius; break;
		case ColliderType3D::Box:     d.shape = BodyDesc::Shape::Box; d.halfExtents = rb.colliderHalfExtents; break;
		case ColliderType3D::Capsule:
			d.shape = BodyDesc::Shape::Capsule;
			d.radius = rb.colliderRadius;
			d.halfHeight = rb.capsuleHeight * 0.5f;
			break;
		case ColliderType3D::Mesh:
			d.shape = BodyDesc::Shape::Mesh;
			describeMesh(t.scale, rb.colliderMesh.get(), d);
			break;
		}
		d.position = t.position;
		d.rotation = toQuat(t.rotation);
		d.mass = rb.mass;
		d.layer = rb.layer;
		d.mask = rb.mask;
		return d;
	}

	void describeMesh(const sgc::Vec3f& scale, const TriangleMesh3D* mesh, BodyDesc& d)
	{
		if (mesh == nullptr) return;   // 頂点の無い Mesh は addBody が警告して断る
		m_meshScratch.clear();
		for (const sgc::Vec3f& v : mesh->vertices) m_meshScratch.push_back(sgc::Vec3f{v.x * scale.x, v.y * scale.y, v.z * scale.z});
		d.meshVertices = m_meshScratch.data();
		d.meshVertexCount = m_meshScratch.size();
		d.meshIndices = mesh->indices.data();
		d.meshIndexCount = mesh->indices.size();
	}

#ifdef MITIRU_HAS_JOLT
	/// @brief 写す条件 (Transform と RigidBody の両方) を満たさなくなったエンティティのボディを消す
	void pruneRemoved(scene::GameWorld& world)
	{
		for (auto it = m_bodies.begin(); it != m_bodies.end();)
		{
			const bool mapped = world.getComponent<RigidBodyComponent3D>(it->first) != nullptr &&
				world.getComponent<scene::TransformComponent>(it->first) != nullptr;
			if (mapped) { ++it; continue; }
			dropBody(it->second);
			it = m_bodies.erase(it);
		}
	}

	void dropBody(const Tracked& tracked)
	{
		if (tracked.body == kInvalidBodyId) return;
		m_world.removeBody(tracked.body);
		m_bodyToEntity.erase(tracked.body);
	}

	/// @details forEach の順はストレージ依存なので、一度 EntityId 順に並べてから Jolt を呼ぶ
	void syncToPhysics(scene::GameWorld& world, float simulated)
	{
		m_syncOrder.clear();
		world.forEach<RigidBodyComponent3D>([this](scene::EntityId id, RigidBodyComponent3D&) { m_syncOrder.push_back(id); });
		std::sort(m_syncOrder.begin(), m_syncOrder.end());

		for (const scene::EntityId id : m_syncOrder)
		{
			const auto* transform = world.getComponent<scene::TransformComponent>(id);
			const auto* rb = world.getComponent<RigidBodyComponent3D>(id);
			if (transform == nullptr || rb == nullptr) continue;

			auto it = m_bodies.find(id);
			if (it != m_bodies.end() && needsRebuild(it->second, *transform, *rb))
			{
				dropBody(it->second);
				m_bodies.erase(it);
				it = m_bodies.end();
			}
			if (it == m_bodies.end()) createBody(id, *transform, *rb);
			else updateBody(it->second, *transform, *rb, simulated);
		}
	}

	void createBody(scene::EntityId id, const scene::TransformComponent& transform, const RigidBodyComponent3D& rb)
	{
		Tracked tracked;
		tracked.body = m_world.addBody(describe(transform, rb));
		tracked.last = rb;
		tracked.lastTransform = transform;
		m_bodies.emplace(id, tracked);
		if (tracked.body == kInvalidBodyId) return;

		m_bodyToEntity.emplace(tracked.body, id);
		if (rb.isTrigger) m_world.setFilter(tracked.body, rb.layer, rb.mask, true);
		m_world.setMaterial(tracked.body, rb.friction, rb.restitution);
		m_world.setDamping(tracked.body, rb.linearDamping, rb.angularDamping);
		if (!isStatic(rb)) m_world.setVelocity(tracked.body, rb.linearVelocity, rb.angularVelocity);
	}

	void updateBody(Tracked& tracked, const scene::TransformComponent& transform, const RigidBodyComponent3D& rb,
		float simulated)
	{
		if (tracked.body == kInvalidBodyId) return;
		const RigidBodyComponent3D& prev = tracked.last;

		if (rb.isKinematic)
			m_world.moveKinematic(tracked.body, transform.position, toQuat(transform.rotation), simulated);
		else if (isStatic(rb) && poseChanged(tracked.lastTransform, transform))
			m_world.setTransform(tracked.body, transform.position, toQuat(transform.rotation));

		if (prev.friction != rb.friction || prev.restitution != rb.restitution)
			m_world.setMaterial(tracked.body, rb.friction, rb.restitution);
		if (prev.linearDamping != rb.linearDamping || prev.angularDamping != rb.angularDamping)
			m_world.setDamping(tracked.body, rb.linearDamping, rb.angularDamping);
		if (prev.isTrigger != rb.isTrigger || prev.layer != rb.layer || prev.mask != rb.mask)
			m_world.setFilter(tracked.body, rb.layer, rb.mask, rb.isTrigger);

		tracked.last = rb;
		tracked.lastTransform = transform;
	}

	[[nodiscard]] static bool poseChanged(const scene::TransformComponent& a, const scene::TransformComponent& b) noexcept
	{
		return a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z ||
			a.rotation.x != b.rotation.x || a.rotation.y != b.rotation.y || a.rotation.z != b.rotation.z;
	}

	/// @brief Static は動かないので書き戻さない（Euler の往復で値が揺れて「動いた」と誤検出しないため）
	void syncFromPhysics(scene::GameWorld& world)
	{
		for (auto& [id, tracked] : m_bodies)
		{
			if (tracked.body == kInvalidBodyId || isStatic(tracked.last)) continue;
			auto* transform = world.getComponent<scene::TransformComponent>(id);
			auto* rb = world.getComponent<RigidBodyComponent3D>(id);
			const auto pose = m_world.bodyTransform(tracked.body);
			if (transform == nullptr || rb == nullptr || !pose.has_value()) continue;

			transform->position = pose->position;
			transform->rotation = pose->rotation.toEuler();
			rb->linearVelocity = m_world.linearVelocity(tracked.body);
			rb->angularVelocity = m_world.angularVelocity(tracked.body);
			tracked.last = *rb;
			tracked.lastTransform = *transform;
		}
	}

	void gatherContacts()
	{
		m_frameContacts.clear();
		for (const BodyContact3D& c : m_world.contacts())
		{
			PairRecord r;
			r.a = entityOf(c.bodyA);
			r.b = entityOf(c.bodyB);
			r.normal = c.normal;
			if (r.b < r.a) { std::swap(r.a, r.b); r.normal = r.normal * -1.0f; }
			r.point = c.point;
			r.depth = c.depth;
			r.isTrigger = c.isSensor;
			m_frameContacts.push_back(r);
		}
	}
#endif // MITIRU_HAS_JOLT

	void notifyCollisions()
	{
		if (m_collisionCallbacks.empty()) return;
		for (const PairRecord& r : m_frameContacts)
		{
			const CollisionEvent3D event{r.a, r.b, r.point, r.normal, r.depth, r.isTrigger};
			for (const auto& cb : m_collisionCallbacks) cb(event);
		}
	}

	/// @details 今フレームの組を (a, b, isTrigger) で 1 件にまとめてから前フレームと突き合わせる。
	///          メッシュのように 1 組で複数の接触を持つと、まとめずに比べると Enter が重複する。
	void notifyContactEvents()
	{
		m_currentPairs.assign(m_frameContacts.begin(), m_frameContacts.end());
		std::sort(m_currentPairs.begin(), m_currentPairs.end(), PairRecord::less);
		m_currentPairs.erase(std::unique(m_currentPairs.begin(), m_currentPairs.end(),
			[](const PairRecord& x, const PairRecord& y) noexcept { return !PairRecord::less(x, y) && !PairRecord::less(y, x); }),
			m_currentPairs.end());

		std::size_t i = 0, j = 0;
		while (i < m_previousPairs.size() || j < m_currentPairs.size())
		{
			const bool hasPrev = i < m_previousPairs.size();
			const bool hasCurr = j < m_currentPairs.size();
			if (hasPrev && (!hasCurr || PairRecord::less(m_previousPairs[i], m_currentPairs[j])))
				emitContact(exitOf(m_previousPairs[i++]), ContactPhase3D::Exit);
			else if (hasCurr && (!hasPrev || PairRecord::less(m_currentPairs[j], m_previousPairs[i])))
				emitContact(m_currentPairs[j++], ContactPhase3D::Enter);
			else
			{
				emitContact(m_currentPairs[j++], ContactPhase3D::Stay);
				++i;
			}
		}
		std::swap(m_previousPairs, m_currentPairs);
	}

	[[nodiscard]] static PairRecord exitOf(const PairRecord& r) noexcept
	{
		PairRecord e;
		e.a = r.a;
		e.b = r.b;
		e.isTrigger = r.isTrigger;
		return e;
	}

	void emitContact(const PairRecord& r, ContactPhase3D phase) const
	{
		const ContactEvent3D event{r.a, r.b, r.point, r.normal, r.depth, r.isTrigger, phase};
		for (const auto& cb : m_contactCallbacks) cb(event);
	}

	PhysicsSystem3DConfig m_config;
	float m_accumulator{0.0f};

#ifdef MITIRU_HAS_JOLT
	JoltPhysicsWorld3D m_world;
#endif
	std::map<scene::EntityId, Tracked> m_bodies;                 ///< EntityId 順に回すので順序付き
	std::unordered_map<BodyId, scene::EntityId> m_bodyToEntity;  ///< 引くだけ（走査しない）
	std::vector<scene::EntityId> m_syncOrder;                    ///< syncToPhysics の作業領域
	std::vector<sgc::Vec3f> m_meshScratch;                       ///< describe の作業領域 (scale を掛けたメッシュ頂点)

	std::vector<PairRecord> m_frameContacts;
	std::vector<PairRecord> m_currentPairs;
	std::vector<PairRecord> m_previousPairs;
	std::vector<CollisionCallback3DEcs> m_collisionCallbacks;
	std::vector<ContactEventCallback3D> m_contactCallbacks;
};

} // namespace mitiru::physics3d
