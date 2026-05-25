#pragma once

// mitiru::vn::ScenarioScript の detail ヘッダ — vn/ScenarioScript.hpp 経由で include される

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScenarioScript_Types.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  字句解析器
// ════════════════════════════════════════════════════════════════════

/// @brief シナリオスクリプトの字句解析器
/// @details テキストベースのVNスクリプトをトークン列に分解する。
class ScenarioLexer
{
public:
	/// @brief ソース文字列をトークン列に分解する
	/// @param source ソース文字列
	/// @return トークン列
	[[nodiscard]] static std::vector<ScenarioToken> tokenize(std::string_view source)
	{
		std::vector<ScenarioToken> tokens;
		std::size_t i = 0;
		std::size_t line = 1;

		while (i < source.size())
		{
			// 改行
			if (source[i] == '\n')
			{
				tokens.push_back({ScenarioTokenType::Newline, "\n", 0.0, line});
				++line;
				++i;
				continue;
			}

			// キャリッジリターン
			if (source[i] == '\r')
			{
				++i;
				continue;
			}

			// 空白・タブ（改行以外）
			if (source[i] == ' ' || source[i] == '\t')
			{
				++i;
				continue;
			}

			// コメント: # または //
			if (source[i] == '#' || (i + 1 < source.size() && source[i] == '/' && source[i + 1] == '/'))
			{
				while (i < source.size() && source[i] != '\n')
				{
					++i;
				}
				continue;
			}

			// コマンド: @identifier
			if (source[i] == '@')
			{
				++i;
				std::size_t start = i;
				while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_'))
				{
					++i;
				}
				auto cmdName = std::string(source.substr(start, i - start));
				const auto cmdLine = line;
				tokens.push_back({ScenarioTokenType::Command, cmdName, 0.0, cmdLine});

				// `@script` は body を verbatim 保持する block directive として
				// 専用処理する。@choice 等の通常 block 系と異なり、body 中の
				// "@" を識別子としてトークン化されたら困る (Lua の @ 演算子等)
				// ので lexer 段階で @endscript までを単一 ScriptBody トークン化する。
				if (cmdName == "script")
				{
					// `@script` トークンの後ろから次の改行までは引数なしとして消費する。
					// 例: `@script` の後ろに余計な識別子があっても黙殺する (将来拡張余地)
					std::size_t scanPos = i;
					while (scanPos < source.size() && source[scanPos] != '\n')
					{
						++scanPos;
					}
					if (scanPos < source.size())
					{
						// 改行を含めて body 開始位置の手前まで進める。改行自体は body から除外。
						++scanPos;
						++line;
					}

					// `@endscript` を探索する。改行直後または行頭から `@endscript`
					// が現れるまでを body として保持する。改行は verbatim で含む。
					const std::size_t bodyStart = scanPos;
					std::size_t bodyEnd = scanPos;
					std::size_t cursor = scanPos;
					bool foundEnd = false;

					while (cursor < source.size())
					{
						// 行頭判定: cursor が source 先頭か直前が改行のとき
						const bool atLineStart = (cursor == 0 || source[cursor - 1] == '\n');

						// 行頭の余白 (空白 / タブ) は許容してから @endscript をチェック
						std::size_t probe = cursor;
						if (atLineStart)
						{
							while (probe < source.size() &&
								(source[probe] == ' ' || source[probe] == '\t'))
							{
								++probe;
							}
							const std::string_view kEnd = "@endscript";
							if (probe + kEnd.size() <= source.size() &&
								source.substr(probe, kEnd.size()) == kEnd)
							{
								// `@endscript` の直前まで (= 行頭余白を除いた cursor 位置) を body 終端とする
								bodyEnd = cursor;
								// `@endscript` 直後まで cursor を進める
								cursor = probe + kEnd.size();
								foundEnd = true;
								break;
							}
						}

						if (source[cursor] == '\n')
						{
							++line;
						}
						++cursor;
					}

					if (foundEnd)
					{
						// body は [bodyStart, bodyEnd) の verbatim 文字列
						std::string body(source.substr(bodyStart, bodyEnd - bodyStart));
						tokens.push_back({
							ScenarioTokenType::ScriptBody,
							std::move(body),
							0.0, cmdLine});

						// `@endscript` を独立した Command トークンとして emit する
						// (parser 側で「@script の後ろに必ず ScriptBody → @endscript が並ぶ」前提を立てるため)
						tokens.push_back({
							ScenarioTokenType::Command,
							"endscript",
							0.0, line});

						i = cursor;
					}
					else
					{
						// `@endscript` が見つからない → 残りの入力すべてを body として保持し、
						// parser 側でエラーノードとしてマークする。i は終端まで進める。
						std::string body(source.substr(bodyStart));
						tokens.push_back({
							ScenarioTokenType::ScriptBody,
							std::move(body),
							0.0, cmdLine});
						i = source.size();
					}
					continue;
				}
				continue;
			}

			// 矢印: ->
			if (i + 1 < source.size() && source[i] == '-' && source[i + 1] == '>')
			{
				tokens.push_back({ScenarioTokenType::Arrow, "->", 0.0, line});
				i += 2;
				continue;
			}

			// 文字列リテラル: "..."
			if (source[i] == '"')
			{
				++i;
				std::string text;
				while (i < source.size() && source[i] != '"')
				{
					if (source[i] == '\\' && i + 1 < source.size())
					{
						++i;
						switch (source[i])
						{
						case 'n':  text += '\n'; break;
						case 't':  text += '\t'; break;
						case '"':  text += '"'; break;
						case '\\': text += '\\'; break;
						default:   text += source[i]; break;
						}
					}
					else
					{
						text += source[i];
					}
					++i;
				}
				if (i < source.size()) ++i; // 閉じ引用符
				tokens.push_back({ScenarioTokenType::String, std::move(text), 0.0, line});
				continue;
			}

			// 数値リテラル
			if (std::isdigit(static_cast<unsigned char>(source[i])) ||
				(source[i] == '-' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1]))))
			{
				std::size_t start = i;
				if (source[i] == '-') ++i;
				while (i < source.size() && (std::isdigit(static_cast<unsigned char>(source[i])) || source[i] == '.'))
				{
					++i;
				}
				auto text = std::string(source.substr(start, i - start));
				double val = std::stod(text);
				tokens.push_back({ScenarioTokenType::Number, std::move(text), val, line});
				continue;
			}

			// 識別子（変数名、ラベル名、演算子キーワードなど）
			if (std::isalpha(static_cast<unsigned char>(source[i])) || source[i] == '_')
			{
				std::size_t start = i;
				while (i < source.size() &&
					(std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_'))
				{
					++i;
				}
				tokens.push_back({
					ScenarioTokenType::Identifier,
					std::string(source.substr(start, i - start)),
					0.0, line});
				continue;
			}

			// 不明な文字: 比較演算子等を識別子的に扱う
			if (source[i] == '=' || source[i] == '!' || source[i] == '<' || source[i] == '>' ||
				source[i] == '&' || source[i] == '|')
			{
				std::size_t start = i;
				while (i < source.size() &&
					(source[i] == '=' || source[i] == '!' || source[i] == '<' || source[i] == '>' ||
					 source[i] == '&' || source[i] == '|'))
				{
					++i;
				}
				tokens.push_back({
					ScenarioTokenType::Identifier,
					std::string(source.substr(start, i - start)),
					0.0, line});
				continue;
			}

			// その他の文字はスキップ
			++i;
		}

		tokens.push_back({ScenarioTokenType::Eof, "", 0.0, line});
		return tokens;
	}
};

} // namespace mitiru::vn
