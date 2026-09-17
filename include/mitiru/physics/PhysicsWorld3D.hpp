#pragma once

/// @file PhysicsWorld3D.hpp
/// @brief sweep-and-prune とインパルスベースの接触解決を使う 3D 物理ワールドの宣言

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/debug/TracyZones.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/CollisionDetection3D.hpp"
#include "mitiru/physics/RigidBody3D.hpp"
#include "mitiru/physics/Constraints3D.hpp"
#include "mitiru/physics/ContactSolver3D.hpp"
#include "mitiru/physics/IPhysicsWorld3D.hpp"

namespace mitiru::physics3d
{

enum class ColliderType3D
{
	Sphere,
	AABB,
	Capsule
};

struct BodyCollider
{
	BodyId bodyId{INVALID_BODY_ID};
	ColliderType3D type{ColliderType3D::Sphere};
	SphereCollider sphere{};
	AABBCollider3D aabb{};
	CapsuleCollider capsule{};

	bool isTrigger{false};              ///< true ならインパルス解決せずコールバックのみ発火
	std::uint32_t layer{0};             ///< 所属レイヤー（0-31のビットindex）
	std::uint32_t mask{0xFFFFFFFFu};    ///< 衝突対象レイヤーマスク
};

struct WorldContact3D
{
	BodyId bodyIdA{INVALID_BODY_ID};
	BodyId bodyIdB{INVALID_BODY_ID};
	ContactInfo3D contact{};
	bool isTrigger{false};  ///< どちらかのコライダーがトリガーの場合 true（インパルス未適用）

	// box-box の接触点が 0 件なら、contact の 1 点を単一マニフォールドとして扱う。
	// callback と lastContactCount は接触点ではなくペア単位で数える。
	std::array<ManifoldPoint3D, kMaxManifoldPoints3D> manifoldPoints{};
	std::size_t manifoldPointCount{0};
};

/// @details HE2 の GOCCollider::filterCategory と同じ双方向ビット判定
[[nodiscard]] constexpr bool layersCollide(
	std::uint32_t layerA, std::uint32_t maskA,
	std::uint32_t layerB, std::uint32_t maskB) noexcept
{
	// layer は契約上 0-31 だが呼び出し側 (createBody 系) は検証しておらず、範囲外の値を
	// 渡されると `1u << layer` がシフト量 >= 32 の未定義動作になる。ここで & 31u しておけば
	// 契約どおりの値ではノーコストで無害、契約違反でも UB を踏まずに済む。
	return ((maskA & (1u << (layerB & 31u))) != 0u) && ((maskB & (1u << (layerA & 31u))) != 0u);
}

[[nodiscard]] constexpr bool layerMatchesQuery(std::uint32_t colliderLayer, std::uint32_t queryMask) noexcept
{
	return (queryMask & (1u << (colliderLayer & 31u))) != 0u;
}

// OverlapResult3D は IPhysicsWorld3D.hpp と共有するため、CollisionDetection3D.hpp で定義する

using CollisionCallback3D = std::function<void(const WorldContact3D&)>;

struct BroadphaseProxy
{
	BodyId bodyId{INVALID_BODY_ID};
	AABBCollider3D aabb{};

	[[nodiscard]] constexpr float minX() const noexcept { return aabb.min.x; }
};

/// @brief ブロードフェーズ、ナローフェーズ、接触解決、拘束ソルバーの順で処理する
class PhysicsWorld3D : public IPhysicsWorld3D
{
public:
	PhysicsWorld3D() = default;

	// ── 重力 ──────────────────────────────────────────────────

	/// @param gravity 重力加速度。単位は m/s^2
	void setGravity(const sgc::Vec3f& gravity) noexcept override { m_gravity = gravity; }

	[[nodiscard]] constexpr const sgc::Vec3f& gravity() const noexcept override { return m_gravity; }

	// ── ボディ管理 ────────────────────────────────────────────

	RigidBody3D& addBody() noexcept
	{
		const BodyId id = m_nextBodyId++;
		auto body = std::make_unique<RigidBody3D>();
		body->setId(id);
		auto& ref = *body;
		m_bodies[id] = std::move(body);
		return ref;
	}

	void removeBody(BodyId id) noexcept override
	{
		m_bodies.erase(id);

		const std::size_t before = m_colliders.size();
		m_colliders.erase(
			std::remove_if(m_colliders.begin(), m_colliders.end(),
				[id](const BodyCollider& c) { return c.bodyId == id; }),
			m_colliders.end());
		if (m_colliders.size() != before) ++m_topologyVersion;
	}

	[[nodiscard]] BodyId addBody(const BodyDesc& desc) noexcept override
	{
		auto& body = addBody();
		body.setMass(desc.type == BodyDesc::Type::Dynamic ? desc.mass : 0.0f);
		body.setPosition(desc.position);
		body.setRotation(desc.rotation);

		const bool isTrigger = false;
		switch (desc.shape)
		{
		case BodyDesc::Shape::Sphere:
			addSphereCollider(body.id(), SphereCollider{desc.position, desc.radius},
				isTrigger, desc.layer, desc.mask);
			break;
		case BodyDesc::Shape::Box:
			addAABBCollider(body.id(),
				AABBCollider3D::fromCenterExtents(desc.position, desc.halfExtents),
				isTrigger, desc.layer, desc.mask);
			break;
		case BodyDesc::Shape::Capsule:
			{
				const sgc::Vec3f axis{0.0f, desc.halfHeight, 0.0f};
				addCapsuleCollider(body.id(),
					CapsuleCollider{desc.position - axis, desc.position + axis, desc.radius},
					isTrigger, desc.layer, desc.mask);
			}
			break;
		}

		return body.id();
	}

	[[nodiscard]] std::optional<BodyTransform> bodyTransform(BodyId id) const noexcept override
	{
		const RigidBody3D* body = getBody(id);
		if (!body) return std::nullopt;
		return BodyTransform{body->position(), body->rotation()};
	}

	[[nodiscard]] RigidBody3D* getBody(BodyId id) noexcept
	{
		auto it = m_bodies.find(id);
		return (it != m_bodies.end()) ? it->second.get() : nullptr;
	}

	[[nodiscard]] const RigidBody3D* getBody(BodyId id) const noexcept
	{
		auto it = m_bodies.find(id);
		return (it != m_bodies.end()) ? it->second.get() : nullptr;
	}

	[[nodiscard]] std::size_t bodyCount() const noexcept { return m_bodies.size(); }

	// ── コライダー管理 ────────────────────────────────────────

	/// @param isTrigger true なら押し返さず、コールバックだけを呼ぶ
	/// @param layer 所属レイヤー。範囲は 0 から 31
	/// @param mask 衝突対象のレイヤーマスク
	void addSphereCollider(BodyId bodyId, const SphereCollider& sphere,
		bool isTrigger = false, std::uint32_t layer = 0, std::uint32_t mask = 0xFFFFFFFFu) noexcept
	{
		BodyCollider bc;
		bc.bodyId = bodyId;
		bc.type = ColliderType3D::Sphere;
		bc.sphere = sphere;
		bc.isTrigger = isTrigger;
		bc.layer = layer;
		bc.mask = mask;
		m_colliders.push_back(bc);
		++m_topologyVersion;
	}

	/// @param isTrigger true なら押し返さず、コールバックだけを呼ぶ
	/// @param layer 所属レイヤー。範囲は 0 から 31
	/// @param mask 衝突対象のレイヤーマスク
	void addAABBCollider(BodyId bodyId, const AABBCollider3D& aabb,
		bool isTrigger = false, std::uint32_t layer = 0, std::uint32_t mask = 0xFFFFFFFFu) noexcept
	{
		BodyCollider bc;
		bc.bodyId = bodyId;
		bc.type = ColliderType3D::AABB;
		bc.aabb = aabb;
		bc.isTrigger = isTrigger;
		bc.layer = layer;
		bc.mask = mask;
		m_colliders.push_back(bc);
		++m_topologyVersion;
	}

	/// @param isTrigger true なら押し返さず、コールバックだけを呼ぶ
	/// @param layer 所属レイヤー。範囲は 0 から 31
	/// @param mask 衝突対象のレイヤーマスク
	void addCapsuleCollider(BodyId bodyId, const CapsuleCollider& capsule,
		bool isTrigger = false, std::uint32_t layer = 0, std::uint32_t mask = 0xFFFFFFFFu) noexcept
	{
		BodyCollider bc;
		bc.bodyId = bodyId;
		bc.type = ColliderType3D::Capsule;
		bc.capsule = capsule;
		bc.isTrigger = isTrigger;
		bc.layer = layer;
		bc.mask = mask;
		m_colliders.push_back(bc);
		++m_topologyVersion;
	}

	[[nodiscard]] std::size_t colliderCount() const noexcept { return m_colliders.size(); }

	// ── 拘束 ──────────────────────────────────────────────────

	void addDistanceConstraint(DistanceConstraint constraint) noexcept
	{
		m_distanceConstraints.push_back(std::move(constraint));
	}

	void addHingeConstraint(HingeConstraint constraint) noexcept
	{
		m_hingeConstraints.push_back(std::move(constraint));
	}

	[[nodiscard]] std::size_t constraintCount() const noexcept
	{
		return m_distanceConstraints.size() + m_hingeConstraints.size();
	}

	// ── コールバック ──────────────────────────────────────────

	void registerCollisionCallback(CollisionCallback3D callback) noexcept
	{
		m_callbacks.push_back(std::move(callback));
	}

	// ── シミュレーション ──────────────────────────────────────

	/// @details 接触と拘束の両方に適用する
	void setSolverIterations(int iterations) noexcept
	{
		m_solverIterations = iterations;
		ContactSolverConfig3D cfg = m_contactSolver.config();
		cfg.iterations = iterations;
		m_contactSolver.configure(cfg);
	}

	/// @brief 接触ソルバーの warm start、Baumgarte 補正、スリープを設定する
	void configureContactSolver(const ContactSolverConfig3D& config) noexcept
	{
		m_contactSolver.configure(config);
	}

	[[nodiscard]] const ContactSolverConfig3D& contactSolverConfig() const noexcept
	{
		return m_contactSolver.config();
	}

	/// @param dt タイムステップ。単位は秒
	void step(float dt) noexcept override
	{
		MITIRU_ZONE_PHYSICS("PhysicsWorld3D::step");

		// 1. 重力を適用
		applyGravity();

		// 2. 積分（速度→位置）
		for (auto& [id, body] : m_bodies)
		{
			body->integrate(dt);
		}

		// 3. コライダー位置を同期
		syncColliders();

		// 4. ブロードフェーズ
		const auto broadPairs = broadphase();

		// 5. ナローフェーズ
		const auto contacts = narrowphase(broadPairs);

		// 6. 接触解決（インパルスベース、ContactSolver3D に委譲）
		resolveContacts(contacts, dt);

		// 7. 拘束ソルバー
		solveConstraints(dt);

		// 8. コールバック通知
		notifyCallbacks(contacts);
	}

	[[nodiscard]] std::size_t lastContactCount() const noexcept { return m_lastContactCount; }

	/// @brief PhysicsSystem3D との統合用にボディマップを返す
	[[nodiscard]] std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies() noexcept
	{
		return m_bodies;
	}

	[[nodiscard]] const std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies() const noexcept
	{
		return m_bodies;
	}

	/// @brief PhysicsSystem3D との統合用にコライダー配列を返す
	[[nodiscard]] std::vector<BodyCollider>& colliders() noexcept
	{
		return m_colliders;
	}

	[[nodiscard]] const std::vector<BodyCollider>& colliders() const noexcept
	{
		return m_colliders;
	}

	/// @brief コライダーを追加または削除するたびに増える世代番号
	/// @details コライダーの要素を書き換えても増えない
	[[nodiscard]] std::uint64_t topologyVersion() const noexcept
	{
		return m_topologyVersion;
	}

	[[nodiscard]] static AABBCollider3D computeAABB(const BodyCollider& collider) noexcept
	{
		switch (collider.type)
		{
		case ColliderType3D::Sphere:
		{
			const sgc::Vec3f r{collider.sphere.radius, collider.sphere.radius, collider.sphere.radius};
			return {collider.sphere.center - r, collider.sphere.center + r};
		}
		case ColliderType3D::AABB:
			return collider.aabb;
		case ColliderType3D::Capsule:
		{
			const sgc::Vec3f r{collider.capsule.radius, collider.capsule.radius, collider.capsule.radius};
			const sgc::Vec3f minPt = sgc::Vec3f::min(collider.capsule.pointA, collider.capsule.pointB) - r;
			const sgc::Vec3f maxPt = sgc::Vec3f::max(collider.capsule.pointA, collider.capsule.pointB) + r;
			return {minPt, maxPt};
		}
		}
		return {};
	}

	// ── レイキャスト ──────────────────────────────────────────

	/// @return 最も近いヒット結果
	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist = 1e6f, std::uint32_t mask = 0xFFFFFFFFu) const noexcept override
	{
		std::optional<RayHit3D> closest;

		for (const auto& collider : m_colliders)
		{
			if (!layerMatchesQuery(collider.layer, mask)) continue;

			const std::optional<RayHit3D> hit = raycastCollider(ray, collider, maxDist);

			if (hit.has_value())
			{
				if (!closest.has_value() || hit->distance < closest->distance)
				{
					closest = hit;
				}
			}
		}

		return closest;
	}

	/// @param outHits ヒット結果の追加先
	void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept override
	{
		for (const auto& collider : m_colliders)
		{
			if (!layerMatchesQuery(collider.layer, mask)) continue;

			const std::optional<RayHit3D> hit = raycastCollider(ray, collider, maxDist);
			if (hit.has_value())
			{
				outHits.push_back(*hit);
			}
		}
	}

	/// @param outResults ヒット結果の追加先
	void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		const SphereCollider query{center, radius};

		for (std::size_t i = 0; i < m_colliders.size(); ++i)
		{
			const auto& collider = m_colliders[i];
			if (!layerMatchesQuery(collider.layer, mask)) continue;

			bool hit = false;
			switch (collider.type)
			{
			case ColliderType3D::Sphere:
				hit = testSphereSphere(query, collider.sphere).hasContact;
				break;
			case ColliderType3D::AABB:
				hit = testSphereAABB(query, collider.aabb).hasContact;
				break;
			case ColliderType3D::Capsule:
				hit = testSphereCapsule(query, collider.capsule).hasContact;
				break;
			}

			if (hit)
			{
				outResults.push_back(OverlapResult3D{collider.bodyId, i});
			}
		}
	}

	/// @brief AABB とのオーバーラップを調べる
	/// @details カプセルは包含 AABB で判定するため、精密な押し出しには使わない
	void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept override
	{
		for (std::size_t i = 0; i < m_colliders.size(); ++i)
		{
			const auto& collider = m_colliders[i];
			if (!layerMatchesQuery(collider.layer, mask)) continue;

			bool hit = false;
			switch (collider.type)
			{
			case ColliderType3D::Sphere:
				hit = testSphereAABB(collider.sphere, aabb).hasContact;
				break;
			case ColliderType3D::AABB:
				hit = testAABBAABB(collider.aabb, aabb).hasContact;
				break;
			case ColliderType3D::Capsule:
				hit = testAABBAABB(computeAABB(collider), aabb).hasContact;
				break;
			}

			if (hit)
			{
				outResults.push_back(OverlapResult3D{collider.bodyId, i});
			}
		}
	}

private:
	/// @brief Sphere と AABB のみを対象にする
	[[nodiscard]] static std::optional<RayHit3D> raycastCollider(
		const Ray3D& ray, const BodyCollider& collider, float maxDist) noexcept
	{
		switch (collider.type)
		{
		case ColliderType3D::Sphere:
			return raycastSphere(ray, collider.sphere, maxDist);
		case ColliderType3D::AABB:
			return raycastAABB(ray, collider.aabb, maxDist);
		default:
			return std::nullopt;
		}
	}

	void applyGravity() noexcept
	{
		for (auto& [id, body] : m_bodies)
		{
			// スリープ中は重力を止める。
			// 重力を加え続けると接触が毎フレーム起こされ、スリープに入れない。
			if (!body->isStatic() && !body->isAsleep())
			{
				body->applyForce(m_gravity * body->mass());
			}
		}
	}

	void syncColliders() noexcept
	{
		for (auto& collider : m_colliders)
		{
			const auto* body = getBody(collider.bodyId);
			if (!body) continue;

			const sgc::Vec3f& pos = body->position();

			switch (collider.type)
			{
			case ColliderType3D::Sphere:
				collider.sphere.center = pos;
				break;
			case ColliderType3D::AABB:
			{
				const sgc::Vec3f half = collider.aabb.halfExtents();
				collider.aabb.min = pos - half;
				collider.aabb.max = pos + half;
				break;
			}
			case ColliderType3D::Capsule:
			{
				const sgc::Vec3f center = collider.capsule.center();
				const sgc::Vec3f offset = pos - center;
				collider.capsule.pointA += offset;
				collider.capsule.pointB += offset;
				break;
			}
			}
		}
	}

	/// @brief sweep-and-prune によるブロードフェーズ
	[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> broadphase() const noexcept
	{
		if (m_colliders.size() < 2) return {};

		std::vector<BroadphaseProxy> proxies;
		proxies.reserve(m_colliders.size());

		for (std::size_t i = 0; i < m_colliders.size(); ++i)
		{
			BroadphaseProxy proxy;
			proxy.bodyId = static_cast<BodyId>(i);
			proxy.aabb = computeAABB(m_colliders[i]);
			proxies.push_back(proxy);
		}

		std::sort(proxies.begin(), proxies.end(),
			[](const BroadphaseProxy& a, const BroadphaseProxy& b)
			{
				return a.minX() < b.minX();
			});

		// Sweep-and-prune
		std::vector<std::pair<std::size_t, std::size_t>> pairs;

		for (std::size_t i = 0; i < proxies.size(); ++i)
		{
			for (std::size_t j = i + 1; j < proxies.size(); ++j)
			{
				if (proxies[j].aabb.min.x > proxies[i].aabb.max.x) break;

				if (proxies[i].aabb.max.y < proxies[j].aabb.min.y ||
					proxies[i].aabb.min.y > proxies[j].aabb.max.y) continue;

				if (proxies[i].aabb.max.z < proxies[j].aabb.min.z ||
					proxies[i].aabb.min.z > proxies[j].aabb.max.z) continue;

				if (m_colliders[proxies[i].bodyId].bodyId ==
					m_colliders[proxies[j].bodyId].bodyId) continue;

				const auto& colA = m_colliders[proxies[i].bodyId];
				const auto& colB = m_colliders[proxies[j].bodyId];
				if (!layersCollide(colA.layer, colA.mask, colB.layer, colB.mask)) continue;

				pairs.emplace_back(proxies[i].bodyId, proxies[j].bodyId);
			}
		}

		return pairs;
	}

	/// @brief box-box の最大 4 接触点を contacts に積む
	/// @details points[0] を代表点として contact に入れ、callback と lastContactCount はペア単位で数える
	static void pushAABBManifoldContact(
		const BodyCollider& colA, const BodyCollider& colB,
		std::vector<WorldContact3D>& contacts) noexcept
	{
		const auto manifold = testAABBAABBManifold(colA.aabb, colB.aabb);
		if (!manifold.hasContact) return;

		WorldContact3D wc;
		wc.bodyIdA = colA.bodyId;
		wc.bodyIdB = colB.bodyId;
		wc.contact = ContactInfo3D{
			manifold.points[0].point, manifold.normal, manifold.points[0].depth, true};
		wc.isTrigger = colA.isTrigger || colB.isTrigger;
		wc.manifoldPointCount = manifold.count;
		for (std::size_t i = 0; i < manifold.count; ++i)
		{
			wc.manifoldPoints[i] = manifold.points[i];
		}
		contacts.push_back(wc);
	}

	[[nodiscard]] std::vector<WorldContact3D> narrowphase(
		const std::vector<std::pair<std::size_t, std::size_t>>& pairs) const noexcept
	{
		std::vector<WorldContact3D> contacts;

		for (const auto& [idxA, idxB] : pairs)
		{
			const auto& colA = m_colliders[idxA];
			const auto& colB = m_colliders[idxB];

			// box-box は複数の接触点を持つため、専用の処理に分ける
			if (colA.type == ColliderType3D::AABB && colB.type == ColliderType3D::AABB)
			{
				pushAABBManifoldContact(colA, colB, contacts);
				continue;
			}

			ContactInfo3D info;

			if (colA.type == ColliderType3D::Sphere && colB.type == ColliderType3D::Sphere)
			{
				info = testSphereSphere(colA.sphere, colB.sphere);
			}
			else if (colA.type == ColliderType3D::Sphere && colB.type == ColliderType3D::AABB)
			{
				info = testSphereAABB(colA.sphere, colB.aabb);
			}
			else if (colA.type == ColliderType3D::AABB && colB.type == ColliderType3D::Sphere)
			{
				info = testSphereAABB(colB.sphere, colA.aabb);
				info.normal = -info.normal;
			}
			else if (colA.type == ColliderType3D::Sphere && colB.type == ColliderType3D::Capsule)
			{
				info = testSphereCapsule(colA.sphere, colB.capsule);
			}
			else if (colA.type == ColliderType3D::Capsule && colB.type == ColliderType3D::Sphere)
			{
				info = testSphereCapsule(colB.sphere, colA.capsule);
				info.normal = -info.normal;
			}
			else if (colA.type == ColliderType3D::Capsule && colB.type == ColliderType3D::Capsule)
			{
				info = testCapsuleCapsule(colA.capsule, colB.capsule);
			}
			else if (colA.type == ColliderType3D::Capsule && colB.type == ColliderType3D::AABB)
			{
				info = testCapsuleAABB(colA.capsule, colB.aabb);
			}
			else if (colA.type == ColliderType3D::AABB && colB.type == ColliderType3D::Capsule)
			{
				info = testCapsuleAABB(colB.capsule, colA.aabb);
				info.normal = -info.normal;
			}

			if (info.hasContact)
			{
				WorldContact3D wc;
				wc.bodyIdA = colA.bodyId;
				wc.bodyIdB = colB.bodyId;
				wc.contact = info;
				wc.isTrigger = colA.isTrigger || colB.isTrigger;
				contacts.push_back(wc);
			}
		}

		return contacts;
	}

	/// @details PhysicsSystem3D と同じ ContactSolver3D を使い、warm start、クーロン摩擦、位置補正を共有する
	void resolveContacts(const std::vector<WorldContact3D>& contacts, float dt) noexcept
	{
		m_lastContactCount = contacts.size();

		m_lastManifolds.clear();
		for (const auto& wc : contacts)
		{
			if (wc.isTrigger) continue;

			if (wc.manifoldPointCount > 0)
			{
				// box-box は接触点ごとに ContactManifold3D を作る。
				// featureId により warm start を接触点単位で引き継ぐ。
				for (std::size_t i = 0; i < wc.manifoldPointCount; ++i)
				{
					const auto& p = wc.manifoldPoints[i];
					ContactManifold3D manifold;
					manifold.bodyIdA = wc.bodyIdA;
					manifold.bodyIdB = wc.bodyIdB;
					manifold.point = p.point;
					manifold.normal = wc.contact.normal;
					manifold.depth = p.depth;
					manifold.featureId = p.featureId;
					manifold.pairPointCount = static_cast<std::uint32_t>(wc.manifoldPointCount);
					m_lastManifolds.push_back(manifold);
				}
				continue;
			}

			ContactManifold3D manifold;
			manifold.bodyIdA = wc.bodyIdA;
			manifold.bodyIdB = wc.bodyIdB;
			manifold.point = wc.contact.point;
			manifold.normal = wc.contact.normal;
			manifold.depth = wc.contact.depth;
			m_lastManifolds.push_back(manifold);
		}

		// 接触がないフレームも、キャッシュの更新とスリープ判定のために呼ぶ。
		// 省略すると再接触時に古いキャッシュが使われ、非接触ボディがスリープに入れない。
		m_contactSolver.prepare(m_lastManifolds, m_bodies);
		m_contactSolver.warmStart(m_lastManifolds, m_bodies);
		m_contactSolver.solve(m_lastManifolds, m_bodies);
		m_contactSolver.solvePositions(m_lastManifolds, m_bodies);
		m_contactSolver.updateSleepStates(m_lastManifolds, m_bodies, dt);
	}

	void solveConstraints(float dt) noexcept
	{
		for (int iter = 0; iter < m_solverIterations; ++iter)
		{
			for (auto& c : m_distanceConstraints)
			{
				c.solve(dt);
			}

			for (auto& c : m_hingeConstraints)
			{
				c.solve(dt);
			}
		}

		// 速度レベルの拘束解決
		for (auto& c : m_distanceConstraints)
		{
			c.solveVelocity(dt);
		}
	}

	void notifyCallbacks(const std::vector<WorldContact3D>& contacts) noexcept
	{
		for (const auto& wc : contacts)
		{
			for (const auto& cb : m_callbacks)
			{
				cb(wc);
			}
		}
	}

	// ── メンバ変数 ────────────────────────────────────────────

	sgc::Vec3f m_gravity{0.0f, -9.81f, 0.0f};

	std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>> m_bodies;
	std::vector<BodyCollider> m_colliders;
	std::uint64_t m_topologyVersion{0};  ///< addXxxCollider/removeBody（構成変化時のみ）で増加

	std::vector<DistanceConstraint> m_distanceConstraints;
	std::vector<HingeConstraint> m_hingeConstraints;

	std::vector<CollisionCallback3D> m_callbacks;

	ContactSolver3D m_contactSolver;
	std::vector<ContactManifold3D> m_lastManifolds;

	BodyId m_nextBodyId{1};
	int m_solverIterations{4};
	std::size_t m_lastContactCount{0};
};

} // namespace mitiru::physics3d
