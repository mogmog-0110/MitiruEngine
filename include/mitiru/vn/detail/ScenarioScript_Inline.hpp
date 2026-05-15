#pragma once

// Detail header for mitiru::vn::ScenarioScript — included via vn/ScenarioScript.hpp
// InlineTag / parseInlineTags / parseScenario をまとめたファイル。

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScenarioScript_Types.hpp"
#include "ScenarioScript_Lexer.hpp"
#include "ScenarioScript_Parser.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  インラインタグ (対話テキスト内の "[wait=500]" 等)
// ════════════════════════════════════════════════════════════════════

/// @brief 対話テキスト内のインラインタグ
struct InlineTag
{
	std::string name;		///< タグ名
	std::string value;		///< タグ値（空の場合あり）
};

/// @brief 対話行のテキスト内インラインコマンドを解析する
/// @details "[wait=500]" や "[shake]" などのインラインタグを分離する。
///          `ScenarioExecutor` が各 dialogue 行で自動実行し、
///          `onDialogueParsed(speaker, plainText, tags)` として
///          コールバックに届ける。
/// @param text 対話テキスト
/// @return {タグ除去後テキスト, タグリスト} のペア
[[nodiscard]] inline std::pair<std::string, std::vector<InlineTag>> parseInlineTags(std::string_view text);

// ════════════════════════════════════════════════════════════════════
//  便利関数
// ════════════════════════════════════════════════════════════════════

/// @brief スクリプト文字列をパースしてコマンドリストを返す
/// @param source スクリプト文字列
/// @return ScenarioNodeのベクタ
[[nodiscard]] inline std::vector<ScenarioNode> parseScenario(std::string_view source)
{
	auto tokens = ScenarioLexer::tokenize(source);
	return ScenarioParser::parse(tokens);
}

// 上で前方宣言した parseInlineTags の定義
[[nodiscard]] inline std::pair<std::string, std::vector<InlineTag>> parseInlineTags(std::string_view text)
{
	std::string cleanText;
	std::vector<InlineTag> tags;

	std::size_t i = 0;
	while (i < text.size())
	{
		if (text[i] == '[')
		{
			auto closePos = text.find(']', i);
			if (closePos == std::string_view::npos)
			{
				cleanText += text[i];
				++i;
				continue;
			}

			auto tagContent = text.substr(i + 1, closePos - i - 1);
			InlineTag tag;

			auto eqPos = tagContent.find('=');
			if (eqPos != std::string_view::npos)
			{
				tag.name = std::string(tagContent.substr(0, eqPos));
				tag.value = std::string(tagContent.substr(eqPos + 1));
			}
			else
			{
				tag.name = std::string(tagContent);
			}

			tags.push_back(std::move(tag));
			i = closePos + 1;
		}
		else
		{
			cleanText += text[i];
			++i;
		}
	}

	return {cleanText, tags};
}

} // namespace mitiru::vn
