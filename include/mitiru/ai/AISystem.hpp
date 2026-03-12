#pragma once

/// @file AISystem.hpp
/// @brief AIシステム（ISystem実装）
///
/// BehaviorBridgeをラップし、SystemRunnerに登録可能なISystem実装を提供する。
/// updateAll()から自動的にビヘイビアツリーとFSMのティック処理が実行される。
///
/// @code
/// mitiru::ai::BehaviorBridge bridge;
/// mitiru::scene::SystemRunner runner;
/// runner.addSystem(std::make_unique<mitiru::ai::AISystem>(bridge), 10);
/// runner.updateAll(world, dt);
/// @endcode

#include <string>

#include "mitiru/ai/BehaviorBridge.hpp"
#include "mitiru/scene/SystemRunner.hpp"

namespace mitiru::ai
{

/// @brief AIシステム — BehaviorBridgeをISystemとして公開する
class AISystem : public scene::ISystem
{
public:
	/// @brief コンストラクタ
	/// @param bridge ビヘイビアブリッジへの参照
	explicit AISystem(BehaviorBridge& bridge)
		: m_bridge(bridge)
	{
	}

	/// @brief システム名を返す
	/// @return "AISystem"
	[[nodiscard]] std::string name() const override
	{
		return "AISystem";
	}

	/// @brief システムを更新する
	/// @param world ゲームワールド
	/// @param dt デルタタイム（秒）
	void update(scene::GameWorld& world, float dt) override
	{
		m_bridge.tickBehaviorTrees(world, dt);
		m_bridge.tickFSMs(world, dt);
	}

private:
	BehaviorBridge& m_bridge;  ///< ビヘイビアブリッジ参照
};

} // namespace mitiru::ai
