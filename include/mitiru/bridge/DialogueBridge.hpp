#pragma once

/// @file DialogueBridge.hpp
/// @brief sgc対話統合ブリッジ
/// @details sgcのDialogueSystem（DialogueGraph + DialogueRunner）を
///          Mitiruエンジンに統合する。対話グラフの走査と選択肢処理を提供。

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include <sgc/dialogue/DialogueSystem.hpp>

namespace mitiru::bridge
{

/// @brief バックログエントリ（簡易版）
struct DialogueBacklogEntry
{
	std::string speaker;      ///< 話者名
	std::string text;         ///< テキスト内容
	std::string choiceMade;   ///< 選択した選択肢（空なら選択肢なし）
};

/// @brief sgc対話統合ブリッジ
/// @details 対話グラフの走査、選択肢処理、バックログを統合する。
///
/// @code
/// mitiru::bridge::DialogueBridge dlg;
///
/// sgc::dialogue::DialogueGraph graph;
/// graph.addNode({"start", "NPC", "Hello!", {{"Hi!", "greet"}}});
/// graph.addNode({"greet", "NPC", "Nice to meet you!", {}});
/// graph.setStartNodeId("start");
///
/// dlg.loadDialogue(std::move(graph));
/// dlg.startDialogue("start");
/// auto text = dlg.currentText();
/// dlg.advance();
/// @endcode
class DialogueBridge
{
public:
	// ── 対話グラフ ──────────────────────────────────────────

	/// @brief 対話グラフを読み込む
	/// @param graph sgc::dialogue::DialogueGraph
	void loadDialogue(sgc::dialogue::DialogueGraph graph)
	{
		m_graph = std::move(graph);
	}

	/// @brief 対話を開始する
	/// @param startNodeId 開始ノードID
	void startDialogue(const std::string& startNodeId)
	{
		m_runner.start(m_graph, startNodeId);
		m_active = true;

		/// バックログに最初のテキストを追加する
		const auto* node = m_runner.currentNode();
		if (node)
		{
			addToBacklog(node->speaker, node->text, "");
		}
	}

	/// @brief 次のノードに進む（選択肢がない場合）
	/// @details 選択肢が0個の場合は対話を終了する。
	///          選択肢が1個の場合は自動選択する。
	void advance()
	{
		if (!m_active) return;

		const auto* node = m_runner.currentNode();
		if (!node)
		{
			m_active = false;
			return;
		}

		const auto choices = m_runner.availableChoices();
		if (choices.empty())
		{
			/// 選択肢なし → 対話終了
			m_active = false;
			return;
		}

		if (choices.size() == 1)
		{
			/// 選択肢が1つ → 自動遷移
			m_runner.choose(0);
		}

		/// 進行後のノードをバックログに記録する
		const auto* nextNode = m_runner.currentNode();
		if (nextNode)
		{
			addToBacklog(nextNode->speaker, nextNode->text, "");
		}
		else
		{
			m_active = false;
		}
	}

	/// @brief 現在のテキストを取得する
	/// @return テキスト（非アクティブ時は空文字列）
	[[nodiscard]] std::string currentText() const
	{
		if (!m_active) return "";
		const auto* node = m_runner.currentNode();
		return node ? node->text : "";
	}

	/// @brief 現在の話者を取得する
	/// @return 話者名（非アクティブ時は空文字列）
	[[nodiscard]] std::string currentSpeaker() const
	{
		if (!m_active) return "";
		const auto* node = m_runner.currentNode();
		return node ? node->speaker : "";
	}

	/// @brief 現在の選択肢テキスト一覧を取得する
	/// @return 選択肢テキストのベクタ
	[[nodiscard]] std::vector<std::string> currentChoices() const
	{
		if (!m_active) return {};
		const auto choices = m_runner.availableChoices();
		std::vector<std::string> result;
		result.reserve(choices.size());
		for (const auto* choice : choices)
		{
			result.push_back(choice->text);
		}
		return result;
	}

	/// @brief 選択肢を選択する
	/// @param index 選択肢インデックス
	void selectChoice(int index)
	{
		if (!m_active || index < 0) return;

		const auto choices = m_runner.availableChoices();
		if (static_cast<std::size_t>(index) >= choices.size()) return;

		const auto choiceText = choices[static_cast<std::size_t>(index)]->text;
		m_runner.choose(static_cast<std::size_t>(index));

		/// 選択後のノードをバックログに記録する
		const auto* nextNode = m_runner.currentNode();
		if (nextNode)
		{
			addToBacklog(nextNode->speaker, nextNode->text, choiceText);
		}
		else
		{
			m_active = false;
		}
	}

	/// @brief 対話がアクティブか
	/// @return アクティブならtrue
	[[nodiscard]] bool isActive() const noexcept { return m_active; }

	// ── バックログ ──────────────────────────────────────────

	/// @brief バックログエントリ一覧を取得する
	/// @return バックログエントリのdeque
	[[nodiscard]] const std::deque<DialogueBacklogEntry>& backlog() const noexcept
	{
		return m_backlog;
	}

	/// @brief バックログのエントリ数を取得する
	/// @return エントリ数
	[[nodiscard]] std::size_t backlogSize() const noexcept
	{
		return m_backlog.size();
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief 対話状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"active\":" + std::string(m_active ? "true" : "false");
		json += ",\"currentNodeId\":\"" + m_runner.currentNodeId() + "\"";

		const auto* node = m_runner.currentNode();
		if (node)
		{
			json += ",\"speaker\":\"" + node->speaker + "\"";
			json += ",\"text\":\"" + node->text + "\"";
		}

		json += ",\"backlogSize\":" + std::to_string(m_backlog.size());
		json += "}";
		return json;
	}

private:
	/// @brief バックログにエントリを追加する（最大500件）
	/// @param speaker 話者名
	/// @param text テキスト
	/// @param choice 選択肢テキスト
	void addToBacklog(const std::string& speaker, const std::string& text,
		const std::string& choice)
	{
		m_backlog.push_back({speaker, text, choice});
		while (m_backlog.size() > MAX_BACKLOG_ENTRIES)
		{
			m_backlog.pop_front();
		}
	}

	static constexpr std::size_t MAX_BACKLOG_ENTRIES = 500;  ///< 最大バックログ数

	sgc::dialogue::DialogueGraph m_graph;                     ///< 対話グラフ
	sgc::dialogue::DialogueRunner m_runner;                   ///< 対話ランナー
	std::deque<DialogueBacklogEntry> m_backlog;               ///< バックログ
	bool m_active{false};                                     ///< アクティブ状態
};

} // namespace mitiru::bridge
