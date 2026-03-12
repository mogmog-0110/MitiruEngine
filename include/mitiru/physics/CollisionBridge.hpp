#pragma once

/// @file CollisionBridge.hpp
/// @brief 衝突検出ブリッジ
///
/// GameWorld内のRigidBodyComponentを持つエンティティ間の
/// 衝突検出を提供する。球-球およびAABB-AABBの簡易検出をサポート。
///
/// @code
/// mitiru::physics::CollisionBridge collision;
/// collision.registerCallback([](const auto& pair) {
///     // 衝突処理
/// });
/// auto pairs = collision.detectCollisions(world);
/// @endcode

#include <cmath>
#include <functional>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/physics/PhysicsBridge.hpp"

namespace mitiru::physics
{

/// @brief 衝突ペア情報
struct CollisionPair
{
	scene::EntityId entityA{scene::INVALID_ENTITY};  ///< エンティティA
	scene::EntityId entityB{scene::INVALID_ENTITY};  ///< エンティティB
	sgc::Vec3f contactPoint{};                        ///< 接触点
	sgc::Vec3f normal{};                              ///< 衝突法線（AからBへの方向）
	float penetration{0.0f};                          ///< 貫通深度
};

/// @brief 衝突コールバック型
using CollisionCallback = std::function<void(const CollisionPair&)>;

/// @brief 衝突検出ブリッジ
///
/// GameWorld内のRigidBodyComponentを持つエンティティペアに対して
/// 球-球およびAABB-AABB衝突検出を実行する。
class CollisionBridge
{
public:
	/// @brief 衝突コールバックを登録する
	/// @param callback 衝突発生時に呼ばれるコールバック
	void registerCallback(CollisionCallback callback)
	{
		m_callbacks.push_back(std::move(callback));
	}

	/// @brief 登録済みコールバック数を返す
	/// @return コールバック数
	[[nodiscard]] std::size_t callbackCount() const noexcept
	{
		return m_callbacks.size();
	}

	/// @brief GameWorld内の全RigidBodyComponentペアで衝突検出を実行する
	/// @param world ゲームワールド
	/// @return 検出された衝突ペアのリスト
	[[nodiscard]] std::vector<CollisionPair> detectCollisions(scene::GameWorld& world)
	{
		// RigidBodyComponent を持つ全エンティティを収集
		std::vector<EntityInfo> entities;
		world.forEach<RigidBodyComponent>(
			[&entities, &world](scene::EntityId id, RigidBodyComponent& rb)
			{
				auto* transform = world.getComponent<scene::TransformComponent>(id);
				if (!transform) return;
				entities.push_back({id, *transform, rb});
			}
		);

		std::vector<CollisionPair> result;

		// 全ペア総当たり検出
		for (std::size_t i = 0; i < entities.size(); ++i)
		{
			for (std::size_t j = i + 1; j < entities.size(); ++j)
			{
				auto pair = testCollision(entities[i], entities[j]);
				if (pair.has_value())
				{
					result.push_back(*pair);

					// 登録済みコールバックに通知
					for (const auto& cb : m_callbacks)
					{
						cb(*pair);
					}
				}
			}
		}

		return result;
	}

private:
	/// @brief エンティティ情報（衝突検出用）
	struct EntityInfo
	{
		scene::EntityId id;
		scene::TransformComponent transform;
		RigidBodyComponent rigidBody;
	};

	/// @brief 2エンティティ間の衝突判定を行う
	/// @param a エンティティA
	/// @param b エンティティB
	/// @return 衝突ペア（衝突なしの場合はnullopt）
	[[nodiscard]] static std::optional<CollisionPair> testCollision(
		const EntityInfo& a, const EntityInfo& b)
	{
		// トリガー同士は衝突しない
		if (a.rigidBody.isTrigger && b.rigidBody.isTrigger) return std::nullopt;

		// 形状の組み合わせに応じて検出
		if (a.rigidBody.shape.type == ColliderShapeType::Sphere &&
			b.rigidBody.shape.type == ColliderShapeType::Sphere)
		{
			return testSphereSphere(a, b);
		}

		if (a.rigidBody.shape.type == ColliderShapeType::Box &&
			b.rigidBody.shape.type == ColliderShapeType::Box)
		{
			return testAABBAABB(a, b);
		}

		// 未対応の組み合わせ
		return std::nullopt;
	}

	/// @brief 球-球衝突検出
	/// @param a エンティティA（球）
	/// @param b エンティティB（球）
	/// @return 衝突ペア（衝突なしの場合はnullopt）
	[[nodiscard]] static std::optional<CollisionPair> testSphereSphere(
		const EntityInfo& a, const EntityInfo& b)
	{
		const auto& sphereA = std::get<SphereData>(a.rigidBody.shape.data);
		const auto& sphereB = std::get<SphereData>(b.rigidBody.shape.data);

		const sgc::Vec3f diff = b.transform.position - a.transform.position;
		const float distSq = diff.lengthSquared();
		const float radiusSum = sphereA.radius + sphereB.radius;

		if (distSq >= radiusSum * radiusSum) return std::nullopt;

		const float dist = std::sqrt(distSq);
		const float penetration = radiusSum - dist;

		sgc::Vec3f normal;
		if (dist > 0.0001f)
		{
			normal = diff / dist;
		}
		else
		{
			// 完全重複時はY軸方向をデフォルトにする
			normal = sgc::Vec3f{0.0f, 1.0f, 0.0f};
		}

		// 接触点は2球の間の中点
		const sgc::Vec3f contactPoint =
			a.transform.position + normal * (sphereA.radius - penetration * 0.5f);

		CollisionPair pair;
		pair.entityA = a.id;
		pair.entityB = b.id;
		pair.contactPoint = contactPoint;
		pair.normal = normal;
		pair.penetration = penetration;
		return pair;
	}

	/// @brief AABB-AABB衝突検出
	/// @param a エンティティA（ボックス）
	/// @param b エンティティB（ボックス）
	/// @return 衝突ペア（衝突なしの場合はnullopt）
	[[nodiscard]] static std::optional<CollisionPair> testAABBAABB(
		const EntityInfo& a, const EntityInfo& b)
	{
		const auto& boxA = std::get<BoxData>(a.rigidBody.shape.data);
		const auto& boxB = std::get<BoxData>(b.rigidBody.shape.data);

		const sgc::Vec3f& posA = a.transform.position;
		const sgc::Vec3f& posB = b.transform.position;

		// 各軸の重なりを計算
		const float overlapX = (boxA.halfExtents.x + boxB.halfExtents.x)
			- std::abs(posB.x - posA.x);
		const float overlapY = (boxA.halfExtents.y + boxB.halfExtents.y)
			- std::abs(posB.y - posA.y);
		const float overlapZ = (boxA.halfExtents.z + boxB.halfExtents.z)
			- std::abs(posB.z - posA.z);

		// いずれかの軸で重なりがなければ衝突なし
		if (overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f)
		{
			return std::nullopt;
		}

		// 最小重なり軸を衝突法線とする
		sgc::Vec3f normal;
		float penetration;

		if (overlapX <= overlapY && overlapX <= overlapZ)
		{
			penetration = overlapX;
			normal = sgc::Vec3f{(posB.x > posA.x) ? 1.0f : -1.0f, 0.0f, 0.0f};
		}
		else if (overlapY <= overlapZ)
		{
			penetration = overlapY;
			normal = sgc::Vec3f{0.0f, (posB.y > posA.y) ? 1.0f : -1.0f, 0.0f};
		}
		else
		{
			penetration = overlapZ;
			normal = sgc::Vec3f{0.0f, 0.0f, (posB.z > posA.z) ? 1.0f : -1.0f};
		}

		// 接触点はAABB重なり領域の中心
		const sgc::Vec3f contactPoint{
			(posA.x + posB.x) * 0.5f,
			(posA.y + posB.y) * 0.5f,
			(posA.z + posB.z) * 0.5f
		};

		CollisionPair pair;
		pair.entityA = a.id;
		pair.entityB = b.id;
		pair.contactPoint = contactPoint;
		pair.normal = normal;
		pair.penetration = penetration;
		return pair;
	}

	/// @brief 登録済み衝突コールバック
	std::vector<CollisionCallback> m_callbacks;
};

} // namespace mitiru::physics
