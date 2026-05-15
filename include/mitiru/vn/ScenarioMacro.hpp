#pragma once

/// @file ScenarioMacro.hpp
/// @brief VNシナリオDSL用マクロ・サブルーチンシステム
/// @details @macro/@endmacro でコマンドブロックを定義し、@call で呼び出す。
///          マクロ本体内では $param 形式の引数置換が行われる。
///          ネストされたマクロ呼び出しもサポートする。
///
/// スクリプト書式例:
/// @code
/// @macro enter_character name position
///   @char $name $position normal show fade
///   @wait 0.3
/// @endmacro
///
/// @call enter_character "sakura" center
/// @call enter_character "rin" left
/// @endcode

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ScenarioScript.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  マクロ定義
// ════════════════════════════════════════════════════════════════════

/// @brief マクロ定義（名前、パラメータリスト、本体トークン列）
struct MacroDefinition
{
	std::string name;							///< マクロ名
	std::vector<std::string> params;			///< パラメータ名リスト
	std::vector<ScenarioToken> body;			///< 本体トークン列（展開前）
};

// ════════════════════════════════════════════════════════════════════
//  マクロレジストリ
// ════════════════════════════════════════════════════════════════════

/// @brief マクロの登録・検索・管理
/// @details マクロ名をキーとして MacroDefinition を保持する。
///
/// @code
/// mitiru::vn::MacroRegistry registry;
///
/// mitiru::vn::MacroDefinition def;
/// def.name = "enter_character";
/// def.params = {"name", "position"};
/// def.body = { /* tokens */ };
/// registry.registerMacro(def);
///
/// auto* m = registry.find("enter_character");
/// @endcode
class MacroRegistry
{
public:
	/// @brief マクロを登録する
	/// @param definition マクロ定義
	void registerMacro(MacroDefinition definition)
	{
		auto name = definition.name;
		m_macros[name] = std::move(definition);
	}

	/// @brief マクロを検索する
	/// @param name マクロ名
	/// @return マクロ定義へのポインタ（見つからなければnullptr）
	[[nodiscard]] const MacroDefinition* find(const std::string& name) const
	{
		auto it = m_macros.find(name);
		if (it != m_macros.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	/// @brief マクロが登録されているか確認する
	/// @param name マクロ名
	/// @return 登録されていればtrue
	[[nodiscard]] bool has(const std::string& name) const
	{
		return m_macros.count(name) > 0;
	}

	/// @brief マクロを削除する
	/// @param name マクロ名
	void remove(const std::string& name)
	{
		m_macros.erase(name);
	}

	/// @brief 全マクロをクリアする
	void clear()
	{
		m_macros.clear();
	}

	/// @brief 登録マクロ数を取得する
	/// @return マクロ数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_macros.size();
	}

	/// @brief 登録されたマクロ名のリストを取得する
	/// @return マクロ名のベクタ
	[[nodiscard]] std::vector<std::string> names() const
	{
		std::vector<std::string> result;
		result.reserve(m_macros.size());
		for (const auto& [name, def] : m_macros)
		{
			result.push_back(name);
		}
		return result;
	}

private:
	std::unordered_map<std::string, MacroDefinition> m_macros;
};

// ════════════════════════════════════════════════════════════════════
//  マクロプリプロセッサ
// ════════════════════════════════════════════════════════════════════

/// @brief マクロの抽出・展開を行うプリプロセッサ
/// @details トークン列から @macro/@endmacro ブロックを抽出してレジストリに登録し、
///          @call をマクロ本体に展開する。展開後のトークン列は通常のパーサーに渡せる。
///
/// @code
/// auto tokens = mitiru::vn::ScenarioLexer::tokenize(script);
/// mitiru::vn::MacroRegistry registry;
/// auto expanded = mitiru::vn::MacroPreprocessor::process(tokens, registry);
/// auto nodes = mitiru::vn::ScenarioParser::parse(expanded);
/// @endcode
class MacroPreprocessor
{
public:
	/// @brief マクロの抽出と展開を一括で行う
	/// @param tokens 入力トークン列
	/// @param registry マクロレジストリ（抽出されたマクロが登録される）
	/// @param maxDepth ネスト展開の最大深度（無限再帰防止）
	/// @return 展開後のトークン列
	[[nodiscard]] static std::vector<ScenarioToken> process(
		const std::vector<ScenarioToken>& tokens,
		MacroRegistry& registry,
		std::size_t maxDepth = 32)
	{
		// Phase 1: @macro ... @endmacro ブロックを抽出してレジストリに登録
		auto stripped = extractMacros(tokens, registry);

		// Phase 2: @call をマクロ本体で展開（ネスト対応）
		return expandMacros(stripped, registry, maxDepth);
	}

	/// @brief トークン列から @macro/@endmacro を抽出してレジストリに登録する
	/// @param tokens 入力トークン列
	/// @param registry マクロレジストリ
	/// @return マクロ定義を除去したトークン列
	[[nodiscard]] static std::vector<ScenarioToken> extractMacros(
		const std::vector<ScenarioToken>& tokens,
		MacroRegistry& registry)
	{
		std::vector<ScenarioToken> output;
		std::size_t i = 0;

		while (i < tokens.size())
		{
			// @macro name param1 param2 ...
			if (tokens[i].type == ScenarioTokenType::Command && tokens[i].text == "macro")
			{
				MacroDefinition def;
				++i;

				// マクロ名
				if (i < tokens.size() &&
					(tokens[i].type == ScenarioTokenType::Identifier ||
					 tokens[i].type == ScenarioTokenType::String))
				{
					def.name = tokens[i].text;
					++i;
				}
				else
				{
					// 名前がない場合はスキップ
					skipToEndmacro(tokens, i);
					continue;
				}

				// パラメータリスト（行末まで）
				while (i < tokens.size() &&
					tokens[i].type != ScenarioTokenType::Newline &&
					tokens[i].type != ScenarioTokenType::Eof)
				{
					if (tokens[i].type == ScenarioTokenType::Identifier)
					{
						def.params.push_back(tokens[i].text);
					}
					++i;
				}

				// 改行をスキップ
				if (i < tokens.size() && tokens[i].type == ScenarioTokenType::Newline)
				{
					++i;
				}

				// @endmacro まで本体を収集
				while (i < tokens.size())
				{
					if (tokens[i].type == ScenarioTokenType::Command && tokens[i].text == "endmacro")
					{
						++i;
						// 行末までスキップ
						while (i < tokens.size() &&
							tokens[i].type != ScenarioTokenType::Newline &&
							tokens[i].type != ScenarioTokenType::Eof)
						{
							++i;
						}
						if (i < tokens.size() && tokens[i].type == ScenarioTokenType::Newline)
						{
							++i;
						}
						break;
					}
					def.body.push_back(tokens[i]);
					++i;
				}

				// 本体末尾の余分な改行を除去
				while (!def.body.empty() && def.body.back().type == ScenarioTokenType::Newline)
				{
					def.body.pop_back();
				}

				registry.registerMacro(std::move(def));
			}
			else
			{
				output.push_back(tokens[i]);
				++i;
			}
		}

		return output;
	}

	/// @brief @call をマクロ本体で展開する
	/// @param tokens 入力トークン列（マクロ定義除去済み）
	/// @param registry マクロレジストリ
	/// @param maxDepth ネスト展開の最大深度
	/// @return 展開後のトークン列
	[[nodiscard]] static std::vector<ScenarioToken> expandMacros(
		const std::vector<ScenarioToken>& tokens,
		const MacroRegistry& registry,
		std::size_t maxDepth = 32)
	{
		return expandMacrosRecursive(tokens, registry, 0, maxDepth);
	}

private:
	/// @brief @endmacro までスキップする（エラー回復用）
	static void skipToEndmacro(const std::vector<ScenarioToken>& tokens, std::size_t& i)
	{
		while (i < tokens.size())
		{
			if (tokens[i].type == ScenarioTokenType::Command && tokens[i].text == "endmacro")
			{
				++i;
				// 行末までスキップ
				while (i < tokens.size() &&
					tokens[i].type != ScenarioTokenType::Newline &&
					tokens[i].type != ScenarioTokenType::Eof)
				{
					++i;
				}
				if (i < tokens.size() && tokens[i].type == ScenarioTokenType::Newline)
				{
					++i;
				}
				return;
			}
			++i;
		}
	}

	/// @brief 再帰的にマクロ展開を行う
	[[nodiscard]] static std::vector<ScenarioToken> expandMacrosRecursive(
		const std::vector<ScenarioToken>& tokens,
		const MacroRegistry& registry,
		std::size_t depth,
		std::size_t maxDepth)
	{
		if (depth >= maxDepth)
		{
			// 深度上限に達した場合はそのまま返す
			return tokens;
		}

		std::vector<ScenarioToken> output;
		std::size_t i = 0;

		while (i < tokens.size())
		{
			// @call macro_name arg1 arg2 ...
			if (tokens[i].type == ScenarioTokenType::Command && tokens[i].text == "call")
			{
				++i;

				// マクロ名を取得
				std::string macroName;
				if (i < tokens.size() &&
					(tokens[i].type == ScenarioTokenType::Identifier ||
					 tokens[i].type == ScenarioTokenType::String))
				{
					macroName = tokens[i].text;
					++i;
				}
				else
				{
					// マクロ名がない: 行末までスキップ
					while (i < tokens.size() &&
						tokens[i].type != ScenarioTokenType::Newline &&
						tokens[i].type != ScenarioTokenType::Eof)
					{
						++i;
					}
					continue;
				}

				// 引数を収集（行末まで）
				std::vector<std::string> args;
				while (i < tokens.size() &&
					tokens[i].type != ScenarioTokenType::Newline &&
					tokens[i].type != ScenarioTokenType::Eof)
				{
					args.push_back(tokens[i].text);
					++i;
				}

				// マクロを検索
				const auto* macro = registry.find(macroName);
				if (macro != nullptr)
				{
					// パラメータ→引数のマッピングを構築
					std::unordered_map<std::string, std::string> substitutions;
					for (std::size_t p = 0; p < macro->params.size(); ++p)
					{
						if (p < args.size())
						{
							substitutions[macro->params[p]] = args[p];
						}
						else
						{
							substitutions[macro->params[p]] = "";
						}
					}

					// 本体を引数置換して展開
					auto expanded = substituteParams(macro->body, substitutions);

					// 展開された本体の前後に改行を挿入
					if (!output.empty() && output.back().type != ScenarioTokenType::Newline)
					{
						output.push_back({ScenarioTokenType::Newline, "\n", 0.0,
							tokens[i > 0 ? i - 1 : 0].line});
					}

					// ネストされた @call を再帰展開
					auto fullyExpanded = expandMacrosRecursive(expanded, registry, depth + 1, maxDepth);

					for (auto& tok : fullyExpanded)
					{
						output.push_back(std::move(tok));
					}

					// 展開後の改行を追加
					if (!output.empty() && output.back().type != ScenarioTokenType::Newline)
					{
						output.push_back({ScenarioTokenType::Newline, "\n", 0.0,
							tokens[i > 0 ? i - 1 : 0].line});
					}
				}
				else
				{
					// 未定義マクロ: @call 行をそのまま無視（改行だけ出す）
					if (!output.empty() && output.back().type != ScenarioTokenType::Newline)
					{
						output.push_back({ScenarioTokenType::Newline, "\n", 0.0,
							tokens[i > 0 ? i - 1 : 0].line});
					}
				}
			}
			else
			{
				output.push_back(tokens[i]);
				++i;
			}
		}

		return output;
	}

	/// @brief マクロ本体のトークン列に対してパラメータ置換を行う
	/// @param body 本体トークン列
	/// @param substitutions パラメータ名→引数値のマッピング
	/// @return 置換後のトークン列
	[[nodiscard]] static std::vector<ScenarioToken> substituteParams(
		const std::vector<ScenarioToken>& body,
		const std::unordered_map<std::string, std::string>& substitutions)
	{
		std::vector<ScenarioToken> result;
		result.reserve(body.size());

		for (const auto& tok : body)
		{
			// $paramName パターンの検出と置換
			if (tok.type == ScenarioTokenType::Identifier && !tok.text.empty())
			{
				// 識別子内の $param を置換
				auto substituted = substituteInText(tok.text, substitutions);
				if (substituted != tok.text)
				{
					auto newTok = tok;
					newTok.text = std::move(substituted);
					result.push_back(std::move(newTok));
					continue;
				}
			}
			else if (tok.type == ScenarioTokenType::String)
			{
				// 文字列リテラル内の $param も置換
				auto substituted = substituteInText(tok.text, substitutions);
				if (substituted != tok.text)
				{
					auto newTok = tok;
					newTok.text = std::move(substituted);
					result.push_back(std::move(newTok));
					continue;
				}
			}

			result.push_back(tok);
		}

		return result;
	}

	/// @brief テキスト内の $paramName を置換する
	/// @param text 入力テキスト
	/// @param substitutions パラメータ名→引数値のマッピング
	/// @return 置換後のテキスト
	[[nodiscard]] static std::string substituteInText(
		const std::string& text,
		const std::unordered_map<std::string, std::string>& substitutions)
	{
		std::string result;
		std::size_t i = 0;

		while (i < text.size())
		{
			if (text[i] == '$')
			{
				++i;
				std::size_t start = i;
				while (i < text.size() &&
					(std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
				{
					++i;
				}

				if (i > start)
				{
					auto paramName = text.substr(start, i - start);
					auto it = substitutions.find(paramName);
					if (it != substitutions.end())
					{
						result += it->second;
					}
					else
					{
						// 未知のパラメータ: そのまま残す
						result += '$';
						result += paramName;
					}
				}
				else
				{
					result += '$';
				}
			}
			else
			{
				result += text[i];
				++i;
			}
		}

		return result;
	}
};

// ════════════════════════════════════════════════════════════════════
//  便利関数
// ════════════════════════════════════════════════════════════════════

/// @brief マクロ展開済みのシナリオをパースする
/// @param source スクリプト文字列（マクロ定義・呼び出しを含む）
/// @return ScenarioNodeのベクタ
/// @details マクロの抽出・展開・パースを一括で行う便利関数。
///
/// @code
/// auto nodes = mitiru::vn::parseScenarioWithMacros(R"(
///   @macro greet name
///     $name "こんにちは！"
///   @endmacro
///   @call greet sakura
/// )");
/// @endcode
[[nodiscard]] inline std::vector<ScenarioNode> parseScenarioWithMacros(std::string_view source)
{
	auto tokens = ScenarioLexer::tokenize(source);
	MacroRegistry registry;
	auto expanded = MacroPreprocessor::process(tokens, registry);
	return ScenarioParser::parse(expanded);
}

/// @brief マクロ展開済みのシナリオをパースする（レジストリ付き）
/// @param source スクリプト文字列
/// @param registry 事前登録済みマクロレジストリ（新規マクロも追加される）
/// @return ScenarioNodeのベクタ
[[nodiscard]] inline std::vector<ScenarioNode> parseScenarioWithMacros(
	std::string_view source, MacroRegistry& registry)
{
	auto tokens = ScenarioLexer::tokenize(source);
	auto expanded = MacroPreprocessor::process(tokens, registry);
	return ScenarioParser::parse(expanded);
}

} // namespace mitiru::vn
