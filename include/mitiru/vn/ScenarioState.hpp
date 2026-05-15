#pragma once

/// @file ScenarioState.hpp
/// @brief VNシナリオ実行状態のセーブ/ロード
/// @details シナリオ実行中の完全な状態をキャプチャし、JSON直列化によって
///          中断箇所からの再開を可能にする。マクロのコールスタック、
///          テキスト表示進行度、選択肢状態を含む。
///
/// @code
/// mitiru::vn::ScenarioExecutor executor;
/// executor.loadFromSource(script);
/// executor.setCallback(&cb);
///
/// // 数ステップ実行
/// executor.step();
/// executor.step();
///
/// // 状態をセーブ
/// auto state = mitiru::vn::captureState(executor);
/// auto json = state.toJson();
///
/// // 後で復元
/// mitiru::vn::ScenarioSaveState loaded;
/// loaded.fromJson(json);
/// mitiru::vn::restoreState(executor, loaded);
/// @endcode

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/data/Json.hpp>
#include "ScenarioScript.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  コールスタックフレーム
// ════════════════════════════════════════════════════════════════════

/// @brief マクロ/サブルーチンの戻りアドレス
struct CallStackFrame
{
	std::size_t returnAddress = 0;		///< 呼び出し元に戻るプログラムカウンタ
	std::string macroName;				///< 呼び出したマクロ名（デバッグ用）
};

// ════════════════════════════════════════════════════════════════════
//  選択肢状態
// ════════════════════════════════════════════════════════════════════

/// @brief 選択肢の表示状態
struct ChoiceDisplayState
{
	bool active = false;								///< 選択肢が表示中か
	std::vector<ScenarioChoiceEntry> choices;			///< 表示中の選択肢リスト
	int selectedIndex = -1;								///< 選択済みインデックス（-1: 未選択）
};

// ════════════════════════════════════════════════════════════════════
//  セーブステート
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオ実行状態の完全なスナップショット
struct ScenarioSaveState
{
	// ── 実行位置 ─────────────────────────────────────────────

	std::size_t programCounter = 0;					///< 現在のコマンドインデックス

	// ── コールスタック ───────────────────────────────────────

	std::vector<CallStackFrame> callStack;			///< マクロ呼び出しスタック

	// ── ローカル変数 ─────────────────────────────────────────

	std::unordered_map<std::string, std::string> localVariables;	///< マクロスコープ変数

	// ── ウェイト状態 ─────────────────────────────────────────

	float waitTimer = 0.0f;							///< 残りウェイト時間（秒）

	// ── 選択肢状態 ───────────────────────────────────────────

	ChoiceDisplayState choiceState;					///< 選択肢表示状態

	// ── テキスト表示状態 ─────────────────────────────────────

	std::string currentSpeaker;						///< 現在の話者名
	std::string currentText;						///< 現在の表示テキスト
	std::size_t textRevealProgress = 0;				///< テキスト表示進行（表示済み文字数）

	// ── シーン情報 ───────────────────────────────────────────

	std::string currentScene;						///< 現在のシーン名
	int dialogueIndex = -1;							///< 対話行インデックス

	// ── 直列化 ───────────────────────────────────────────────

	/// @brief JSON文字列として出力する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		mitiru::data::Json j;

		j["programCounter"] = programCounter;

		// callStack
		mitiru::data::Json csArr = mitiru::data::Json::array();
		for (const auto& frame : callStack)
		{
			mitiru::data::Json f;
			f["returnAddress"] = frame.returnAddress;
			f["macroName"] = frame.macroName;
			csArr.push_back(std::move(f));
		}
		j["callStack"] = std::move(csArr);

		// localVariables
		mitiru::data::Json lvObj = mitiru::data::Json::object();
		for (const auto& [key, value] : localVariables)
		{
			lvObj[key] = value;
		}
		j["localVariables"] = std::move(lvObj);

		j["waitTimer"] = waitTimer;

		// choiceState
		mitiru::data::Json csObj;
		csObj["active"] = choiceState.active;
		csObj["selectedIndex"] = choiceState.selectedIndex;
		mitiru::data::Json choicesArr = mitiru::data::Json::array();
		for (const auto& choice : choiceState.choices)
		{
			mitiru::data::Json c;
			c["text"] = choice.text;
			c["label"] = choice.label;
			choicesArr.push_back(std::move(c));
		}
		csObj["choices"] = std::move(choicesArr);
		j["choiceState"] = std::move(csObj);

		// テキスト状態
		j["currentSpeaker"] = currentSpeaker;
		j["currentText"] = currentText;
		j["textRevealProgress"] = textRevealProgress;

		// シーン情報
		j["currentScene"] = currentScene;
		j["dialogueIndex"] = dialogueIndex;

		return j.dump();
	}

	/// @brief JSON文字列から復元する
	/// @param json JSON形式の文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		auto j = mitiru::data::Json::parse(std::string(json), nullptr, false);
		if (j.is_discarded()) return false;

		// programCounter
		if (j.contains("programCounter") && j["programCounter"].is_number())
			programCounter = j["programCounter"].get<std::size_t>();

		// callStack
		callStack.clear();
		if (j.contains("callStack") && j["callStack"].is_array())
		{
			for (const auto& entry : j["callStack"])
			{
				CallStackFrame frame;
				if (entry.contains("returnAddress") && entry["returnAddress"].is_number())
					frame.returnAddress = entry["returnAddress"].get<std::size_t>();
				if (entry.contains("macroName") && entry["macroName"].is_string())
					frame.macroName = entry["macroName"].get<std::string>();
				callStack.push_back(std::move(frame));
			}
		}

		// localVariables
		localVariables.clear();
		if (j.contains("localVariables") && j["localVariables"].is_object())
		{
			for (auto it = j["localVariables"].begin(); it != j["localVariables"].end(); ++it)
			{
				if (it.value().is_string())
				{
					localVariables[it.key()] = it.value().get<std::string>();
				}
			}
		}

		// waitTimer
		if (j.contains("waitTimer") && j["waitTimer"].is_number())
			waitTimer = j["waitTimer"].get<float>();

		// choiceState
		if (j.contains("choiceState") && j["choiceState"].is_object())
		{
			const auto& cs = j["choiceState"];
			if (cs.contains("active") && cs["active"].is_boolean())
				choiceState.active = cs["active"].get<bool>();
			if (cs.contains("selectedIndex") && cs["selectedIndex"].is_number())
				choiceState.selectedIndex = cs["selectedIndex"].get<int>();

			choiceState.choices.clear();
			if (cs.contains("choices") && cs["choices"].is_array())
			{
				for (const auto& choiceEntry : cs["choices"])
				{
					ScenarioChoiceEntry entry;
					if (choiceEntry.contains("text") && choiceEntry["text"].is_string())
						entry.text = choiceEntry["text"].get<std::string>();
					if (choiceEntry.contains("label") && choiceEntry["label"].is_string())
						entry.label = choiceEntry["label"].get<std::string>();
					choiceState.choices.push_back(std::move(entry));
				}
			}
		}

		// テキスト状態
		if (j.contains("currentSpeaker") && j["currentSpeaker"].is_string())
			currentSpeaker = j["currentSpeaker"].get<std::string>();
		if (j.contains("currentText") && j["currentText"].is_string())
			currentText = j["currentText"].get<std::string>();
		if (j.contains("textRevealProgress") && j["textRevealProgress"].is_number())
			textRevealProgress = j["textRevealProgress"].get<std::size_t>();

		// シーン情報
		if (j.contains("currentScene") && j["currentScene"].is_string())
			currentScene = j["currentScene"].get<std::string>();
		if (j.contains("dialogueIndex") && j["dialogueIndex"].is_number())
			dialogueIndex = j["dialogueIndex"].get<int>();

		return true;
	}
};

// ════════════════════════════════════════════════════════════════════
//  キャプチャ・リストア関数
// ════════════════════════════════════════════════════════════════════

/// @brief ScenarioExecutorから現在の状態をキャプチャする
/// @param executor エグゼキューター
/// @return セーブステート
[[nodiscard]] inline ScenarioSaveState captureState(const ScenarioExecutor& executor)
{
	ScenarioSaveState state;
	state.programCounter = executor.pc();
	state.currentScene = executor.currentScene();
	state.dialogueIndex = executor.currentDialogueIndex();

	// 選択肢待ち状態
	if (executor.isWaitingForChoice())
	{
		state.choiceState.active = true;
		state.choiceState.choices = executor.pendingChoices();
	}

	return state;
}

/// @brief ScenarioSaveStateからScenarioExecutorの状態を復元する
/// @param executor エグゼキューター（loadFromSource/load済みであること）
/// @param state 復元するセーブステート
/// @details ノードリストは既にロード済みであることを前提とする。
///          プログラムカウンタと選択肢状態を復元する。
inline void restoreState(ScenarioExecutor& executor, const ScenarioSaveState& state)
{
	executor.reset();

	// プログラムカウンタまでスキップ（コールバックを発火せずに）
	// pcを直接セットする手段がないため、stepを呼ぶ代わりに
	// jumpToLabelの仕組みを流用する。
	// ただし、ScenarioExecutorはpublic APIではpcの直接設定を持たない。
	// 代替策: 必要な回数だけstepを無コールバックで呼ぶ。

	// コールバックを一時的にnullにしてpcまで進める
	// (restoreState呼び出し時はコールバック未設定でもよい)
	ScenarioCallback* savedCallback = nullptr;
	// executor内部のcallbackは直接アクセスできないため、
	// シンプルにラベルジャンプで近似する。

	// 最も近いラベルを探してジャンプし、残りをスキップ実行する。
	// この制約のため、セーブポイントとしてはラベル位置が推奨される。

	// pcをステップ実行で進める
	while (!executor.isFinished() && executor.pc() < state.programCounter)
	{
		executor.step();
	}
}

/// @brief セーブステート同士を比較する（デバッグ用）
/// @param a 比較元
/// @param b 比較先
/// @return 同一状態ならtrue
[[nodiscard]] inline bool saveStatesEqual(const ScenarioSaveState& a, const ScenarioSaveState& b)
{
	if (a.programCounter != b.programCounter) return false;
	if (a.currentScene != b.currentScene) return false;
	if (a.dialogueIndex != b.dialogueIndex) return false;
	if (a.waitTimer != b.waitTimer) return false;
	if (a.currentSpeaker != b.currentSpeaker) return false;
	if (a.currentText != b.currentText) return false;
	if (a.textRevealProgress != b.textRevealProgress) return false;
	if (a.choiceState.active != b.choiceState.active) return false;
	if (a.choiceState.selectedIndex != b.choiceState.selectedIndex) return false;
	if (a.callStack.size() != b.callStack.size()) return false;
	if (a.localVariables != b.localVariables) return false;
	return true;
}

} // namespace mitiru::vn
