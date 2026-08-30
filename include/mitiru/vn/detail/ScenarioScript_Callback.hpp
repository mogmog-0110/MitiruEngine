#pragma once

// mitiru::vn::ScenarioScript の detail ヘッダ。vn/ScenarioScript.hpp 経由で include される
// ScenarioCallback / ConditionEvaluator / ScriptExecutor / FlagManager fwd decl をまとめたファイル。

#include <functional>
#include <string>
#include <vector>

#include "ScenarioScript_Types.hpp"
#include "ScenarioScript_Inline.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  コールバックインターフェース
// ════════════════════════════════════════════════════════════════════

// 循環参照回避: FlagManager の前方宣言 (FlagManager.hpp は VN.hpp 経由で後から include される)
class FlagManager;

/// @brief シナリオ実行時のコールバックインターフェース
/// @details エグゼキューターが各コマンドを実行するたびに対応するコールバックを呼び出す。
///          デフォルト実装は何もしない。
struct ScenarioCallback
{
	virtual ~ScenarioCallback() = default;

	/// @brief シーンマーカー
	/// @param name シーン名
	virtual void onScene(const std::string& /*name*/) {}

	/// @brief 対話テキスト (raw 版、インラインタグを含む文字列をそのまま渡す)
	/// @param speaker 話者名（ナレーションの場合は空）
	/// @param text テキスト（"[wait=500]" 等のインラインタグを含みうる）
	virtual void onDialogue(const std::string& /*speaker*/, const std::string& /*text*/) {}

	/// @brief 対話テキスト (解析済み版、インラインタグ抽出後)
	/// @details エグゼキューターは毎回 dialogue 行で `onDialogue` と本コールバックの
	///          両方を呼ぶ。インラインタグ制御 (wait / shake / ruby 等) を扱いたい
	///          ゲームは本コールバックを override する。未 override の場合は no-op。
	/// @param speaker 話者名
	/// @param plainText インラインタグを除去したテキスト
	/// @param tags 抽出されたインラインタグ列
	virtual void onDialogueParsed(const std::string& /*speaker*/,
		const std::string& /*plainText*/,
		const std::vector<InlineTag>& /*tags*/) {}

	/// @brief 背景変更
	/// @param params 背景パラメータ
	virtual void onBackground(const BackgroundParams& /*params*/) {}

	/// @brief キャラクター制御
	/// @param params キャラクターパラメータ
	virtual void onCharacter(const CharacterParams& /*params*/) {}

	/// @brief 選択肢表示
	/// @param choices 選択肢リスト
	/// @return 選択されたインデックス（-1で待機要求）
	virtual int onChoice(const std::vector<ScenarioChoiceEntry>& /*choices*/) { return -1; }

	/// @brief オーディオ再生
	/// @param params オーディオパラメータ
	virtual void onAudio(const AudioParams& /*params*/) {}

	/// @brief 画面トランジション
	/// @param params トランジションパラメータ
	virtual void onTransition(const TransitionParams& /*params*/) {}

	/// @brief ウェイト
	/// @param duration 待機時間（秒）
	virtual void onWait(float /*duration*/) {}

	/// @brief 変数設定通知
	/// @param variable 変数名
	/// @param value 値
	virtual void onSetVariable(const std::string& /*variable*/, const std::string& /*value*/) {}

	/// @brief ラベル到達通知
	/// @param name ラベル名
	virtual void onLabel(const std::string& /*name*/) {}

	/// @brief ジャンプ通知
	/// @param name ジャンプ先ラベル名
	virtual void onJump(const std::string& /*name*/) {}
};

// ════════════════════════════════════════════════════════════════════
//  エグゼキューター関数オブジェクト型
// ════════════════════════════════════════════════════════════════════

/// @brief 条件評価関数の型
using ConditionEvaluator = std::function<bool(const std::string& expression)>;

/// @brief スクリプトブロック実行関数の型
/// @details `@script ... @endscript` ブロックの body (verbatim) を受け取り、
///          外部スクリプトエンジン (`IScriptingEngine` 実装等) に dispatch する。
///          `setConditionEvaluator` と対称的な疎結合フック。
using ScriptExecutor = std::function<void(const std::string& code)>;

} // namespace mitiru::vn
