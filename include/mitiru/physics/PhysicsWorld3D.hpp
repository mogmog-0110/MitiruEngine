#pragma once

/// @file PhysicsWorld3D.hpp
/// @brief 3D物理ワールド
///
/// 剛体の管理、ブロードフェーズ（sweep-and-prune）、ナローフェーズ衝突検出、
/// インパルスベースの接触解決、拘束ソルバーを統合した物理シミュレーション。
///
/// @code
/// mitiru::physics3d::PhysicsWorld3D world;
/// world.setGravity({0, -9.81f, 0});
///
/// auto& body = world.addBody();
/// body.setMass(1.0f);
/// body.setPosition({0, 10, 0});
/// world.addSphereCollider(body.id(), {{0, 10, 0}, 0.5f});
///
/// world.step(1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/physics/Collider3D.hpp"
#include "mitiru/physics/CollisionDetection3D.hpp"
#include "mitiru/physics/RigidBody3D.hpp"
#include "mitiru/physics/Constraints3D.hpp"

namespace mitiru::physics3d
{

/// @brief コライダータイプ識別
enum class ColliderType3D
{
	Sphere,
	AABB,
	Capsule
};

/// @brief ボディに紐づくコライダー
struct BodyCollider
{
	BodyId bodyId{INVALID_BODY_ID};
	ColliderType3D type{ColliderType3D::Sphere};
	SphereCollider sphere{};
	AABBCollider3D aabb{};
	CapsuleCollider capsule{};
};

/// @brief 衝突コンタクト情報（ワールドレベル）
struct WorldContact3D
{
	BodyId bodyIdA{INVALID_BODY_ID};
	BodyId bodyIdB{INVALID_BODY_ID};
	ContactInfo3D contact{};
};

/// @brief 衝突コールバック型
using CollisionCallback3D = std::function<void(const WorldContact3D&)>;

/// @brief ブロードフェーズのAABBプロキシ
struct BroadphaseProxy
{
	BodyId bodyId{INVALID_BODY_ID};
	AABBCollider3D aabb{};

	/// @brief ソート用: X軸の最小値
	[[nodiscard]] constexpr float minX() const noexcept { return aabb.min.x; }
};

/// @brief 3D物理ワールド
///
/// 全体的な物理シミュレーションを管理する。
/// ブロードフェーズ→ナローフェーズ→接触解決→拘束ソルバーの順で処理。
class PhysicsWorld3D
{
public:
	/// @brief デフォルトコンストラクタ
	PhysicsWorld3D() = default;

	// ── 重力 ──────────────────────────────────────────────────

	/// @brief 重力を設定する
	/// @param gravity 重力加速度（m/s^2）
	void setGravity(const sgc::Vec3f& gravity) noexcept { m_gravity = gravity; }

	/// @brief 重力を取得する
	[[nodiscard]] constexpr const sgc::Vec3f& gravity() const noexcept { return m_gravity; }

	// ── ボディ管理 ────────────────────────────────────────────

	/// @brief 剛体を追加する
	/// @return 追加した剛体への参照
	RigidBody3D& addBody() noexcept
	{
		const BodyId id = m_nextBodyId++;
		auto body = std::make_unique<RigidBody3D>();
		body->setId(id);
		auto& ref = *body;
		m_bodies[id] = std::move(body);
		return ref;
	}

	/// @brief 剛体を削除する
	/// @param id 削除するボディID
	void removeBody(BodyId id) noexcept
	{
		m_bodies.erase(id);

		// 関連コライダーも削除
		m_colliders.erase(
			std::remove_if(m_colliders.begin(), m_colliders.end(),
				[id](const BodyCollider& c) { return c.bodyId == id; }),
			m_colliders.end());
	}

	/// @brief ボディを取得する
	/// @param id ボディID
	/// @return ボディへのポインタ（見つからない場合はnullptr）
	[[nodiscard]] RigidBody3D* getBody(BodyId id) noexcept
	{
		auto it = m_bodies.find(id);
		return (it != m_bodies.end()) ? it->second.get() : nullptr;
	}

	/// @brief ボディを取得する（const版）
	/// @param id ボディID
	/// @return ボディへのポインタ（見つからない場合はnullptr）
	[[nodiscard]] const RigidBody3D* getBody(BodyId id) const noexcept
	{
		auto it = m_bodies.find(id);
		return (it != m_bodies.end()) ? it->second.get() : nullptr;
	}

	/// @brief ボディ数を返す
	[[nodiscard]] std::size_t bodyCount() const noexcept { return m_bodies.size(); }

	// ── コライダー管理 ────────────────────────────────────────

	/// @brief 球コライダーを追加する
	/// @param bodyId 紐づくボディID
	/// @param sphere 球コライダー
	void addSphereCollider(BodyId bodyId, const SphereCollider& sphere) noexcept
	{
		BodyCollider bc;
		bc.bodyId = bodyId;
		bc.type = ColliderType3D::Sphere;
		bc.sphere = sphere;
		m_colliders.push_back(bc);
	}

	/// @brief AABBコライダーを追加する
	/// @param bodyId 紐づくボディID
	/// @param aabb AABBコライダー
	void addAABBCollider(BodyId bodyId, const AABBCollider3D& aabb) noexcept
	{
		BodyCollider bc;
		bc.bodyId = bodyId;
		bc.type = ColliderType3D::AABB;
		bc.aabb = aabb;
		m_colliders.push_back(bc);
	}

	/// @brief カプセルコライダーを追加する
	/// @param bodyId 紐づくボディID
	/// @param capsule カプセルコライダー
	void addCapsuleCollider(BodyId bodyId, const CapsuleCollider& capsule) noexcept
	{
		BodyCollider bc;
		bc.bodyId = bodyId;
		bc.type = ColliderType3D::Capsule;
		bc.capsule = capsule;
		m_colliders.push_back(bc);
	}

	/// @brief コライダー数を返す
	[[nodiscard]] std::size_t colliderCount() const noexcept { return m_colliders.size(); }

	// ── 拘束 ──────────────────────────────────────────────────

	/// @brief 距離拘束を追加する
	/// @param constraint 距離拘束
	void addDistanceConstraint(DistanceConstraint constraint) noexcept
	{
		m_distanceConstraints.push_back(std::move(constraint));
	}

	/// @brief ヒンジ拘束を追加する
	/// @param constraint ヒンジ拘束
	void addHingeConstraint(HingeConstraint constraint) noexcept
	{
		m_hingeConstraints.push_back(std::move(constraint));
	}

	/// @brief 拘束数を返す
	[[nodiscard]] std::size_t constraintCount() const noexcept
	{
		return m_distanceConstraints.size() + m_hingeConstraints.size();
	}

	// ── コールバック ──────────────────────────────────────────

	/// @brief 衝突コールバックを登録する
	/// @param callback 衝突発生時に呼ばれるコールバック
	void registerCollisionCallback(CollisionCallback3D callback) noexcept
	{
		m_callbacks.push_back(std::move(callback));
	}

	// ── シミュレーション ──────────────────────────────────────

	/// @brief ソルバー反復回数を設定する
	/// @param iterations 反復回数
	void setSolverIterations(int iterations) noexcept { m_solverIterations = iterations; }

	/// @brief シミュレーションを1ステップ進める
	/// @param dt タイムステップ（秒）
	void step(float dt) noexcept
	{
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

		// 6. 接触解決（インパルスベース）
		resolveContacts(contacts);

		// 7. 拘束ソルバー
		solveConstraints(dt);

		// 8. コールバック通知
		notifyCallbacks(contacts);
	}

	/// @brief 最後のステップで検出されたコンタクト数を返す
	[[nodiscard]] std::size_t lastContactCount() const noexcept { return m_lastContactCount; }

	/// @brief ボディマップへの参照を取得する（システム統合用）
	[[nodiscard]] std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies() noexcept
	{
		return m_bodies;
	}

	/// @brief ボディマップへのconst参照を取得する
	[[nodiscard]] const std::unordered_map<BodyId, std::unique_ptr<RigidBody3D>>& bodies() const noexcept
	{
		return m_bodies;
	}

	/// @brief コライダー配列への参照を取得する（システム統合用）
	[[nodiscard]] std::vector<BodyCollider>& colliders() noexcept
	{
		return m_colliders;
	}

	/// @brief コライダー配列へのconst参照を取得する
	[[nodiscard]] const std::vector<BodyCollider>& colliders() const noexcept
	{
		return m_colliders;
	}

	/// @brief コライダーの包含AABBを計算する（公開ヘルパー）
	/// @param collider コライダー
	/// @return 包含AABB
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

	/// @brief ワールド内でレイキャストを実行する
	/// @param ray レイ
	/// @param maxDist 最大検出距離
	/// @return 最も近いヒット結果
	[[nodiscard]] std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist = 1e6f) const noexcept
	{
		std::optional<RayHit3D> closest;

		for (const auto& collider : m_colliders)
		{
			std::optional<RayHit3D> hit;

			switch (collider.type)
			{
			case ColliderType3D::Sphere:
				hit = raycastSphere(ray, collider.sphere, maxDist);
				break;
			case ColliderType3D::AABB:
				hit = raycastAABB(ray, collider.aabb, maxDist);
				break;
			default:
				break;
			}

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

private:
	/// @brief 全動的ボディに重力を適用する
	void applyGravity() noexcept
	{
		for (auto& [id, body] : m_bodies)
		{
			if (!body->isStatic())
			{
				body->applyForce(m_gravity * body->mass());
			}
		}
	}

	/// @brief コライダー位置をボディに同期する
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

	/// @brief ブロードフェーズ（sweep-and-prune）
	/// @return 衝突の可能性があるコライダーペアのインデックス
	[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> broadphase() const noexcept
	{
		if (m_colliders.size() < 2) return {};

		// AABBプロキシを構築
		std::vector<BroadphaseProxy> proxies;
		proxies.reserve(m_colliders.size());

		for (std::size_t i = 0; i < m_colliders.size(); ++i)
		{
			BroadphaseProxy proxy;
			proxy.bodyId = static_cast<BodyId>(i);
			proxy.aabb = computeAABB(m_colliders[i]);
			proxies.push_back(proxy);
		}

		// X軸でソート
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
				// X軸で離れていたら後続もスキップ
				if (proxies[j].aabb.min.x > proxies[i].aabb.max.x) break;

				// Y軸とZ軸でも重なりを確認
				if (proxies[i].aabb.max.y < proxies[j].aabb.min.y ||
					proxies[i].aabb.min.y > proxies[j].aabb.max.y) continue;

				if (proxies[i].aabb.max.z < proxies[j].aabb.min.z ||
					proxies[i].aabb.min.z > proxies[j].aabb.max.z) continue;

				// 同一ボディは除外
				if (m_colliders[proxies[i].bodyId].bodyId ==
					m_colliders[proxies[j].bodyId].bodyId) continue;

				pairs.emplace_back(proxies[i].bodyId, proxies[j].bodyId);
			}
		}

		return pairs;
	}

	/// @brief ナローフェーズ衝突検出
	/// @param pairs ブロードフェーズで検出されたペア
	/// @return 接触情報リスト
	[[nodiscard]] std::vector<WorldContact3D> narrowphase(
		const std::vector<std::pair<std::size_t, std::size_t>>& pairs) const noexcept
	{
		std::vector<WorldContact3D> contacts;

		for (const auto& [idxA, idxB] : pairs)
		{
			const auto& colA = m_colliders[idxA];
			const auto& colB = m_colliders[idxB];

			ContactInfo3D info;

			// 形状ペアに応じたナローフェーズ検出
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
			else if (colA.type == ColliderType3D::AABB && colB.type == ColliderType3D::AABB)
			{
				info = testAABBAABB(colA.aabb, colB.aabb);
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

			if (info.hasContact)
			{
				WorldContact3D wc;
				wc.bodyIdA = colA.bodyId;
				wc.bodyIdB = colB.bodyId;
				wc.contact = info;
				contacts.push_back(wc);
			}
		}

		return contacts;
	}

	/// @brief インパルスベースの接触解決
	/// @param contacts 接触情報リスト
	void resolveContacts(const std::vector<WorldContact3D>& contacts) noexcept
	{
		m_lastContactCount = contacts.size();

		for (int iter = 0; iter < m_solverIterations; ++iter)
		{
			for (const auto& wc : contacts)
			{
				auto* bodyA = getBody(wc.bodyIdA);
				auto* bodyB = getBody(wc.bodyIdB);

				if (!bodyA || !bodyB) continue;

				const float invMassA = bodyA->inverseMass();
				const float invMassB = bodyB->inverseMass();
				const float totalInvMass = invMassA + invMassB;

				if (totalInvMass < 1e-8f) continue;

				const sgc::Vec3f& normal = wc.contact.normal;

				// 相対速度
				const sgc::Vec3f relVel = bodyB->linearVelocity() - bodyA->linearVelocity();
				const float velAlongNormal = relVel.dot(normal);

				// 離れていく場合はスキップ
				if (velAlongNormal > 0.0f) continue;

				// 反発係数（最小値を使用）
				const float restitution = std::min(bodyA->restitution(), bodyB->restitution());

				// インパルス計算
				const float j = -(1.0f + restitution) * velAlongNormal / totalInvMass;
				const sgc::Vec3f impulse = normal * j;

				// インパルス適用
				if (!bodyA->isStatic())
				{
					bodyA->applyImpulse(-impulse);
				}
				if (!bodyB->isStatic())
				{
					bodyB->applyImpulse(impulse);
				}

				// 位置補正（sinking防止）
				const float slop = 0.01f;
				const float percent = 0.2f;
				const float correctionMag = std::max(wc.contact.depth - slop, 0.0f)
					/ totalInvMass * percent;
				const sgc::Vec3f correction = normal * correctionMag;

				if (!bodyA->isStatic())
				{
					bodyA->setPosition(bodyA->position() - correction * invMassA);
				}
				if (!bodyB->isStatic())
				{
					bodyB->setPosition(bodyB->position() + correction * invMassB);
				}
			}
		}
	}

	/// @brief 拘束ソルバーを実行する
	/// @param dt タイムステップ
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

	/// @brief コールバックに通知する
	/// @param contacts 接触情報リスト
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

	std::vector<DistanceConstraint> m_distanceConstraints;
	std::vector<HingeConstraint> m_hingeConstraints;

	std::vector<CollisionCallback3D> m_callbacks;

	BodyId m_nextBodyId{1};
	int m_solverIterations{4};
	std::size_t m_lastContactCount{0};
};

} // namespace mitiru::physics3d
