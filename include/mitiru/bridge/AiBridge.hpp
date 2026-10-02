#pragma once

/// @file AiBridge.hpp
/// @brief sgc の AI 統合ブリッジ
/// @details sgc の BehaviorTree、UtilityAI、A* パス検索を Mitiru エンジンに統合する。
///          型消去により任意の Blackboard 型のビヘイビアツリーをサポートする。

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

/// @brief AI 状態（sgc::bt::Status との対応）
enum class AiState
{
	Idle,       ///< アイドル（未実行）
	Running,    ///< 実行中
	Success,    ///< 成功
	Failure     ///< 失敗
};

/// @brief UtilityAI のコンテキスト型（汎用ブラックボード）
/// @details AiBridge 内部で UtilityAI の評価コンテキストとして使用する。
///          sgc::bt::Blackboard を流用する。
using AiContext = sgc::bt::Blackboard;

/// @brief UtilityAI アクション評価結果
struct UtilityResult
{
	std::string actionName;  ///< 選択されたアクション名
	float score = 0.0f;      ///< スコア
	std::size_t index = 0;   ///< アクションインデックス
};

/// @brief sgc の AI 統合ブリッジ
/// @details ビヘイビアツリー・UtilityAI・A* パス検索をまとめて管理する。
///          ビヘイビアツリーは型消去により Blackboard 型に依存しない。
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
	/// @param rootNode sgc::bt::Builder で構築したルートノード
	void registerBehaviorTree(const std::string& name, std::unique_ptr<sgc::bt::Node> rootNode)
	{
		m_trees[name] = TreeEntry{std::move(rootNode), AiState::Idle};
	}

	/// @brief ビヘイビアツリーを登録する（後方互換: nullptr でスタブ登録）
	/// @param name ビヘイビアツリー名
	/// @param tree ツリーポインタ（nullptr の場合はノードなしで登録）
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

	/// @brief ビヘイビアツリーを 1 フレーム分実行する
	/// @param name ビヘイビアツリー名
	/// @param blackboard ブラックボード（ノード間で共有するデータ）
	/// @return 実行結果の AI 状態（未登録の場合は Idle）
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

	/// @brief ビヘイビアツリーの現在の状態を取得する
	/// @param name ビヘイビアツリー名
	/// @return AI 状態（未登録の場合は Idle）
	[[nodiscard]] AiState getTreeState(const std::string& name) const
	{
		const auto it = m_trees.find(name);
		if (it == m_trees.end())
		{
			return AiState::Idle;
		}
		return it->second.state;
	}

	/// @brief ビヘイビアツリーの状態を手動で設定する（後方互換）
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

	/// @brief ビヘイビアツリーの状態を取得する（後方互換: getTreeState のエイリアス）
	/// @param name ビヘイビアツリー名
	/// @return AI 状態（未登録の場合は Idle）
	[[nodiscard]] AiState getState(const std::string& name) const
	{
		return getTreeState(name);
	}

	/// @brief 登録されているビヘイビアツリー名の一覧を取得する（後方互換エイリアス）
	/// @return 名前のベクタ
	[[nodiscard]] std::vector<std::string> registeredNames() const
	{
		return registeredTreeNames();
	}

	/// @brief 登録されているビヘイビアツリー名の一覧を取得する
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

	/// @brief UtilityAI セレクタを登録する
	/// @param name セレクタ名
	/// @param selector 構築済みの UtilitySelector
	void registerUtilityAI(const std::string& name,
		sgc::ai::UtilitySelector<AiContext> selector)
	{
		m_utilitySelectors[name] = std::move(selector);
	}

	/// @brief UtilityAI の登録を解除する
	/// @param name セレクタ名
	void unregisterUtilityAI(const std::string& name)
	{
		m_utilitySelectors.erase(name);
	}

	/// @brief UtilityAI で最良アクションを選択する
	/// @param name セレクタ名
	/// @param context 評価コンテキスト
	/// @return 選択結果（未登録またはアクションなしの場合は nullopt）
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

	/// @brief UtilityAI の全アクションスコアを取得する
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

	/// @brief 2D グリッドで A* パス検索を実行する
	/// @param grid グリッドグラフ
	/// @param start 開始位置
	/// @param goal ゴール位置
	/// @param maxIterations 最大探索回数
	/// @return パス探索結果（パスが見つからない場合は nullopt）
	[[nodiscard]] std::optional<sgc::ai::AStarResult<sgc::ai::GridPos>> findPath(
		const sgc::ai::GridGraph& grid,
		const sgc::ai::GridPos& start,
		const sgc::ai::GridPos& goal,
		std::size_t maxIterations = 10000) const
	{
		return sgc::ai::findPath(grid, start, goal, maxIterations);
	}

	// ── GOAP プランニング ────────────────────────────────────

	/// @brief GOAP アクションを登録する
	/// @param action GOAP アクション
	void addGoapAction(const sgc::ai::GOAPAction& action)
	{
		m_goapPlanner.addAction(action);
	}

	/// @brief GOAP のプランを立てる
	/// @param current 現在のワールドステート
	/// @param goal ゴールのワールドステート
	/// @return アクションシーケンス（計画に失敗した場合は nullopt）
	[[nodiscard]] auto planGoap(
		const sgc::ai::WorldState& current,
		const sgc::ai::WorldState& goal) const
	{
		return m_goapPlanner.plan(current, goal);
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief AI 状態を JSON 文字列として返す
	/// @return JSON 形式の文字列
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

		/// UtilityAI セレクタ
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
	/// @brief sgc::bt::Status から AiState へ変換する
	/// @param status sgc 側のステータス
	/// @return 対応する AiState
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

	/// @brief AI 状態を文字列に変換する
	/// @param state AI 状態
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

	/// @brief ビヘイビアツリーの内部エントリ
	struct TreeEntry
	{
		std::unique_ptr<sgc::bt::Node> rootNode;  ///< ルートノード
		AiState state = AiState::Idle;             ///< 現在の状態
	};

	/// @brief 名前からビヘイビアツリーへの対応
	std::unordered_map<std::string, TreeEntry> m_trees;

	/// @brief 名前 → UtilityAI セレクタ
	std::unordered_map<std::string, sgc::ai::UtilitySelector<AiContext>> m_utilitySelectors;

	/// @brief GOAP プランナー
	sgc::ai::GOAPPlanner m_goapPlanner;
};

} // namespace mitiru::bridge
