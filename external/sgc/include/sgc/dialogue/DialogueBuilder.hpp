#pragma once

/// @file DialogueBuilder.hpp
/// @brief 対話グラフのフルーエントビルダー
///
/// メソッドチェーンで対話グラフを簡潔に構築する。
///
/// @code
/// using namespace sgc::dialogue;
/// auto graph = DialogueBuilder()
///     .node("start", "NPC", "Hello there!")
///         .choice("Hi!", "greet")
///         .choiceIf("Secret", "secret", [](const auto& vars) {
///             auto it = vars.find("hasKey");
///             return it != vars.end() && std::get<bool>(it->second);
///         })
///     .node("greet", "NPC", "Nice to meet you!")
///         .choice("Goodbye", "end")
///     .node("secret", "NPC", "You found the secret!")
///         .choice("Cool!", "end")
///     .node("end", "NPC", "Farewell!")
///     .setStart("start")
///     .build();
/// @endcode

#include "sgc/dialogue/DialogueSystem.hpp"

namespace sgc::dialogue
{

/// @brief 対話グラフのフルーエントビルダー
///
/// メソッドチェーンでDialogueGraphを構築する。
/// node()で新しいノードを開始し、choice()/choiceIf()で選択肢を追加する。
class DialogueBuilder
{
public:
	/// @brief 新しいノードを開始する
	/// @param id ノードID
	/// @param speaker 話者名
	/// @param text セリフテキスト
	/// @return ビルダー自身への参照
	DialogueBuilder& node(std::string id, std::string speaker, std::string text)
	{
		flushCurrentNode();
		m_currentNode = DialogueNode{
			std::move(id),
			std::move(speaker),
			std::move(text),
			{}
		};
		return *this;
	}

	/// @brief 現在のノードに無条件の選択肢を追加する
	/// @param text 選択肢テキスト
	/// @param nextId 遷移先ノードID
	/// @return ビルダー自身への参照
	DialogueBuilder& choice(std::string text, std::string nextId)
	{
		if (m_currentNode.has_value())
		{
			m_currentNode->choices.push_back(DialogueChoice{
				std::move(text),
				std::move(nextId),
				std::nullopt
			});
		}
		return *this;
	}

	/// @brief 現在のノードに条件付き選択肢を追加する
	/// @param text 選択肢テキスト
	/// @param nextId 遷移先ノードID
	/// @param condition 表示条件関数
	/// @return ビルダー自身への参照
	DialogueBuilder& choiceIf(std::string text, std::string nextId, ConditionFunc condition)
	{
		if (m_currentNode.has_value())
		{
			m_currentNode->choices.push_back(DialogueChoice{
				std::move(text),
				std::move(nextId),
				std::move(condition)
			});
		}
		return *this;
	}

	/// @brief 開始ノードIDを設定する
	/// @param id 開始ノードID
	/// @return ビルダー自身への参照
	DialogueBuilder& setStart(std::string id)
	{
		m_startNodeId = std::move(id);
		return *this;
	}

	/// @brief 対話グラフを構築する
	/// @return 構築されたDialogueGraph
	[[nodiscard]] DialogueGraph build()
	{
		flushCurrentNode();
		DialogueGraph graph;
		for (auto& [id, node] : m_nodes)
		{
			graph.addNode(std::move(node));
		}
		if (!m_startNodeId.empty())
		{
			graph.setStartNodeId(m_startNodeId);
		}
		return graph;
	}

private:
	/// @brief 現在構築中のノードをマップにフラッシュする
	void flushCurrentNode()
	{
		if (m_currentNode.has_value())
		{
			const auto id = m_currentNode->id;
			m_nodes.emplace(id, std::move(m_currentNode.value()));
			m_currentNode.reset();
		}
	}

	std::optional<DialogueNode> m_currentNode;                    ///< 構築中のノード
	std::unordered_map<std::string, DialogueNode> m_nodes;        ///< 構築済みノード
	std::string m_startNodeId;                                     ///< 開始ノードID
};

} // namespace sgc::dialogue
