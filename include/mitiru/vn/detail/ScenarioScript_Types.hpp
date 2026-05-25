#pragma once

// mitiru::vn::ScenarioScript の detail ヘッダ — vn/ScenarioScript.hpp 経由で include される

#include <cstddef>
#include <string>
#include <vector>

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  トークン定義
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオトークン種別
enum class ScenarioTokenType
{
	Command,		///< @command
	String,			///< "quoted string"
	Number,			///< 数値リテラル
	Identifier,		///< 識別子（ラベル名、変数名など）
	Arrow,			///< ->
	Newline,		///< 改行
	ScriptBody,		///< @script ... @endscript の raw body (改行・空白を verbatim 保持)
	Eof,			///< 入力終端
};

/// @brief シナリオトークン
struct ScenarioToken
{
	ScenarioTokenType type = ScenarioTokenType::Eof;
	std::string text;
	double numValue = 0.0;
	std::size_t line = 0;		///< ソース行番号（1始まり）
};

// ════════════════════════════════════════════════════════════════════
//  AST ノード定義
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオコマンドの種類
enum class ScenarioCommandType
{
	Scene,			///< @scene "name"
	Background,		///< @bg "file" [transition] [duration]
	Bgm,			///< @bgm "file"
	Se,				///< @se "file"
	Voice,			///< @voice "file"
	Character,		///< @char "name" [pos] [expr] [show/hide] [transition]
	Dialogue,		///< speaker "text"
	Choice,			///< @choice ... @endchoice
	Label,			///< @label name
	Jump,			///< @jump name
	Set,			///< @set var value
	If,				///< @if condition ... @endif
	Else,			///< @else (inside @if block)
	EndIf,			///< @endif (terminates @if block)
	Wait,			///< @wait duration
	Transition,		///< @transition type duration
	Script,			///< @script ... @endscript（IScriptingEngine 等の外部スクリプトに dispatch）
};

/// @brief 選択肢エントリ
struct ScenarioChoiceEntry
{
	std::string text;		///< 表示テキスト
	std::string label;		///< ジャンプ先ラベル
};

/// @brief キャラクターコマンドのパラメータ
struct CharacterParams
{
	std::string name;						///< キャラクター名
	std::string position;					///< 位置（left, center, right など）
	std::string expression;					///< 表情
	bool show = true;						///< 表示/非表示
	std::string transition;					///< トランジション
};

/// @brief 背景コマンドのパラメータ
struct BackgroundParams
{
	std::string file;						///< ファイル名
	std::string transition;					///< トランジション種別
	float duration = 0.0f;					///< トランジション時間
};

/// @brief オーディオコマンドのパラメータ
struct AudioParams
{
	std::string file;						///< ファイル名
	std::string type;						///< "bgm", "se", "voice"
};

/// @brief 条件分岐のパラメータ
struct IfParams
{
	std::string condition;					///< 条件式
	std::vector<std::size_t> thenCommands;	///< 真の場合のコマンドインデックス
	std::vector<std::size_t> elseCommands;	///< 偽の場合のコマンドインデックス
};

/// @brief 変数設定のパラメータ
struct SetParams
{
	std::string variable;					///< 変数名
	std::string value;						///< 値（文字列表現）
};

/// @brief トランジションのパラメータ
struct TransitionParams
{
	std::string type;						///< トランジション種別
	float duration = 0.0f;					///< 時間
};

/// @brief 対話のパラメータ
struct DialogueParams
{
	std::string speaker;					///< 話者名（空ならナレーション）
	std::string text;						///< テキスト
};

/// @brief シナリオコマンドノード
struct ScenarioNode
{
	ScenarioCommandType type;
	std::size_t sourceLine = 0;				///< ソース行番号

	// コマンド種別に応じたパラメータ（typeに対応するものだけ有効）
	std::string sceneName;					///< Scene
	BackgroundParams background;			///< Background
	AudioParams audio;						///< Bgm, Se, Voice
	CharacterParams character;				///< Character
	DialogueParams dialogue;				///< Dialogue
	std::vector<ScenarioChoiceEntry> choices;		///< Choice
	std::string labelName;					///< Label, Jump
	SetParams setParams;					///< Set
	IfParams ifParams;						///< If
	float waitDuration = 0.0f;				///< Wait
	TransitionParams transition;			///< Transition
	std::string scriptBody;					///< Script: @script ... @endscript の verbatim body
};

} // namespace mitiru::vn
