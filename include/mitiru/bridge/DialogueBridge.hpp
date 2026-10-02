#pragma once

/// @file DialogueBridge.hpp
/// @brief sgc の対話統合ブリッジ
/// @details sgc の DialogueSystem（DialogueGraph + DialogueRunner）を
///          Mitiru エンジンに統合する。対話グラフの走査と選択肢の処理を提供する。

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include <sgc/dialogue/DialogueSystem.hpp>
#include <mitiru/bridge/BridgeViewPush.hpp>
#include <mitiru/observe/JsonEscape.hpp>

#include <nlohmann/json.hpp>

namespace mitiru::bridge
{

/// @brief バックログエントリ（簡易版）
struct DialogueBacklogEntry
{
	std::string speaker;      ///< 話者名
	std::string text;         ///< テキスト内容
	std::string choiceMade;   ///< 選択した選択肢（空なら選択肢なし）
};

/// @brief sgc の対話統合ブリッジ
/// @details 対話グラフの走査、選択肢の処理、バックログを統合する。
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
	// ── View Push 統合 ─────────────────────────────────────

	/// @brief view push ハンドラを登録する（非所有の raw pointer）
	/// @details 登録すると startDialogue / advance / selectChoice の実行後に
	///          現在の対話状態が BridgeViewPush 経由で view 側に自動的に push される。
	///          呼び出し元が BridgeViewPush の lifetime を管理すること。
	///          nullptr を渡すと push を無効にできる。
	///
	/// push される key（subsystem prefix は consumer 側で指定）は次のとおり。
	/// - `"active"` → `"true"` または `"false"`
	/// - `"speaker"` → JSON-quoted 文字列
	/// - `"text"` → JSON-quoted 文字列
	/// - `"choices"` → JSON 配列 `["A","B"]`
	///
	/// @code
	/// BridgeViewPush vp("dialogue", setSink, emitSink);
	/// dlg.setViewPush(&vp);
	/// @endcode
	void setViewPush(BridgeViewPush* viewPush) noexcept
	{
		m_viewPush = viewPush;
	}

	// ── 対話グラフ ──────────────────────────────────────────

	/// @brief 対話グラフを読み込む
	/// @param graph sgc::dialogue::DialogueGraph
	void loadDialogue(sgc::dialogue::DialogueGraph graph)
	{
		m_graph = std::move(graph);
	}

	/// @brief 対話を開始する
	/// @param startNodeId 開始ノード ID
	void startDialogue(const std::string& startNodeId)
	{
		m_runner.start(m_graph, startNodeId);
		m_active = true;

		/// 最初のテキストをバックログに追加する
		const auto* node = m_runner.currentNode();
		if (node)
		{
			addToBacklog(node->speaker, node->text, "");
		}

		pushCurrentState();
	}

	/// @brief 選択肢がない場合に次のノードへ進む
	/// @details 選択肢が 0 個の場合は対話を終了する。
	///          選択肢が 1 個の場合は自動的に選択する。
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
			/// 選択肢がないため、対話を終了する
			m_active = false;
			pushActiveState(false);
			return;
		}

		if (choices.size() == 1)
		{
			/// 選択肢が 1 つの場合は自動的に遷移する
			m_runner.choose(0);
		}

		/// 進行後のノードをバックログに記録する
		const auto* nextNode = m_runner.currentNode();
		if (nextNode)
		{
			addToBacklog(nextNode->speaker, nextNode->text, "");
			pushCurrentState();
		}
		else
		{
			m_active = false;
			pushActiveState(false);
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

	/// @brief 現在の選択肢テキストの一覧を取得する
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
	/// @param index 選択肢のインデックス
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
			pushCurrentState();
		}
		else
		{
			m_active = false;
			pushActiveState(false);
		}
	}

	/// @brief 対話がアクティブか
	/// @return アクティブなら true
	[[nodiscard]] bool isActive() const noexcept { return m_active; }

	// ── バックログ ──────────────────────────────────────────

	/// @brief バックログエントリの一覧を取得する
	/// @return バックログエントリの deque
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

	/// @brief 対話状態を JSON 文字列として返す
	/// @return JSON 形式の文字列
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
	/// @brief バックログにエントリを追加する（最大 500 件）
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

	/// @brief active=true を前提として、現在のノードの全フィールドを view push する
	void pushCurrentState()
	{
		if (!m_viewPush) return;
		m_viewPush->set("active", "true");

		const auto* node = m_runner.currentNode();
		if (node)
		{
			m_viewPush->set("speaker", observe::jsonQuoted(node->speaker));
			m_viewPush->set("text",    observe::jsonQuoted(node->text));
		}

		const auto choicesVec = currentChoices();
		m_viewPush->set("choices",
			nlohmann::json(choicesVec).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
	}

	/// @brief 終了通知用に、active フラグのみを view push する
	void pushActiveState(bool active)
	{
		if (!m_viewPush) return;
		m_viewPush->set("active", active ? "true" : "false");
	}

	static constexpr std::size_t MAX_BACKLOG_ENTRIES = 500;  ///< 最大バックログ数

	sgc::dialogue::DialogueGraph m_graph;                     ///< 対話グラフ
	sgc::dialogue::DialogueRunner m_runner;                   ///< 対話ランナー
	std::deque<DialogueBacklogEntry> m_backlog;               ///< バックログ
	bool m_active{false};                                     ///< アクティブ状態
	BridgeViewPush* m_viewPush{nullptr};                      ///< view push ハンドラ（非所有）
};

} // namespace mitiru::bridge
