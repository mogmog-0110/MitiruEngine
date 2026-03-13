#pragma once

/// @file PhysicsIntegration.hpp
/// @brief GraphWalker物理統合レイヤー
///
/// sgc::physics::Collider2Dを活用したAABB/円衝突検出と
/// プレイヤー・オブジェクト間の衝突解決を提供する。
///
/// @code
/// mitiru::gw::GWPhysicsWorld physics;
/// physics.addObject(&player);
/// physics.addObject(&platform);
/// physics.step(dt);
/// @endcode

#include <algorithm>
#include <cmath>
#include <vector>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Rect.hpp"
#include "sgc/physics/Collider2D.hpp"
#include "mitiru/graphwalker/GameObject.hpp"
#include "mitiru/graphwalker/Player.hpp"

namespace mitiru::gw
{

/// @brief 衝突結果
struct CollisionResult
{
	bool collided{false};          ///< 衝突したか
	sgc::Vec2f normal{};           ///< 衝突法線（AからBへの方向）
	float penetration{0.0f};       ///< めり込み深さ
	ObjectType hitType{};          ///< 衝突相手のオブジェクト種別
};

/// @brief GraphWalker物理ワールド
///
/// 登録されたオブジェクト間のAABB衝突検出と
/// プレイヤーとの衝突解決を担当する。
/// ワンウェイ衝突（上方移動中はスキップ）をサポート。
class GWPhysicsWorld
{
public:
	/// @brief 物理ワールドを構築する
	/// @param gravity 重力加速度（ピクセル/秒^2）
	/// @param pixelsPerUnit ピクセル/ユニット比率
	explicit GWPhysicsWorld(float gravity = 980.0f, float pixelsPerUnit = 60.0f)
		: m_gravity(gravity), m_pixelsPerUnit(pixelsPerUnit) {}

	/// @brief オブジェクトを物理ワールドに登録する
	/// @param obj 登録するオブジェクト（非所有ポインタ）
	void addObject(IGameObject* obj)
	{
		if (obj != nullptr)
		{
			m_objects.push_back(obj);
		}
	}

	/// @brief オブジェクトを物理ワールドから除去する
	/// @param obj 除去するオブジェクト
	void removeObject(IGameObject* obj)
	{
		m_objects.erase(
			std::remove(m_objects.begin(), m_objects.end(), obj),
			m_objects.end()
		);
	}

	/// @brief 物理ステップ実行（衝突検出のみ）
	/// @param dt デルタタイム（秒）
	///
	/// 全オブジェクトの更新後に呼び出すことを想定。
	/// 実際の衝突解決はresolveCollision()で個別に行う。
	void step(float /*dt*/)
	{
		// ステップ処理は衝突ペア検出の準備のみ
		// 実際のゲームループで個別にresolveCollision()を呼ぶ
	}

	/// @brief AABB同士の衝突判定
	/// @param a 矩形A
	/// @param b 矩形B
	/// @return 重なっていればtrue
	[[nodiscard]] static bool checkCollision(const sgc::Rectf& a, const sgc::Rectf& b)
	{
		const sgc::physics::AABBCollider aabbA{
			{a.x(), a.y()},
			{a.x() + a.width(), a.y() + a.height()}
		};
		const sgc::physics::AABBCollider aabbB{
			{b.x(), b.y()},
			{b.x() + b.width(), b.y() + b.height()}
		};

		return sgc::physics::testAABBAABB(aabbA, aabbB).has_value();
	}

	/// @brief プレイヤーとオブジェクトの衝突を検出・解決する
	/// @param player プレイヤー
	/// @param obj 衝突対象オブジェクト
	/// @return 衝突結果
	[[nodiscard]] static CollisionResult resolveCollision(
		const Player& player, const IGameObject& obj)
	{
		CollisionResult result;

		if (!obj.isActive())
		{
			return result;
		}

		const auto playerBounds = player.getBounds();
		const auto objBounds = obj.getBounds();

		// sgc::physics::AABBColliderに変換して衝突テスト
		const sgc::physics::AABBCollider playerAABB{
			{playerBounds.x(), playerBounds.y()},
			{playerBounds.x() + playerBounds.width(),
			 playerBounds.y() + playerBounds.height()}
		};
		const sgc::physics::AABBCollider objAABB{
			{objBounds.x(), objBounds.y()},
			{objBounds.x() + objBounds.width(),
			 objBounds.y() + objBounds.height()}
		};

		const auto contact = sgc::physics::testAABBAABB(playerAABB, objAABB);
		if (!contact.has_value())
		{
			return result;
		}

		// ワンウェイ衝突：プレイヤーが上方移動中はプラットフォーム衝突をスキップ
		const auto type = obj.getType();
		const bool isPlatform =
			(type == ObjectType::StaticPlatform ||
			 type == ObjectType::MovingPlatform ||
			 type == ObjectType::CrumblingPlatform ||
			 type == ObjectType::SpringPlatform);

		if (isPlatform && player.getVelocity().y < 0.0f)
		{
			return result;
		}

		result.collided = true;
		result.normal = contact->normal;
		result.penetration = contact->penetration;
		result.hitType = type;

		return result;
	}

	/// @brief プレイヤーが何かの上に乗っているか判定する
	/// @param player プレイヤー
	/// @return 接地していればtrue
	[[nodiscard]] bool isGrounded(const Player& player) const
	{
		const auto playerBounds = player.getBounds();

		// プレイヤーの足元に細い矩形を配置して接地判定
		const sgc::Rectf footSensor{
			playerBounds.x() + 2.0f,
			playerBounds.y() + playerBounds.height(),
			playerBounds.width() - 4.0f,
			2.0f
		};

		for (const auto* obj : m_objects)
		{
			if (obj == nullptr || !obj->isActive())
			{
				continue;
			}

			const auto type = obj->getType();
			if (type == ObjectType::Player)
			{
				continue;
			}

			const bool isSolid =
				(type == ObjectType::StaticPlatform ||
				 type == ObjectType::MovingPlatform ||
				 type == ObjectType::CrumblingPlatform ||
				 type == ObjectType::SpringPlatform);

			if (!isSolid)
			{
				continue;
			}

			if (checkCollision(footSensor, obj->getBounds()))
			{
				return true;
			}
		}

		return false;
	}

	/// @brief 重力値を取得する
	[[nodiscard]] float getGravity() const { return m_gravity; }
	/// @brief ピクセル/ユニット比率を取得する
	[[nodiscard]] float getPixelsPerUnit() const { return m_pixelsPerUnit; }
	/// @brief 登録オブジェクト数を取得する
	[[nodiscard]] std::size_t getObjectCount() const { return m_objects.size(); }

private:
	std::vector<IGameObject*> m_objects;  ///< 登録オブジェクト（非所有）
	float m_gravity;                       ///< 重力加速度
	float m_pixelsPerUnit;                 ///< ピクセル/ユニット比率
};

} // namespace mitiru::gw
