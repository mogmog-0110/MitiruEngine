#pragma once

/// @file AiBridge.hpp
/// @brief sgc AI統合ブリッジ
/// @details sgcのBehaviorTree、UtilityAI、A*パス検索をMitiruエンジンに統合する。
///          型消去により任意のBlackboard型のビヘイビアツリーをサポートする。

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/ai/AStar.hpp>
#include <sgc/ai/BehaviorTree.hpp>
#include <sgc/ai/GOAP.hpp>
#include <sgc/ai/UtilityAI.hpp>

namespace mitiru::bridge
{

/// @brief AI状態（sgc::bt::Statusとの対応）
enum class AiState
{
	Idle,       ///< アイドル（未実行）
	Running,    ///< 実行中
	Success,    ///< 成功
	Failure     ///< 失敗
};

/// @brief UtilityAIのコンテキスト型（汎用ブラックボード）
/// @details AiBridge内部でUtilityAIの評価コンテキストとして使用する。
///          sgc::bt::Blackboardを流用する。
using AiContext = sgc::bt::Blackboard;

/// @brief UtilityAIアクション評価結果
struct UtilityResult
{
	std::string actionName;  ///< 選択されたアクション名
	float score = 0.0f;      ///< スコア
	std::size_t index = 0;   ///< アクションインデックス
};

/// @brief sgc AI統合ブリッジ
/// @details ビヘイビアツリー・UtilityAI・A*パス検索を統合管理する。
///          ビヘイビアツリーは型消去によりBlackboard型に依存しない。
///
/// @code
/// mitiru::bridge::AiBridge ai;
///
/// // ビヘイビアツリー登録
/// auto tree = sgc::bt::Builder()
///     .sequence()
///         .condition([](auto& bb) { return bb.template get<int>("hp").value_or(0) > 0; })
///         .action([](auto& bb) { return sgc::bt::Status::Success; })
///     .end()
///     .build();
/// ai.registerBehaviorTree("patrol", std::move(tree));
///
/// // 毎フレーム更新
/// sgc::bt::Blackboard bb;
/// ai.tickTree("patrol", bb);
/// @endcode
class AiBridge
{
public:
	// ── ビヘイビアツリー ────────────────────────────────────

	/// @brief ビヘイビアツリーを登録する
	/// @param name ビヘイビアツリー名
	/// @param rootNode sgc::bt::Builderで構築したルートノード
	void registerBehaviorTree(const std::string& name, std::unique_ptr<sgc::bt::Node> rootNode)
	{
		m_trees[name] = TreeEntry{std::move(rootNode), AiState::Idle};
	}

	/// @brief ビヘイビアツリーを登録する（後方互換: nullptrでスタブ登録）
	/// @param name ビヘイビアツリー名
	/// @param tree ツリーポインタ（nullptrの場合はノードなしで登録）
	void registerBehaviorTree(const std::string& name, std::nullptr_t)
	{
		m_trees[name] = TreeEntry{nullptr, AiState::Idle};
	}

	/// @brief ビヘイビアツリーの登録を解除する
	/// @param name ビヘイビアツリー名
	void unregisterBehaviorTree(const std::string& name)
	{
		m_trees.erase(name);
	}

	/// @brief ビヘイビアツリーを1フレーム分実行する
	/// @param name ビヘイビアツリー名
	/// @param blackboard ブラックボード（ノード間データ共有）
	/// @return 実行結果のAI状態（未登録の場合はIdle）
	AiState tickTree(const std::string& name, sgc::bt::Blackboard& blackboard)
	{
		const auto it = m_trees.find(name);
		if (it == m_trees.end() || !it->second.rootNode)
		{
			return AiState::Idle;
		}

		const sgc::bt::Status status = it->second.rootNode->tick(blackboard);
		it->second.state = convertStatus(status);
		return it->second.state;
	}

	/// @brief ビヘイビアツリーの現在状態を取得する
	/// @param name ビヘイビアツリー名
	/// @return AI状態（未登録の場合はIdle）
	[[nodiscard]] AiState getTreeState(const std::string& name) const
	{
		const auto it = m_trees.find(name);
		if (it == m_trees.end())
		{
			return AiState::Idle;
		}
		return it->second.state;
	}

	/// @brief ビヘイビアツリーの状態を手動設定する（後方互換）
	/// @param name ビヘイビアツリー名
	/// @param state 新しい状態
	void setState(const std::string& name, AiState state)
	{
		const auto it = m_trees.find(name);
		if (it != m_trees.end())
		{
			it->second.state = state;
		}
	}

	/// @brief ビヘイビアツリーの状態を取得する（後方互換: getTreeStateのエイリアス）
	/// @param name ビヘイビアツリー名
	/// @return AI状態（未登録の場合はIdle）
	[[nodiscard]] AiState getState(const std::string& name) const
	{
		return getTreeState(name);
	}

	/// @brief 登録されているビヘイビアツリー名一覧を取得する（後方互換エイリアス）
	/// @return 名前のベクタ
	[[nodiscard]] std::vector<std::string> registeredNames() const
	{
		return registeredTreeNames();
	}

	/// @brief 登録されているビヘイビアツリー名一覧を取得する
	/// @return 名前のベクタ
	[[nodiscard]] std::vector<std::string> registeredTreeNames() const
	{
		std::vector<std::string> names;
		names.reserve(m_trees.size());
		for (const auto& [name, entry] : m_trees)
		{
			names.push_back(name);
		}
		return names;
	}

	// ── UtilityAI ────────────────────────────────────────────

	/// @brief UtilityAIセレクタを登録する
	/// @param name セレクタ名
	/// @param selector 構築済みのUtilitySelector
	void registerUtilityAI(const std::string& name,
		sgc::ai::UtilitySelector<AiContext> selector)
	{
		m_utilitySelectors[name] = std::move(selector);
	}

	/// @brief UtilityAIの登録を解除する
	/// @param name セレクタ名
	void unregisterUtilityAI(const std::string& name)
	{
		m_utilitySelectors.erase(name);
	}

	/// @brief UtilityAIで最良アクションを選択する
	/// @param name セレクタ名
	/// @param context 評価コンテキスト
	/// @return 選択結果（未登録またはアクションなしの場合はnullopt）
	[[nodiscard]] std::optional<UtilityResult> selectBestAction(
		const std::string& name, const AiContext& context) const
	{
		const auto it = m_utilitySelectors.find(name);
		if (it == m_utilitySelectors.end())
		{
			return std::nullopt;
		}

		const auto result = it->second.evaluate(context);
		if (!result.has_value())
		{
			return std::nullopt;
		}

		return UtilityResult{result->name, result->score, result->index};
	}

	/// @brief UtilityAIの全アクションスコアを取得する
	/// @param name セレクタ名
	/// @param context 評価コンテキスト
	/// @return スコア降順のアクション一覧
	[[nodiscard]] std::vector<UtilityResult> evaluateAllActions(
		const std::string& name, const AiContext& context) const
	{
		const auto it = m_utilitySelectors.find(name);
		if (it == m_utilitySelectors.end())
		{
			return {};
		}

		const auto scores = it->second.evaluateAll(context);
		std::vector<UtilityResult> results;
		results.reserve(scores.size());
		for (const auto& s : scores)
		{
			results.push_back({s.name, s.score, s.index});
		}
		return results;
	}

	// ── A*パス検索 ──────────────────────────────────────────

	/// @brief 2DグリッドでA*パス検索を実行する
	/// @param grid グリッドグラフ
	/// @param start 開始位置
	/// @param goal ゴール位置
	/// @param maxIterations 最大探索回数
	/// @return パス探索結果（パスが見つからない場合はnullopt）
	[[nodiscard]] std::optional<sgc::ai::AStarResult<sgc::ai::GridPos>> findPath(
		const sgc::ai::GridGraph& grid,
		const sgc::ai::GridPos& start,
		const sgc::ai::GridPos& goal,
		std::size_t maxIterations = 10000) const
	{
		return sgc::ai::findPath(grid, start, goal, maxIterations);
	}

	// ── GOAPプランニング ────────────────────────────────────

	/// @brief GOAPアクションを登録する
	/// @param action GOAPアクション
	void addGoapAction(const sgc::ai::GOAPAction& action)
	{
		m_goapPlanner.addAction(action);
	}

	/// @brief GOAPプランを計画する
	/// @param current 現在のワールドステート
	/// @param goal ゴールのワールドステート
	/// @return アクションシーケンス（計画失敗時はnullopt）
	[[nodiscard]] auto planGoap(
		const sgc::ai::WorldState& current,
		const sgc::ai::WorldState& goal) const
	{
		return m_goapPlanner.plan(current, goal);
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief AI状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"treeCount\":" + std::to_string(m_trees.size()) + ",";

		/// ビヘイビアツリー
		json += "\"trees\":[";
		{
			bool first = true;
			for (const auto& [name, entry] : m_trees)
			{
				if (!first) json += ",";
				json += "{\"name\":\"" + name + "\",\"state\":\"" + stateToString(entry.state) + "\"}";
				first = false;
			}
		}
		json += "],";

		/// UtilityAIセレクタ
		json += "\"utilitySelectors\":[";
		{
			bool first = true;
			for (const auto& [name, selector] : m_utilitySelectors)
			{
				if (!first) json += ",";
				json += "{\"name\":\"" + name + "\",\"actionCount\":" +
					std::to_string(selector.actionCount()) + "}";
				first = false;
			}
		}
		json += "]";

		json += "}";
		return json;
	}

private:
	/// @brief sgc::bt::StatusからAiStateへ変換する
	/// @param status sgc側のステータス
	/// @return 対応するAiState
	[[nodiscard]] static AiState convertStatus(sgc::bt::Status status) noexcept
	{
		switch (status)
		{
		case sgc::bt::Status::Success: return AiState::Success;
		case sgc::bt::Status::Failure: return AiState::Failure;
		case sgc::bt::Status::Running: return AiState::Running;
		}
		return AiState::Idle;
	}

	/// @brief AI状態を文字列に変換する
	/// @param state AI状態
	/// @return 文字列表現
	[[nodiscard]] static std::string stateToString(AiState state)
	{
		switch (state)
		{
		case AiState::Idle:    return "Idle";
		case AiState::Running: return "Running";
		case AiState::Success: return "Success";
		case AiState::Failure: return "Failure";
		}
		return "Unknown";
	}

	/// @brief ビヘイビアツリー内部エントリ
	struct TreeEntry
	{
		std::unique_ptr<sgc::bt::Node> rootNode;  ///< ルートノード
		AiState state = AiState::Idle;             ///< 現在の状態
	};

	/// @brief 名前 → ビヘイビアツリー
	std::unordered_map<std::string, TreeEntry> m_trees;

	/// @brief 名前 → UtilityAIセレクタ
	std::unordered_map<std::string, sgc::ai::UtilitySelector<AiContext>> m_utilitySelectors;

	/// @brief GOAPプランナー
	sgc::ai::GOAPPlanner m_goapPlanner;
};

} // namespace mitiru::bridge
