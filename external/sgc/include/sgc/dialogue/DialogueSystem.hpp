#pragma once

/// @file DialogueSystem.hpp
/// @brief 分岐対話システム
///
/// ノードベースの対話グラフを管理し、選択肢による分岐をサポートする。
/// 条件付き選択肢、変数管理、対話の進行制御を提供。
///
/// @code
/// using namespace sgc::dialogue;
/// DialogueGraph graph;
/// graph.addNode("start", {"NPC", "Hello!", {
///     DialogueChoice{"Hi!", "greet"},
///     DialogueChoice{"Bye", "end"}
/// }});
/// DialogueRunner runner;
/// runner.start(graph);
/// runner.choose(0); // "Hi!"を選択
/// @endcode

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sgc::dialogue
{

/// @brief 対話変数の値型
using DialogueValue = std::variant<int, float, bool, std::string>;

/// @brief 対話変数マップ
using DialogueVariables = std::unordered_map<std::string, DialogueValue>;

/// @brief 条件関数型
using ConditionFunc = std::function<bool(const DialogueVariables&)>;

/// @brief 対話の選択肢
struct DialogueChoice
{
	std::string text;                     ///< 選択肢テキスト
	std::string nextNodeId;               ///< 遷移先ノードID
	std::optional<ConditionFunc> condition; ///< 表示条件（nulloptなら常に表示）
};

/// @brief 対話ノード
struct DialogueNode
{
	std::string id;                       ///< ノードID
	std::string speaker;                  ///< 話者名
	std::string text;                     ///< セリフテキスト
	std::vector<DialogueChoice> choices;  ///< 選択肢リスト
};

/// @brief 対話グラフ（ノード群のコレクション）
///
/// 複数のDialogueNodeを保持し、IDで管理する。
/// 開始ノードIDを指定してランナーに渡す。
class DialogueGraph
{
public:
	/// @brief ノードを追加する
	/// @param node 追加するノード
	void addNode(DialogueNode node)
	{
		const auto id = node.id;
		m_nodes.emplace(std::move(id), std::move(node));
	}

	/// @brief ノードを取得する
	/// @param id ノードID
	/// @return ノードへのポインタ。存在しなければnullptr
	[[nodiscard]] const DialogueNode* getNode(const std::string& id) const noexcept
	{
		const auto it = m_nodes.find(id);
		if (it == m_nodes.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief ノード数を取得
	/// @return ノード数
	[[nodiscard]] size_t nodeCount() const noexcept { return m_nodes.size(); }

	/// @brief 開始ノードIDを設定する
	/// @param id 開始ノードID
	void setStartNodeId(const std::string& id) { m_startNodeId = id; }

	/// @brief 開始ノードIDを取得
	/// @return 開始ノードID
	[[nodiscard]] const std::string& startNodeId() const noexcept { return m_startNodeId; }

	/// @brief 指定IDのノードが存在するか
	/// @param id ノードID
	/// @return 存在すればtrue
	[[nodiscard]] bool hasNode(const std::string& id) const noexcept
	{
		return m_nodes.find(id) != m_nodes.end();
	}

private:
	std::unordered_map<std::string, DialogueNode> m_nodes;  ///< ノードマップ
	std::string m_startNodeId;                               ///< 開始ノードID
};

/// @brief 対話グラフの実行ランナー
///
/// DialogueGraphを走査し、現在のノードの管理と選択肢による遷移を行う。
/// 対話変数を保持し、条件付き選択肢のフィルタリングも行う。
class DialogueRunner
{
public:
	/// @brief 対話を開始する
	/// @param graph 対話グラフへの参照
	void start(const DialogueGraph& graph)
	{
		m_graph = &graph;
		m_currentNodeId = graph.startNodeId();
		m_finished = false;
	}

	/// @brief 指定ノードIDから対話を開始する
	/// @param graph 対話グラフへの参照
	/// @param startId 開始ノードID
	void start(const DialogueGraph& graph, const std::string& startId)
	{
		m_graph = &graph;
		m_currentNodeId = startId;
		m_finished = false;
	}

	/// @brief 現在のノードを取得
	/// @return 現在のノードへのポインタ。終了済みまたは無効ならnullptr
	[[nodiscard]] const DialogueNode* currentNode() const noexcept
	{
		if (!m_graph || m_finished)
		{
			return nullptr;
		}
		return m_graph->getNode(m_currentNodeId);
	}

	/// @brief 選択肢を選択して次のノードに遷移する
	/// @param choiceIndex 選択する選択肢のインデックス（利用可能な選択肢内）
	/// @return 遷移成功ならtrue
	bool choose(size_t choiceIndex)
	{
		const auto* node = currentNode();
		if (!node)
		{
			return false;
		}

		// 利用可能な選択肢を取得
		const auto available = availableChoices();
		if (choiceIndex >= available.size())
		{
			return false;
		}

		const auto& choice = *available[choiceIndex];
		if (choice.nextNodeId.empty())
		{
			m_finished = true;
			return true;
		}

		m_currentNodeId = choice.nextNodeId;
		// 遷移先ノードが存在しなければ終了
		if (!m_graph->hasNode(m_currentNodeId))
		{
			m_finished = true;
		}
		return true;
	}

	/// @brief 対話が終了したか
	/// @return 終了済みならtrue
	[[nodiscard]] bool isFinished() const noexcept { return m_finished; }

	/// @brief 現在利用可能な選択肢を取得する（条件を評価済み）
	/// @return 利用可能な選択肢へのポインタ配列
	[[nodiscard]] std::vector<const DialogueChoice*> availableChoices() const
	{
		const auto* node = currentNode();
		if (!node)
		{
			return {};
		}

		std::vector<const DialogueChoice*> result;
		for (const auto& choice : node->choices)
		{
			if (!choice.condition.has_value() || choice.condition.value()(m_variables))
			{
				result.push_back(&choice);
			}
		}
		return result;
	}

	/// @brief 対話変数を設定する
	/// @param key 変数名
	/// @param value 変数値
	void setVariable(const std::string& key, DialogueValue value)
	{
		m_variables[key] = std::move(value);
	}

	/// @brief 対話変数を取得する
	/// @param key 変数名
	/// @return 変数値。存在しなければstd::nullopt
	[[nodiscard]] std::optional<DialogueValue> getVariable(const std::string& key) const
	{
		const auto it = m_variables.find(key);
		if (it == m_variables.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	/// @brief 対話変数マップへの読み取り専用アクセス
	/// @return 変数マップ
	[[nodiscard]] const DialogueVariables& variables() const noexcept { return m_variables; }

	/// @brief 現在のノードIDを取得
	/// @return 現在のノードID
	[[nodiscard]] const std::string& currentNodeId() const noexcept { return m_currentNodeId; }

private:
	const DialogueGraph* m_graph{nullptr};  ///< 対話グラフへのポインタ
	std::string m_currentNodeId;             ///< 現在のノードID
	bool m_finished{true};                   ///< 対話終了フラグ
	DialogueVariables m_variables;           ///< 対話変数
};

} // namespace sgc::dialogue
