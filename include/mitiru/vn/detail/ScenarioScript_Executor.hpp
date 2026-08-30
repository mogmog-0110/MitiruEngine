#pragma once

// mitiru::vn::ScenarioScript の detail ヘッダ。vn/ScenarioScript.hpp 経由で include される
// class ScenarioExecutor のみを定義する。
//
// 注意: applySetToFlagManager の out-of-line 定義は
//       ScenarioScript_FlagManagerGlue.hpp に分離されている (FlagManager.hpp の
//       遅延 include による循環参照回避)。umbrella では本ファイル include の
//       後ろに _FlagManagerGlue.hpp を必ず置くこと。

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "ScenarioScript_Types.hpp"
#include "ScenarioScript_Lexer.hpp"
#include "ScenarioScript_Parser.hpp"
#include "ScenarioScript_Inline.hpp"
#include "ScenarioScript_Callback.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  エグゼキューター
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオスクリプトのステップ実行器
/// @details パース済みのScenarioNodeリストを1ステップずつ実行する。
///          コールバックを通じてエンジン側に指示を伝える。
///
/// @code
/// auto tokens = ScenarioLexer::tokenize(script);
/// auto nodes = ScenarioParser::parse(tokens);
///
/// struct MyCallback : mitiru::vn::ScenarioCallback {
///     void onDialogue(const std::string& speaker, const std::string& text) override {
///         // テキスト表示処理
///     }
/// };
///
/// MyCallback cb;
/// mitiru::vn::ScenarioExecutor executor;
/// executor.load(std::move(nodes));
/// executor.setCallback(&cb);
/// while (!executor.isFinished()) {
///     executor.step();
/// }
/// @endcode
class ScenarioExecutor
{
public:
	/// @brief コマンドリストをロードする
	/// @param nodes ScenarioNodeのベクタ
	void load(std::vector<ScenarioNode> nodes)
	{
		m_nodes = std::move(nodes);
		m_pc = 0;
		m_finished = false;
		m_waitingForChoice = false;
		m_labelMap.clear();

		// ラベルマップを構築
		for (std::size_t i = 0; i < m_nodes.size(); ++i)
		{
			if (m_nodes[i].type == ScenarioCommandType::Label)
			{
				m_labelMap[std::get<LabelParams>(m_nodes[i].payload).name] = i;
			}
		}
	}

	/// @brief ソーススクリプトから直接ロードする
	/// @param source スクリプト文字列
	void loadFromSource(std::string_view source)
	{
		auto tokens = ScenarioLexer::tokenize(source);
		auto nodes = ScenarioParser::parse(tokens);
		load(std::move(nodes));
	}

	/// @brief コールバックを設定する
	/// @param callback コールバックインターフェース（nullptrで無効化）
	void setCallback(ScenarioCallback* callback) noexcept
	{
		m_callback = callback;
	}

	/// @brief 条件評価関数を設定する（@if 用）
	/// @param evaluator 条件評価関数
	void setConditionEvaluator(ConditionEvaluator evaluator)
	{
		m_conditionEvaluator = std::move(evaluator);
	}

	/// @brief スクリプトブロック実行関数を設定する（@script ... @endscript 用）
	/// @details 設定後、`@script` ブロックを step() するたびに body 文字列が
	///          そのまま渡される。未設定なら `@script` は no-op (副作用なし)。
	///          engine 同梱の scripting 実装は廃止された（C++ gameplay 方針）ため、
	///          consumer は `IScriptingEngine` を自前で実装して注入する形になる。
	///          典型例:
	/// @code
	/// // consumer 側で IScriptingEngine を実装したと仮定
	/// auto engine = std::make_shared<MyScriptingEngine>();
	/// exec.setScriptExecutor([engine](const std::string& code) {
	///     auto r = engine->execute(code);
	///     if (!r.success) { /* log r.error */ }
	/// });
	/// @endcode
	/// @param executor スクリプト実行関数 (空コピー可、空なら no-op)
	void setScriptExecutor(ScriptExecutor executor)
	{
		m_scriptExecutor = std::move(executor);
	}

	/// @brief FlagManager を bind する (@set 自動書き込み用)
	/// @details bind すると `@set var value` が実行されたとき、value 文字列を
	///          型推論 (true/false → bool, 整数 → int, 小数 → float, それ以外 → string)
	///          して `FlagManager::set(var, ...)` を自動で呼ぶ。未 bind の場合は
	///          `onSetVariable` コールバックのみ発火する従来挙動。
	/// @param manager FlagManager インスタンス (非所有、nullptr で解除)
	void setFlagManager(FlagManager* manager) noexcept
	{
		m_flagManager = manager;
	}

	/// @brief 1ステップ実行する
	/// @return 実行されたコマンドの種類（終了済みの場合はnullopt）
	std::optional<ScenarioCommandType> step()
	{
		if (m_finished || m_pc >= m_nodes.size())
		{
			m_finished = true;
			return std::nullopt;
		}

		if (m_waitingForChoice)
		{
			return std::nullopt; // 選択肢待ち
		}

		const auto& node = m_nodes[m_pc];
		auto cmdType = node.type;
		++m_pc;

		executeNode(node);
		return cmdType;
	}

	/// @brief 選択肢を選択する（onChoiceが-1を返した場合に外部から呼ぶ）
	/// @param index 選択肢インデックス
	void selectChoice(std::size_t index)
	{
		if (!m_waitingForChoice || index >= m_pendingChoices.size())
		{
			return;
		}

		m_waitingForChoice = false;
		const auto& label = m_pendingChoices[index].label;
		if (!label.empty())
		{
			jumpToLabel(label);
		}
	}

	/// @brief 指定ラベルまでジャンプする
	/// @param label ラベル名
	/// @return 成功ならtrue
	bool jumpToLabel(const std::string& label)
	{
		auto it = m_labelMap.find(label);
		if (it != m_labelMap.end())
		{
			m_pc = it->second + 1; // ラベルの次のコマンドから実行
			return true;
		}
		return false;
	}

	/// @brief 実行が終了したか
	/// @return 終了済みならtrue
	[[nodiscard]] bool isFinished() const noexcept
	{
		return m_finished;
	}

	/// @brief 選択肢待ち状態か
	/// @return 選択肢待ちならtrue
	[[nodiscard]] bool isWaitingForChoice() const noexcept
	{
		return m_waitingForChoice;
	}

	/// @brief 現在のプログラムカウンタを取得する
	/// @return 現在のコマンドインデックス
	[[nodiscard]] std::size_t pc() const noexcept
	{
		return m_pc;
	}

	/// @brief コマンド総数を取得する
	/// @return コマンド数
	[[nodiscard]] std::size_t commandCount() const noexcept
	{
		return m_nodes.size();
	}

	/// @brief 現在のシーン名を取得する
	/// @return シーン名
	[[nodiscard]] const std::string& currentScene() const noexcept
	{
		return m_currentScene;
	}

	/// @brief 対話行数（Dialogueコマンドの数）を取得する
	/// @return 対話行数
	[[nodiscard]] std::size_t dialogueLineCount() const
	{
		std::size_t count = 0;
		for (const auto& node : m_nodes)
		{
			if (node.type == ScenarioCommandType::Dialogue)
			{
				++count;
			}
		}
		return count;
	}

	/// @brief 現在の対話行インデックスを取得する（既読追跡用）
	/// @return 直前に実行された対話行のインデックス（-1: 未実行）
	[[nodiscard]] int currentDialogueIndex() const noexcept
	{
		return m_currentDialogueIndex;
	}

	/// @brief コマンドノードを直接参照する
	/// @return コマンドリストへの参照
	[[nodiscard]] const std::vector<ScenarioNode>& nodes() const noexcept
	{
		return m_nodes;
	}

	/// @brief 待機中の選択肢を取得する
	/// @return 選択肢エントリのリスト
	[[nodiscard]] const std::vector<ScenarioChoiceEntry>& pendingChoices() const noexcept
	{
		return m_pendingChoices;
	}

	/// @brief 実行をリセットする（先頭から再実行可能にする）
	void reset()
	{
		m_pc = 0;
		m_finished = false;
		m_waitingForChoice = false;
		m_currentScene.clear();
		m_currentDialogueIndex = -1;
		m_pendingChoices.clear();
	}

private:
	/// @brief ノードを実行する
	void executeNode(const ScenarioNode& node)
	{
		switch (node.type)
		{
		case ScenarioCommandType::Scene:
		{
			const auto& sc = std::get<SceneParams>(node.payload);
			m_currentScene = sc.name;
			if (m_callback) m_callback->onScene(sc.name);
			break;
		}

		case ScenarioCommandType::Background:
			if (m_callback) m_callback->onBackground(std::get<BackgroundParams>(node.payload));
			break;

		case ScenarioCommandType::Bgm:
		case ScenarioCommandType::Se:
		case ScenarioCommandType::Voice:
			if (m_callback) m_callback->onAudio(std::get<AudioParams>(node.payload));
			break;

		case ScenarioCommandType::Character:
			if (m_callback) m_callback->onCharacter(std::get<CharacterParams>(node.payload));
			break;

		case ScenarioCommandType::Dialogue:
			++m_currentDialogueIndex;
			if (m_callback)
			{
				const auto& d = std::get<DialogueParams>(node.payload);
				m_callback->onDialogue(d.speaker, d.text);
				// インラインタグ自動解析 ("[wait=500]" 等)。games は onDialogueParsed を
				// override して plain text + tag 列を受け取る。tags 空なら nop override が走る。
				auto [plain, tags] = parseInlineTags(d.text);
				m_callback->onDialogueParsed(d.speaker, plain, tags);
			}
			break;

		case ScenarioCommandType::Choice:
			executeChoice(node);
			break;

		case ScenarioCommandType::Label:
			if (m_callback) m_callback->onLabel(std::get<LabelParams>(node.payload).name);
			break;

		case ScenarioCommandType::Jump:
		{
			const auto& lp = std::get<LabelParams>(node.payload);
			if (m_callback) m_callback->onJump(lp.name);
			jumpToLabel(lp.name);
			break;
		}

		case ScenarioCommandType::Set:
		{
			const auto& st = std::get<SetParams>(node.payload);
			if (m_callback) m_callback->onSetVariable(st.variable, st.value);
			applySetToFlagManager(st.variable, st.value);
			break;
		}

		case ScenarioCommandType::If:
			executeIf(node);
			break;

		case ScenarioCommandType::Else:
			// then-ブロックから落ちてきた場合: @endif までスキップして else-ブロックを飛ばす
			skipToEndIf();
			break;

		case ScenarioCommandType::EndIf:
			// 実行中に素通りしてきた場合は no-op
			break;

		case ScenarioCommandType::Wait:
			if (m_callback) m_callback->onWait(std::get<WaitParams>(node.payload).duration);
			break;

		case ScenarioCommandType::Transition:
			if (m_callback) m_callback->onTransition(std::get<TransitionParams>(node.payload));
			break;

		case ScenarioCommandType::Script:
			if (m_scriptExecutor) m_scriptExecutor(std::get<ScriptParams>(node.payload).body);
			break;
		}
	}

	/// @brief 選択肢コマンドを実行する
	void executeChoice(const ScenarioNode& node)
	{
		const auto& entries = std::get<ChoiceParams>(node.payload).entries;
		m_pendingChoices = entries;

		if (m_callback)
		{
			int selected = m_callback->onChoice(entries);
			if (selected >= 0 && static_cast<std::size_t>(selected) < entries.size())
			{
				const auto& label = entries[static_cast<std::size_t>(selected)].label;
				if (!label.empty())
				{
					jumpToLabel(label);
				}
				return;
			}
		}

		// コールバックが-1を返した場合は選択肢待ち状態に
		m_waitingForChoice = true;
	}

	/// @brief 条件分岐を実行する
	/// @details 真なら then-ブロックを実行（そのまま次ノード）、偽なら
	///          @else または @endif までスキップする。ネストは depth で追跡。
	void executeIf(const ScenarioNode& node)
	{
		bool result = false;
		if (m_conditionEvaluator)
		{
			result = m_conditionEvaluator(std::get<IfParams>(node.payload).condition);
		}

		if (!result)
		{
			// 偽の場合: @else または @endif までスキップ (@else があれば else-ブロックに入る)
			skipToElseOrEndIf();
		}
		// 真の場合: そのまま次のコマンドに進む。then-ブロック終端の
		// @else で skipToEndIf() が呼ばれるため else-ブロックはスキップされる。
	}

	/// @brief @endif までスキップする (ネストした @if を正しく数える)
	/// @details 真ブロックの終端で @else を踏んだとき使う。
	void skipToEndIf()
	{
		int depth = 1;
		while (m_pc < m_nodes.size())
		{
			const auto& node = m_nodes[m_pc];
			++m_pc;

			if (node.type == ScenarioCommandType::If)
			{
				++depth;
			}
			else if (node.type == ScenarioCommandType::EndIf)
			{
				--depth;
				if (depth == 0) return;
			}
		}
	}

	/// @brief @else または @endif までスキップする
	/// @details 偽ブロックのときに使う。@else で止まれば else-ブロック先頭から続行。
	///          @endif で止まればブロック全体を抜ける。ネストした @if の
	///          @else は無視する。
	void skipToElseOrEndIf()
	{
		int depth = 1;
		while (m_pc < m_nodes.size())
		{
			const auto& node = m_nodes[m_pc];
			++m_pc;

			if (node.type == ScenarioCommandType::If)
			{
				++depth;
			}
			else if (node.type == ScenarioCommandType::EndIf)
			{
				--depth;
				if (depth == 0) return;
			}
			else if (node.type == ScenarioCommandType::Else && depth == 1)
			{
				// 最外 @if の @else で停止。else-ブロック先頭から続行。
				return;
			}
		}
	}

	/// @brief @set value を型推論して FlagManager に書き込む
	/// @details 定義は ScenarioScript_FlagManagerGlue.hpp (FlagManager 本体 include 後)
	void applySetToFlagManager(const std::string& variable, const std::string& value);

	std::vector<ScenarioNode> m_nodes;							///< コマンドリスト
	std::unordered_map<std::string, std::size_t> m_labelMap;	///< ラベル→インデックスマップ
	std::size_t m_pc = 0;										///< プログラムカウンタ
	bool m_finished = false;									///< 終了フラグ
	bool m_waitingForChoice = false;							///< 選択肢待ちフラグ
	std::string m_currentScene;									///< 現在のシーン名
	int m_currentDialogueIndex = -1;							///< 現在の対話行インデックス
	std::vector<ScenarioChoiceEntry> m_pendingChoices;					///< 待機中の選択肢
	ScenarioCallback* m_callback = nullptr;						///< コールバック（非所有）
	ConditionEvaluator m_conditionEvaluator;					///< 条件評価関数
	ScriptExecutor m_scriptExecutor;							///< @script body dispatcher（未設定なら no-op）
	FlagManager* m_flagManager = nullptr;						///< @set 自動書込先（非所有）
};

} // namespace mitiru::vn
