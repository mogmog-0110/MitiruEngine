#pragma once

/// @file Dialogue.hpp
/// @brief 会話の台本 (LineScript の行指向の書式) を読み取り専用の命令の表にしたもの。書式は docs/DIALOGUE.md。
/// @details 台本の文言・分岐・選択肢はアセット側 (DLL の static) に置き、進み具合 (DialogueState) と旗 (NarrativeVars)
///          だけを GameMemory に置く。進み具合は命令の添字なので、台本を書き換えると会話の途中のセーブは別の行を指す
///          (会話の外でとったセーブは影響を受けない)。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/core/Localization.hpp>
#include <mitiru/narrative/NarrativeCue.hpp>
#include <mitiru/narrative/NarrativeExpr.hpp>

namespace mitiru::narrative
{

enum class DialogueOpKind : std::uint8_t
{
	Line,       ///< 台詞を出して、進める操作を待つ
	Choice,     ///< 選択肢 1 つ。続けて書いた Choice が 1 組になる
	Jump,
	JumpIfNot,  ///< expr が偽なら target へ
	Set,        ///< var = expr
	Call,       ///< target へ行き、@return で戻る
	Return,
	End,
	CueOp,      ///< 合図を出してすぐ次へ
};

struct DialogueOp
{
	DialogueOpKind kind = DialogueOpKind::End;
	std::uint16_t  target = 0;
	ExprRef        expr{};
	std::uint32_t  var = 0;
	std::string    speaker;
	std::string    text;     ///< 表示する文言 (localize で訳に替わる)
	std::string    source;   ///< 書かれたままの文言
	std::string    key;      ///< 訳の鍵
	Cue            cue;
	int            line = 0; ///< 台本の行番号
};

class DialogueScript
{
public:
	std::string             name;
	std::uint32_t           id = 0;
	std::vector<DialogueOp> ops;
	std::vector<std::pair<std::string, std::uint16_t>> labels;
	ExprPool                exprs;
	std::vector<std::string> names;   ///< 式と @set に出てきた旗の名前 (story のツール窓が鍵を名前で出す)

	/// @brief ラベルの命令の添字。無ければ -1
	[[nodiscard]] int findLabel(std::string_view label) const noexcept
	{
		for (const auto& [n, at] : labels)
		{
			if (n == label) { return at; }
		}
		return -1;
	}

	/// @brief 台詞と選択肢を language の訳に替える。その言語の訳が無ければ書いたまま
	void localize(const LocalizationManager& loc, const std::string& language)
	{
		for (DialogueOp& op : ops)
		{
			if (op.kind != DialogueOpKind::Line && op.kind != DialogueOpKind::Choice) { continue; }
			op.text = op.key.empty() ? op.source : loc.find(op.key, language).value_or(op.source);
		}
	}
};

}  // namespace mitiru::narrative

#include <mitiru/narrative/detail/DialogueCompile.hpp>
