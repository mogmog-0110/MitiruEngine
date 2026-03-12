#pragma once

/// @file BehaviorBridge.hpp
/// @brief sgc AIモジュールとmitiru GameWorldを接続するブリッジ
///
/// ビヘイビアツリーとFSMをエンティティコンポーネントとして管理し、
/// GameWorldのforEachで自動的にティック処理を行う。
///
/// @code
/// mitiru::scene::GameWorld world;
/// mitiru::ai::BehaviorBridge bridge;
/// bridge.registerBehaviorTree("patrol", R"({"type":"sequence"})");
/// bridge.registerFSM("enemy_fsm", R"({"states":["idle","chase"]})");
///
/// auto eid = world.createEntity("Enemy");
/// world.addComponent(eid, mitiru::ai::BehaviorTreeComponent{"patrol"});
/// bridge.tickBehaviorTrees(world, 0.016f);
/// @endcode

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "mitiru/scene/GameWorld.hpp"

namespace mitiru::ai
{

/// @brief ビヘイビアツリーのノード状態
enum class NodeStatus
{
	Success,  ///< 成功
	Failure,  ///< 失敗
	Running   ///< 実行中
};

/// @brief ビヘイビアツリーコンポーネント
/// @details エンティティに付与してAI行動ツリーを駆動する
struct BehaviorTreeComponent
{
	std::string treeId;                        ///< 登録済みツリーID
	std::map<std::string, float> blackboard;   ///< ブラックボード（キー→値）
	float tickRate{0.1f};                      ///< ティック間隔（秒）
	float lastTickTime{0.0f};                  ///< 最後のティック時刻
};

/// @brief FSMコンポーネント
/// @details エンティティに付与して有限状態マシンを駆動する
struct FSMComponent
{
	std::string fsmId;                              ///< 登録済みFSM ID
	std::string currentState;                       ///< 現在のステート名
	std::map<std::string, std::string> stateData;   ///< ステートごとのデータ
};

/// @brief AIエージェント統合コンポーネント
/// @details ビヘイビアツリー・FSM・ユーティリティスコアを統合する
struct AIAgentComponent
{
	std::string behaviorTreeId;                     ///< 使用するビヘイビアツリーID
	std::string fsmId;                              ///< 使用するFSM ID
	std::map<std::string, float> utilityScores;     ///< ユーティリティスコア
};

/// @brief ビヘイビアツリーの定義（簡易構成）
struct BehaviorTreeDef
{
	std::string id;       ///< ツリーID
	std::string config;   ///< JSON形式の構成文字列
};

/// @brief FSMの遷移定義
struct FSMTransition
{
	std::string fromState;   ///< 遷移元ステート
	std::string toState;     ///< 遷移先ステート
	std::string condition;   ///< 遷移条件文字列
};

/// @brief FSMの定義
struct FSMDef
{
	std::string id;                       ///< FSM ID
	std::string config;                   ///< JSON形式の構成文字列
	std::vector<std::string> states;      ///< ステート一覧
	std::vector<FSMTransition> transitions;  ///< 遷移一覧
};

/// @brief sgc AIとmitiru GameWorldを接続するブリッジ
class BehaviorBridge
{
public:
	/// @brief ビヘイビアツリーを登録する
	/// @param id ツリーID
	/// @param configJson JSON形式の構成文字列
	void registerBehaviorTree(const std::string& id, const std::string& configJson)
	{
		BehaviorTreeDef def;
		def.id = id;
		def.config = configJson;
		m_trees[id] = std::move(def);
	}

	/// @brief FSMを登録する
	/// @param id FSM ID
	/// @param configJson JSON形式の構成文字列（states/transitions）
	void registerFSM(const std::string& id, const std::string& configJson)
	{
		FSMDef def;
		def.id = id;
		def.config = configJson;
		m_fsms[id] = std::move(def);
	}

	/// @brief 登録済みビヘイビアツリー数を返す
	/// @return ツリー数
	[[nodiscard]] size_t treeCount() const noexcept
	{
		return m_trees.size();
	}

	/// @brief 登録済みFSM数を返す
	/// @return FSM数
	[[nodiscard]] size_t fsmCount() const noexcept
	{
		return m_fsms.size();
	}

	/// @brief ビヘイビアツリーコンポーネントを持つ全エンティティをティックする
	/// @param world ゲームワールド
	/// @param dt デルタタイム（秒）
	void tickBehaviorTrees(scene::GameWorld& world, float dt)
	{
		world.forEach<BehaviorTreeComponent>(
			[this, dt](scene::EntityId id, BehaviorTreeComponent& btc)
			{
				btc.lastTickTime += dt;
				if (btc.lastTickTime >= btc.tickRate)
				{
					btc.lastTickTime -= btc.tickRate;
					executeBehaviorTree(id, btc);
				}
			}
		);
		++m_btTickCount;
	}

	/// @brief FSMコンポーネントを持つ全エンティティをティックする
	/// @param world ゲームワールド
	/// @param dt デルタタイム（秒）
	void tickFSMs(scene::GameWorld& world, float dt)
	{
		world.forEach<FSMComponent>(
			[this, dt](scene::EntityId id, FSMComponent& fsm)
			{
				(void)dt;
				executeFSM(id, fsm);
			}
		);
		++m_fsmTickCount;
	}

	/// @brief エンティティのブラックボード値を設定する
	/// @param world ゲームワールド
	/// @param entityId エンティティID
	/// @param key キー名
	/// @param value 値
	void setBlackboardValue(scene::GameWorld& world, scene::EntityId entityId,
		const std::string& key, float value)
	{
		auto* btc = world.getComponent<BehaviorTreeComponent>(entityId);
		if (btc)
		{
			btc->blackboard[key] = value;
		}
	}

	/// @brief エンティティのブラックボード値を取得する
	/// @param world ゲームワールド
	/// @param entityId エンティティID
	/// @param key キー名
	/// @return 値（存在しない場合はnullopt）
	[[nodiscard]] std::optional<float> getBlackboardValue(
		const scene::GameWorld& world, scene::EntityId entityId,
		const std::string& key) const
	{
		const auto* btc = world.getComponent<BehaviorTreeComponent>(entityId);
		if (!btc) return std::nullopt;
		auto it = btc->blackboard.find(key);
		if (it == btc->blackboard.end()) return std::nullopt;
		return it->second;
	}

	/// @brief BTティック回数を返す（デバッグ用）
	/// @return ティック回数
	[[nodiscard]] uint32_t btTickCount() const noexcept
	{
		return m_btTickCount;
	}

	/// @brief FSMティック回数を返す（デバッグ用）
	/// @return ティック回数
	[[nodiscard]] uint32_t fsmTickCount() const noexcept
	{
		return m_fsmTickCount;
	}

private:
	/// @brief ビヘイビアツリーを実行する（内部）
	void executeBehaviorTree(scene::EntityId id, BehaviorTreeComponent& btc)
	{
		(void)id;
		// 登録済みツリーが存在すれば処理（簡易実装）
		auto it = m_trees.find(btc.treeId);
		if (it == m_trees.end()) return;

		// ブラックボードの "status" を更新してツリー実行を示す
		btc.blackboard["_lastTick"] = 1.0f;
	}

	/// @brief FSMを実行する（内部）
	void executeFSM(scene::EntityId id, FSMComponent& fsm)
	{
		(void)id;
		auto it = m_fsms.find(fsm.fsmId);
		if (it == m_fsms.end()) return;

		// ステートデータに実行マークを付与
		fsm.stateData["_lastTick"] = "1";
	}

	std::unordered_map<std::string, BehaviorTreeDef> m_trees;  ///< 登録済みツリー
	std::unordered_map<std::string, FSMDef> m_fsms;            ///< 登録済みFSM
	uint32_t m_btTickCount{0};    ///< BTティック回数
	uint32_t m_fsmTickCount{0};   ///< FSMティック回数
};

} // namespace mitiru::ai
