#pragma once

// Detail header for mitiru::vn::ScenarioScript — included via vn/ScenarioScript.hpp

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ScenarioScript_Types.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  構文解析器
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオスクリプトの構文解析器
/// @details トークン列からScenarioNodeのシーケンスを生成する。
class ScenarioParser
{
public:
	/// @brief トークン列をパースしてコマンドリストを生成する
	/// @param tokens トークン列
	/// @return ScenarioNodeのベクタ
	[[nodiscard]] static std::vector<ScenarioNode> parse(const std::vector<ScenarioToken>& tokens)
	{
		std::vector<ScenarioNode> nodes;
		std::size_t pos = 0;

		while (pos < tokens.size() && tokens[pos].type != ScenarioTokenType::Eof)
		{
			skipNewlines(tokens, pos);
			if (pos >= tokens.size() || tokens[pos].type == ScenarioTokenType::Eof) break;

			const auto& tok = tokens[pos];

			if (tok.type == ScenarioTokenType::Command)
			{
				auto node = parseCommand(tokens, pos);
				if (node.has_value())
				{
					nodes.push_back(std::move(*node));
				}
			}
			else if (tok.type == ScenarioTokenType::Identifier || tok.type == ScenarioTokenType::String)
			{
				auto node = parseDialogue(tokens, pos);
				if (node.has_value())
				{
					nodes.push_back(std::move(*node));
				}
			}
			else
			{
				++pos; // 不明なトークンをスキップ
			}
		}

		return nodes;
	}

private:
	/// @brief 改行をスキップする
	static void skipNewlines(const std::vector<ScenarioToken>& tokens, std::size_t& pos)
	{
		while (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Newline)
		{
			++pos;
		}
	}

	/// @brief 行末までスキップする
	static void skipToEndOfLine(const std::vector<ScenarioToken>& tokens, std::size_t& pos)
	{
		while (pos < tokens.size() &&
			tokens[pos].type != ScenarioTokenType::Newline &&
			tokens[pos].type != ScenarioTokenType::Eof)
		{
			++pos;
		}
	}

	/// @brief 行末までの残りのトークンを連結して文字列にする
	[[nodiscard]] static std::string collectRestOfLine(const std::vector<ScenarioToken>& tokens, std::size_t& pos)
	{
		std::string result;
		while (pos < tokens.size() &&
			tokens[pos].type != ScenarioTokenType::Newline &&
			tokens[pos].type != ScenarioTokenType::Eof)
		{
			if (!result.empty()) result += " ";
			result += tokens[pos].text;
			++pos;
		}
		return result;
	}

	/// @brief コマンドをパースする
	[[nodiscard]] static std::optional<ScenarioNode> parseCommand(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos)
	{
		const auto& cmdToken = tokens[pos];
		auto line = cmdToken.line;
		const auto& cmd = cmdToken.text;
		++pos;

		if (cmd == "scene") return parseScene(tokens, pos, line);
		if (cmd == "bg") return parseBackground(tokens, pos, line);
		if (cmd == "bgm") return parseAudio(tokens, pos, line, "bgm");
		if (cmd == "se") return parseAudio(tokens, pos, line, "se");
		if (cmd == "voice") return parseAudio(tokens, pos, line, "voice");
		if (cmd == "char") return parseCharacter(tokens, pos, line);
		if (cmd == "choice") return parseChoice(tokens, pos, line);
		if (cmd == "label") return parseLabel(tokens, pos, line);
		if (cmd == "jump") return parseJump(tokens, pos, line);
		if (cmd == "set") return parseSet(tokens, pos, line);
		if (cmd == "if") return parseIf(tokens, pos, line);
		if (cmd == "else") return parseElse(tokens, pos, line);
		if (cmd == "endif") return parseEndIf(tokens, pos, line);
		if (cmd == "wait") return parseWait(tokens, pos, line);
		if (cmd == "transition") return parseTransition(tokens, pos, line);
		if (cmd == "script") return parseScript(tokens, pos, line);
		// `endscript` は @script の閉じトークンとして lexer で emit される。
		// 単独で来た場合 (= マッチしない @endscript) は no-op として黙殺する。
		if (cmd == "endscript")
		{
			skipToEndOfLine(tokens, pos);
			return std::nullopt;
		}

		// 未知のコマンド: 行末までスキップ
		skipToEndOfLine(tokens, pos);
		return std::nullopt;
	}

	/// @brief @scene "name"
	[[nodiscard]] static std::optional<ScenarioNode> parseScene(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Scene;
		node.sourceLine = line;
		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::String)
		{
			node.sceneName = tokens[pos].text;
			++pos;
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief @bg "file" [transition] [duration]
	[[nodiscard]] static std::optional<ScenarioNode> parseBackground(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Background;
		node.sourceLine = line;

		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::String)
		{
			node.background.file = tokens[pos].text;
			++pos;
		}
		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Identifier)
		{
			node.background.transition = tokens[pos].text;
			++pos;
		}
		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Number)
		{
			node.background.duration = static_cast<float>(tokens[pos].numValue);
			++pos;
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief @bgm/@se/@voice "file"
	[[nodiscard]] static std::optional<ScenarioNode> parseAudio(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line,
		const std::string& audioType)
	{
		ScenarioNode node;
		if (audioType == "bgm") node.type = ScenarioCommandType::Bgm;
		else if (audioType == "se") node.type = ScenarioCommandType::Se;
		else node.type = ScenarioCommandType::Voice;
		node.sourceLine = line;
		node.audio.type = audioType;

		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::String)
		{
			node.audio.file = tokens[pos].text;
			++pos;
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief @char "name" [position] [expression] [show/hide] [transition]
	[[nodiscard]] static std::optional<ScenarioNode> parseCharacter(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Character;
		node.sourceLine = line;

		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::String)
		{
			node.character.name = tokens[pos].text;
			++pos;
		}

		// 残りのパラメータを収集
		while (pos < tokens.size() &&
			tokens[pos].type != ScenarioTokenType::Newline &&
			tokens[pos].type != ScenarioTokenType::Eof)
		{
			const auto& text = tokens[pos].text;

			if (text == "show")
			{
				node.character.show = true;
			}
			else if (text == "hide")
			{
				node.character.show = false;
			}
			else if (text == "left" || text == "center" || text == "right" ||
					 text == "far_left" || text == "far_right")
			{
				node.character.position = text;
			}
			else if (text == "fade" || text == "dissolve" || text == "slide" || text == "none")
			{
				node.character.transition = text;
			}
			else if (tokens[pos].type == ScenarioTokenType::Identifier)
			{
				// その他の識別子は表情として解釈
				if (node.character.expression.empty())
				{
					node.character.expression = text;
				}
			}
			++pos;
		}

		return node;
	}

	/// @brief @choice ... @endchoice（複数行の選択肢ブロック）
	[[nodiscard]] static std::optional<ScenarioNode> parseChoice(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Choice;
		node.sourceLine = line;

		skipToEndOfLine(tokens, pos); // @choice の行末をスキップ

		while (pos < tokens.size() && tokens[pos].type != ScenarioTokenType::Eof)
		{
			skipNewlines(tokens, pos);
			if (pos >= tokens.size()) break;

			// @endchoice で終了
			if (tokens[pos].type == ScenarioTokenType::Command && tokens[pos].text == "endchoice")
			{
				++pos;
				skipToEndOfLine(tokens, pos);
				break;
			}

			// 次の @command が来たら暗黙的に終了（@endchoice 省略対応）
			if (tokens[pos].type == ScenarioTokenType::Command && tokens[pos].text != "endchoice")
			{
				break;
			}

			// 選択肢エントリ: "text" -> label
			if (tokens[pos].type == ScenarioTokenType::String)
			{
				ScenarioChoiceEntry entry;
				entry.text = tokens[pos].text;
				++pos;

				if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Arrow)
				{
					++pos;
					if (pos < tokens.size() &&
						(tokens[pos].type == ScenarioTokenType::Identifier ||
						 tokens[pos].type == ScenarioTokenType::String))
					{
						entry.label = tokens[pos].text;
						++pos;
					}
				}
				node.choices.push_back(std::move(entry));
				skipToEndOfLine(tokens, pos);
			}
			else
			{
				skipToEndOfLine(tokens, pos);
			}
		}

		return node;
	}

	/// @brief @script ... @endscript（block directive、body は verbatim 保持）
	/// @details lexer は `@script` Command の直後に必ず `ScriptBody` トークン
	///          (body 文字列) と、続いて `@endscript` Command (matching 時のみ)
	///          を emit する。parser はそのシーケンスを `ScenarioNode::scriptBody`
	///          に格納する。`@endscript` が無い場合 (lexer が見つけられなかった場合)
	///          も `ScriptBody` 単独で来るので、ノード自体は emit する
	///          (executor 側で no-op になる)。
	[[nodiscard]] static std::optional<ScenarioNode> parseScript(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Script;
		node.sourceLine = line;

		// `@script` Command の直後は (lexer 仕様により) 改行を経由せず
		// すぐに `ScriptBody` トークンが続く。Newline トークンを emit しない
		// 設計のため `skipToEndOfLine` は呼ばずに直接 ScriptBody を期待する。
		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::ScriptBody)
		{
			node.scriptBody = tokens[pos].text;
			++pos;
		}

		// `@endscript` Command を消費 (matching 時のみ lexer が emit する)
		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Command &&
			tokens[pos].text == "endscript")
		{
			++pos;
			skipToEndOfLine(tokens, pos);
		}

		return node;
	}

	/// @brief @label name
	[[nodiscard]] static std::optional<ScenarioNode> parseLabel(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Label;
		node.sourceLine = line;

		if (pos < tokens.size() &&
			(tokens[pos].type == ScenarioTokenType::Identifier ||
			 tokens[pos].type == ScenarioTokenType::String))
		{
			node.labelName = tokens[pos].text;
			++pos;
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief @jump name
	[[nodiscard]] static std::optional<ScenarioNode> parseJump(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Jump;
		node.sourceLine = line;

		if (pos < tokens.size() &&
			(tokens[pos].type == ScenarioTokenType::Identifier ||
			 tokens[pos].type == ScenarioTokenType::String))
		{
			node.labelName = tokens[pos].text;
			++pos;
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief @set var value
	[[nodiscard]] static std::optional<ScenarioNode> parseSet(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Set;
		node.sourceLine = line;

		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Identifier)
		{
			node.setParams.variable = tokens[pos].text;
			++pos;
		}

		// 値: 残りをすべて連結
		if (pos < tokens.size() && tokens[pos].type != ScenarioTokenType::Newline &&
			tokens[pos].type != ScenarioTokenType::Eof)
		{
			if (tokens[pos].type == ScenarioTokenType::String)
			{
				node.setParams.value = tokens[pos].text;
				++pos;
			}
			else
			{
				node.setParams.value = collectRestOfLine(tokens, pos);
				return node; // collectRestOfLineが既にposを進めている
			}
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief @if condition ... @endif
	[[nodiscard]] static std::optional<ScenarioNode> parseIf(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::If;
		node.sourceLine = line;

		// 条件式を行末まで取得
		node.ifParams.condition = collectRestOfLine(tokens, pos);

		// フラットな構造で保持。@else / @endif は独立ノードとして emit され、
		// ScenarioExecutor::skipToEndif / skipToElseOrEndif が走査する。

		return node;
	}

	/// @brief @else (inside @if block)
	[[nodiscard]] static std::optional<ScenarioNode> parseElse(
		const std::vector<ScenarioToken>& /*tokens*/, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Else;
		node.sourceLine = line;
		// @else は引数を取らない。行末までスキップは呼び出し側の parseCommand が行わないので
		// ここで単純に何もしない（newline は次のループで吸収される）
		(void)pos;
		return node;
	}

	/// @brief @endif (terminates @if block)
	[[nodiscard]] static std::optional<ScenarioNode> parseEndIf(
		const std::vector<ScenarioToken>& /*tokens*/, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::EndIf;
		node.sourceLine = line;
		(void)pos;
		return node;
	}

	/// @brief @wait duration
	[[nodiscard]] static std::optional<ScenarioNode> parseWait(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Wait;
		node.sourceLine = line;

		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Number)
		{
			node.waitDuration = static_cast<float>(tokens[pos].numValue);
			++pos;
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief @transition type duration
	[[nodiscard]] static std::optional<ScenarioNode> parseTransition(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos, std::size_t line)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Transition;
		node.sourceLine = line;

		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Identifier)
		{
			node.transition.type = tokens[pos].text;
			++pos;
		}
		if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::Number)
		{
			node.transition.duration = static_cast<float>(tokens[pos].numValue);
			++pos;
		}
		skipToEndOfLine(tokens, pos);
		return node;
	}

	/// @brief 対話行をパースする: speaker "text" または "text"
	[[nodiscard]] static std::optional<ScenarioNode> parseDialogue(
		const std::vector<ScenarioToken>& tokens, std::size_t& pos)
	{
		ScenarioNode node;
		node.type = ScenarioCommandType::Dialogue;
		node.sourceLine = tokens[pos].line;

		if (tokens[pos].type == ScenarioTokenType::Identifier)
		{
			// speaker "text" 形式
			node.dialogue.speaker = tokens[pos].text;
			++pos;

			if (pos < tokens.size() && tokens[pos].type == ScenarioTokenType::String)
			{
				node.dialogue.text = tokens[pos].text;
				++pos;
			}
		}
		else if (tokens[pos].type == ScenarioTokenType::String)
		{
			// "text" のみ（ナレーション）
			node.dialogue.text = tokens[pos].text;
			++pos;
		}

		skipToEndOfLine(tokens, pos);
		return node;
	}
};

} // namespace mitiru::vn
