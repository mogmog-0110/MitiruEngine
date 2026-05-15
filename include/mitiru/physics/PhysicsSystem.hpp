#pragma once

/// @file PhysicsSystem.hpp
/// @brief ISystem実装による物理システム
///
/// PhysicsBridgeとCollisionBridgeをSystemRunnerに統合するためのシステム。
/// update()内で物理同期→ステップ→書き戻し→衝突検出を一括実行する。
///
/// @code
/// mitiru::physics::PhysicsBridge bridge;
/// mitiru::physics::CollisionBridge collision;
/// bridge.init(mitiru::physics::PhysicsConfig{});
///
/// auto physicsSystem = std::make_unique<mitiru::physics::PhysicsSystem>(bridge, collision);
/// runner.addSystem(std::move(physicsSystem), 100);
/// @endcode

#include <string>

#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SystemRunner.hpp"
#include "mitiru/physics/PhysicsBridge.hpp"
#include "mitiru/physics/CollisionBridge.hpp"

namespace mitiru::physics
{

/// @brief 物理システム（ISystem実装）
///
/// SystemRunnerに登録してPhysicsBridge + CollisionBridgeを
/// 自動的に更新するシステム。
class PhysicsSystem : public scene::ISystem
{
public:
	/// @brief コンストラクタ
	/// @param physicsBridge 物理ブリッジへの参照
	/// @param collisionBridge 衝突検出ブリッジへの参照
	PhysicsSystem(PhysicsBridge& physicsBridge, CollisionBridge& collisionBridge)
		: m_physicsBridge(physicsBridge)
		, m_collisionBridge(collisionBridge)
	{
	}

	/// @brief システム名を返す
	/// @return "PhysicsSystem"
	[[nodiscard]] std::string name() const override
	{
		return "PhysicsSystem";
	}

	/// @brief 物理更新を実行する
	/// @param world ゲームワールド
	/// @param dt デルタタイム（秒）
	///
	/// @details 以下の順序で処理を実行する:
	///   1. syncToPhysics: TransformComponent→内部シミュレーション
	///   2. stepSimulation: 固定ステップ積分
	///   3. syncFromPhysics: 内部シミュレーション→TransformComponent
	///   4. detectCollisions: 衝突検出とコールバック通知
	void update(scene::GameWorld& world, float dt) override
	{
		m_physicsBridge.syncToPhysics(world);
		m_physicsBridge.stepSimulation(dt);
		m_physicsBridge.syncFromPhysics(world);
		static_cast<void>(m_collisionBridge.detectCollisions(world));
	}

private:
	PhysicsBridge& m_physicsBridge;       ///< 物理ブリッジ参照
	CollisionBridge& m_collisionBridge;   ///< 衝突検出ブリッジ参照
};

} // namespace mitiru::physics
