#pragma once

/// @file ExpressionEvaluator.hpp
/// @brief VNシナリオ向け拡張式評価エンジン
/// @details 算術演算、文字列操作、比較、論理演算、組み込み関数をサポートする
///          再帰下降パーサーベースの式評価器。FlagManagerと統合して変数参照を解決する。
///
/// 対応する式の例:
/// @code
/// $affinity_sakura > 5 && $chapter >= 2
/// $score + 10
/// min($hp, 100)
/// strlen($player_name) > 0
/// $greeting + " " + $player_name
/// random(1, 6)
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "FlagManager.hpp"

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  式評価の結果型
// ════════════════════════════════════════════════════════════════════

/// @brief 式評価結果の型（bool, int, float, string）
using ExpressionResult = std::variant<bool, int, float, std::string>;

// ════════════════════════════════════════════════════════════════════
//  式トークン
// ════════════════════════════════════════════════════════════════════

/// @brief 式トークンの種別
enum class ExprTokenKind
{
	IntLiteral,			///< 整数リテラル
	FloatLiteral,		///< 浮動小数点リテラル
	StringLiteral,		///< 文字列リテラル "..."
	BoolLiteral,		///< true / false
	Variable,			///< $varName
	Identifier,			///< 変数名（$なし）または関数名
	Plus,				///< +
	Minus,				///< -
	Star,				///< *
	Slash,				///< /
	Percent,			///< %
	EqEq,				///< ==
	NotEq,				///< !=
	Less,				///< <
	LessEq,			///< <=
	Greater,			///< >
	GreaterEq,			///< >=
	And,				///< &&
	Or,					///< ||
	Not,				///< !
	LParen,				///< (
	RParen,				///< )
	Comma,				///< ,
	Eof,				///< 終端
};

/// @brief 式トークン
struct ExprToken
{
	ExprTokenKind kind = ExprTokenKind::Eof;
	std::string text;
	double numValue = 0.0;
};

// ════════════════════════════════════════════════════════════════════
//  式レキサー
// ════════════════════════════════════════════════════════════════════

/// @brief 式の字句解析器
class ExpressionLexer
{
public:
	/// @brief 式文字列をトークン列に分解する
	/// @param source 式文字列
	/// @return トークン列
	[[nodiscard]] static std::vector<ExprToken> tokenize(std::string_view source)
	{
		std::vector<ExprToken> tokens;
		std::size_t i = 0;

		while (i < source.size())
		{
			// 空白スキップ
			if (std::isspace(static_cast<unsigned char>(source[i])))
			{
				++i;
				continue;
			}

			// 文字列リテラル
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
				tokens.push_back({ExprTokenKind::StringLiteral, std::move(text), 0.0});
				continue;
			}

			// 変数参照: $varName
			if (source[i] == '$')
			{
				++i;
				std::size_t start = i;
				while (i < source.size() &&
					(std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_'))
				{
					++i;
				}
				tokens.push_back({ExprTokenKind::Variable,
					std::string(source.substr(start, i - start)), 0.0});
				continue;
			}

			// 2文字演算子
			if (i + 1 < source.size())
			{
				auto two = source.substr(i, 2);
				if (two == "==") { tokens.push_back({ExprTokenKind::EqEq, "==", 0.0}); i += 2; continue; }
				if (two == "!=") { tokens.push_back({ExprTokenKind::NotEq, "!=", 0.0}); i += 2; continue; }
				if (two == "<=") { tokens.push_back({ExprTokenKind::LessEq, "<=", 0.0}); i += 2; continue; }
				if (two == ">=") { tokens.push_back({ExprTokenKind::GreaterEq, ">=", 0.0}); i += 2; continue; }
				if (two == "&&") { tokens.push_back({ExprTokenKind::And, "&&", 0.0}); i += 2; continue; }
				if (two == "||") { tokens.push_back({ExprTokenKind::Or, "||", 0.0}); i += 2; continue; }
			}

			// 1文字演算子
			if (source[i] == '+') { tokens.push_back({ExprTokenKind::Plus, "+", 0.0}); ++i; continue; }
			if (source[i] == '-')
			{
				// 単項マイナスか二項マイナスか: 前のトークンが値でなければ単項
				// ここではトークン化のみなので常にMinusとして出す（パーサーが判断）
				tokens.push_back({ExprTokenKind::Minus, "-", 0.0});
				++i;
				continue;
			}
			if (source[i] == '*') { tokens.push_back({ExprTokenKind::Star, "*", 0.0}); ++i; continue; }
			if (source[i] == '/') { tokens.push_back({ExprTokenKind::Slash, "/", 0.0}); ++i; continue; }
			if (source[i] == '%') { tokens.push_back({ExprTokenKind::Percent, "%", 0.0}); ++i; continue; }
			if (source[i] == '!') { tokens.push_back({ExprTokenKind::Not, "!", 0.0}); ++i; continue; }
			if (source[i] == '(') { tokens.push_back({ExprTokenKind::LParen, "(", 0.0}); ++i; continue; }
			if (source[i] == ')') { tokens.push_back({ExprTokenKind::RParen, ")", 0.0}); ++i; continue; }
			if (source[i] == ',') { tokens.push_back({ExprTokenKind::Comma, ",", 0.0}); ++i; continue; }
			if (source[i] == '<') { tokens.push_back({ExprTokenKind::Less, "<", 0.0}); ++i; continue; }
			if (source[i] == '>') { tokens.push_back({ExprTokenKind::Greater, ">", 0.0}); ++i; continue; }

			// 数値リテラル
			if (std::isdigit(static_cast<unsigned char>(source[i])))
			{
				std::size_t start = i;
				bool hasDot = false;
				while (i < source.size() &&
					(std::isdigit(static_cast<unsigned char>(source[i])) || source[i] == '.'))
				{
					if (source[i] == '.') hasDot = true;
					++i;
				}
				auto text = std::string(source.substr(start, i - start));
				double val = std::stod(text);
				auto kind = hasDot ? ExprTokenKind::FloatLiteral : ExprTokenKind::IntLiteral;
				tokens.push_back({kind, std::move(text), val});
				continue;
			}

			// 識別子 / true / false
			if (std::isalpha(static_cast<unsigned char>(source[i])) || source[i] == '_')
			{
				std::size_t start = i;
				while (i < source.size() &&
					(std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_'))
				{
					++i;
				}
				auto text = std::string(source.substr(start, i - start));
				if (text == "true" || text == "false")
				{
					tokens.push_back({ExprTokenKind::BoolLiteral, std::move(text), 0.0});
				}
				else
				{
					tokens.push_back({ExprTokenKind::Identifier, std::move(text), 0.0});
				}
				continue;
			}

			// 不明な文字はスキップ
			++i;
		}

		tokens.push_back({ExprTokenKind::Eof, "", 0.0});
		return tokens;
	}
};

// ════════════════════════════════════════════════════════════════════
//  式評価器
// ════════════════════════════════════════════════════════════════════

/// @brief 式評価器（再帰下降パーサー）
/// @details 演算子優先順位:
///   1. || (論理OR)
///   2. && (論理AND)
///   3. ==, !=, <, <=, >, >= (比較)
///   4. +, - (加算・減算 / 文字列連結)
///   5. *, /, % (乗算・除算・剰余)
///   6. 単項 !, - (否定・符号反転)
///   7. 関数呼び出し、リテラル、変数、括弧
///
/// @code
/// mitiru::vn::FlagManager flags;
/// flags.set("score", 42);
/// flags.set("name", std::string("Sakura"));
///
/// mitiru::vn::ExpressionEvaluator eval;
/// auto result = eval.evaluate("$score + 10", flags);
/// // result == int(52)
///
/// auto cond = eval.evaluateBool("$score > 30 && $score < 100", flags);
/// // cond == true
/// @endcode
class ExpressionEvaluator
{
public:
	/// @brief 式を評価して結果を返す
	/// @param expression 式文字列
	/// @param flags 変数解決用のFlagManager
	/// @return 評価結果
	[[nodiscard]] ExpressionResult evaluate(std::string_view expression, const FlagManager& flags) const
	{
		auto tokens = ExpressionLexer::tokenize(expression);
		std::size_t pos = 0;
		return parseOrExpr(tokens, pos, flags);
	}

	/// @brief 式をbool値として評価する
	/// @param expression 式文字列
	/// @param flags 変数解決用のFlagManager
	/// @return bool評価結果
	[[nodiscard]] bool evaluateBool(std::string_view expression, const FlagManager& flags) const
	{
		auto result = evaluate(expression, flags);
		return resultToBool(result);
	}

	/// @brief 式をint値として評価する
	/// @param expression 式文字列
	/// @param flags 変数解決用のFlagManager
	/// @return int評価結果
	[[nodiscard]] int evaluateInt(std::string_view expression, const FlagManager& flags) const
	{
		auto result = evaluate(expression, flags);
		return resultToInt(result);
	}

	/// @brief 式をfloat値として評価する
	/// @param expression 式文字列
	/// @param flags 変数解決用のFlagManager
	/// @return float評価結果
	[[nodiscard]] float evaluateFloat(std::string_view expression, const FlagManager& flags) const
	{
		auto result = evaluate(expression, flags);
		return resultToFloat(result);
	}

	/// @brief 式をstring値として評価する
	/// @param expression 式文字列
	/// @param flags 変数解決用のFlagManager
	/// @return string評価結果
	[[nodiscard]] std::string evaluateString(std::string_view expression, const FlagManager& flags) const
	{
		auto result = evaluate(expression, flags);
		return resultToString(result);
	}

	// ── 型変換ユーティリティ ─────────────────────────────────

	/// @brief ExpressionResultをboolに変換する
	[[nodiscard]] static bool resultToBool(const ExpressionResult& val)
	{
		if (auto* b = std::get_if<bool>(&val)) return *b;
		if (auto* i = std::get_if<int>(&val)) return *i != 0;
		if (auto* f = std::get_if<float>(&val)) return *f != 0.0f;
		if (auto* s = std::get_if<std::string>(&val)) return !s->empty();
		return false;
	}

	/// @brief ExpressionResultをintに変換する
	[[nodiscard]] static int resultToInt(const ExpressionResult& val)
	{
		if (auto* i = std::get_if<int>(&val)) return *i;
		if (auto* b = std::get_if<bool>(&val)) return *b ? 1 : 0;
		if (auto* f = std::get_if<float>(&val)) return static_cast<int>(*f);
		if (auto* s = std::get_if<std::string>(&val))
		{
			try { return std::stoi(*s); } catch (...) { return 0; }
		}
		return 0;
	}

	/// @brief ExpressionResultをfloatに変換する
	[[nodiscard]] static float resultToFloat(const ExpressionResult& val)
	{
		if (auto* f = std::get_if<float>(&val)) return *f;
		if (auto* i = std::get_if<int>(&val)) return static_cast<float>(*i);
		if (auto* b = std::get_if<bool>(&val)) return *b ? 1.0f : 0.0f;
		if (auto* s = std::get_if<std::string>(&val))
		{
			try { return std::stof(*s); } catch (...) { return 0.0f; }
		}
		return 0.0f;
	}

	/// @brief ExpressionResultをstringに変換する
	[[nodiscard]] static std::string resultToString(const ExpressionResult& val)
	{
		if (auto* s = std::get_if<std::string>(&val)) return *s;
		if (auto* b = std::get_if<bool>(&val)) return *b ? "true" : "false";
		if (auto* i = std::get_if<int>(&val)) return std::to_string(*i);
		if (auto* f = std::get_if<float>(&val))
		{
			std::ostringstream oss;
			oss << *f;
			return oss.str();
		}
		return "";
	}

	/// @brief ExpressionResultをFlagValueに変換する
	[[nodiscard]] static FlagValue resultToFlagValue(const ExpressionResult& val)
	{
		if (auto* b = std::get_if<bool>(&val)) return *b;
		if (auto* i = std::get_if<int>(&val)) return *i;
		if (auto* f = std::get_if<float>(&val)) return *f;
		if (auto* s = std::get_if<std::string>(&val)) return *s;
		return false;
	}

	/// @brief FlagValueをExpressionResultに変換する
	[[nodiscard]] static ExpressionResult flagValueToResult(const FlagValue& val)
	{
		if (auto* b = std::get_if<bool>(&val)) return *b;
		if (auto* i = std::get_if<int>(&val)) return *i;
		if (auto* f = std::get_if<float>(&val)) return *f;
		if (auto* s = std::get_if<std::string>(&val)) return *s;
		return false;
	}

private:
	// ── 再帰下降パーサー ─────────────────────────────────────

	/// @brief OR式: and_expr (|| and_expr)*
	[[nodiscard]] ExpressionResult parseOrExpr(
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		auto result = parseAndExpr(tokens, pos, flags);

		while (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::Or)
		{
			++pos;
			auto rhs = parseAndExpr(tokens, pos, flags);
			result = ExpressionResult{resultToBool(result) || resultToBool(rhs)};
		}

		return result;
	}

	/// @brief AND式: comparison (&& comparison)*
	[[nodiscard]] ExpressionResult parseAndExpr(
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		auto result = parseComparison(tokens, pos, flags);

		while (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::And)
		{
			++pos;
			auto rhs = parseComparison(tokens, pos, flags);
			result = ExpressionResult{resultToBool(result) && resultToBool(rhs)};
		}

		return result;
	}

	/// @brief 比較式: additive (op additive)?
	[[nodiscard]] ExpressionResult parseComparison(
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		auto lhs = parseAdditive(tokens, pos, flags);

		if (pos < tokens.size())
		{
			auto kind = tokens[pos].kind;
			if (kind == ExprTokenKind::EqEq || kind == ExprTokenKind::NotEq ||
				kind == ExprTokenKind::Less || kind == ExprTokenKind::LessEq ||
				kind == ExprTokenKind::Greater || kind == ExprTokenKind::GreaterEq)
			{
				++pos;
				auto rhs = parseAdditive(tokens, pos, flags);
				return ExpressionResult{compareResults(lhs, kind, rhs)};
			}
		}

		return lhs;
	}

	/// @brief 加減算式: multiplicative ((+ | -) multiplicative)*
	[[nodiscard]] ExpressionResult parseAdditive(
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		auto result = parseMultiplicative(tokens, pos, flags);

		while (pos < tokens.size())
		{
			auto kind = tokens[pos].kind;
			if (kind != ExprTokenKind::Plus && kind != ExprTokenKind::Minus)
			{
				break;
			}
			++pos;
			auto rhs = parseMultiplicative(tokens, pos, flags);

			if (kind == ExprTokenKind::Plus)
			{
				// 文字列連結: どちらかが文字列なら文字列連結
				if (std::holds_alternative<std::string>(result) ||
					std::holds_alternative<std::string>(rhs))
				{
					result = ExpressionResult{resultToString(result) + resultToString(rhs)};
				}
				else if (std::holds_alternative<float>(result) || std::holds_alternative<float>(rhs))
				{
					result = ExpressionResult{resultToFloat(result) + resultToFloat(rhs)};
				}
				else
				{
					result = ExpressionResult{resultToInt(result) + resultToInt(rhs)};
				}
			}
			else // Minus
			{
				if (std::holds_alternative<float>(result) || std::holds_alternative<float>(rhs))
				{
					result = ExpressionResult{resultToFloat(result) - resultToFloat(rhs)};
				}
				else
				{
					result = ExpressionResult{resultToInt(result) - resultToInt(rhs)};
				}
			}
		}

		return result;
	}

	/// @brief 乗除算式: unary ((* | / | %) unary)*
	[[nodiscard]] ExpressionResult parseMultiplicative(
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		auto result = parseUnary(tokens, pos, flags);

		while (pos < tokens.size())
		{
			auto kind = tokens[pos].kind;
			if (kind != ExprTokenKind::Star && kind != ExprTokenKind::Slash &&
				kind != ExprTokenKind::Percent)
			{
				break;
			}
			++pos;
			auto rhs = parseUnary(tokens, pos, flags);

			if (kind == ExprTokenKind::Star)
			{
				if (std::holds_alternative<float>(result) || std::holds_alternative<float>(rhs))
				{
					result = ExpressionResult{resultToFloat(result) * resultToFloat(rhs)};
				}
				else
				{
					result = ExpressionResult{resultToInt(result) * resultToInt(rhs)};
				}
			}
			else if (kind == ExprTokenKind::Slash)
			{
				if (std::holds_alternative<float>(result) || std::holds_alternative<float>(rhs))
				{
					float divisor = resultToFloat(rhs);
					result = ExpressionResult{(divisor != 0.0f) ? resultToFloat(result) / divisor : 0.0f};
				}
				else
				{
					int divisor = resultToInt(rhs);
					result = ExpressionResult{(divisor != 0) ? resultToInt(result) / divisor : 0};
				}
			}
			else // Percent
			{
				int divisor = resultToInt(rhs);
				result = ExpressionResult{(divisor != 0) ? resultToInt(result) % divisor : 0};
			}
		}

		return result;
	}

	/// @brief 単項式: (! | -) unary | primary
	[[nodiscard]] ExpressionResult parseUnary(
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		if (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::Not)
		{
			++pos;
			auto val = parseUnary(tokens, pos, flags);
			return ExpressionResult{!resultToBool(val)};
		}

		if (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::Minus)
		{
			// 単項マイナス: 前のトークンが値でない場合
			// （パーサーの文脈でunaryに来ている時点で単項）
			++pos;
			auto val = parseUnary(tokens, pos, flags);
			if (std::holds_alternative<float>(val))
			{
				return ExpressionResult{-resultToFloat(val)};
			}
			return ExpressionResult{-resultToInt(val)};
		}

		return parsePrimary(tokens, pos, flags);
	}

	/// @brief 基本式: リテラル | 変数 | 関数呼び出し | 括弧式
	[[nodiscard]] ExpressionResult parsePrimary(
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		if (pos >= tokens.size() || tokens[pos].kind == ExprTokenKind::Eof)
		{
			return ExpressionResult{0};
		}

		const auto& tok = tokens[pos];

		// 括弧式
		if (tok.kind == ExprTokenKind::LParen)
		{
			++pos;
			auto result = parseOrExpr(tokens, pos, flags);
			if (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::RParen)
			{
				++pos;
			}
			return result;
		}

		// リテラル
		if (tok.kind == ExprTokenKind::IntLiteral)
		{
			++pos;
			return ExpressionResult{static_cast<int>(tok.numValue)};
		}
		if (tok.kind == ExprTokenKind::FloatLiteral)
		{
			++pos;
			return ExpressionResult{static_cast<float>(tok.numValue)};
		}
		if (tok.kind == ExprTokenKind::StringLiteral)
		{
			++pos;
			return ExpressionResult{tok.text};
		}
		if (tok.kind == ExprTokenKind::BoolLiteral)
		{
			++pos;
			return ExpressionResult{tok.text == "true"};
		}

		// 変数参照: $varName
		if (tok.kind == ExprTokenKind::Variable)
		{
			++pos;
			auto val = flags.get(tok.text);
			if (val.has_value())
			{
				return flagValueToResult(*val);
			}
			return ExpressionResult{0};
		}

		// 識別子: 関数呼び出し or フラグ名（$なし）
		if (tok.kind == ExprTokenKind::Identifier)
		{
			auto name = tok.text;
			++pos;

			// 関数呼び出し: name(...)
			if (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::LParen)
			{
				return parseFunction(name, tokens, pos, flags);
			}

			// $なしの変数参照（フラグ名として解決）
			auto val = flags.get(name);
			if (val.has_value())
			{
				return flagValueToResult(*val);
			}
			return ExpressionResult{0};
		}

		// 不明なトークン
		++pos;
		return ExpressionResult{0};
	}

	// ── 組み込み関数 ─────────────────────────────────────────

	/// @brief 関数呼び出しをパースして評価する
	[[nodiscard]] ExpressionResult parseFunction(
		const std::string& name,
		const std::vector<ExprToken>& tokens, std::size_t& pos,
		const FlagManager& flags) const
	{
		// '(' を消費
		++pos;

		// 引数を収集
		std::vector<ExpressionResult> args;
		while (pos < tokens.size() && tokens[pos].kind != ExprTokenKind::RParen &&
			tokens[pos].kind != ExprTokenKind::Eof)
		{
			args.push_back(parseOrExpr(tokens, pos, flags));
			if (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::Comma)
			{
				++pos;
			}
		}

		// ')' を消費
		if (pos < tokens.size() && tokens[pos].kind == ExprTokenKind::RParen)
		{
			++pos;
		}

		return callFunction(name, args);
	}

	/// @brief 組み込み関数を呼び出す
	[[nodiscard]] ExpressionResult callFunction(
		const std::string& name,
		const std::vector<ExpressionResult>& args) const
	{
		if (name == "min" && args.size() >= 2)
		{
			if (std::holds_alternative<float>(args[0]) || std::holds_alternative<float>(args[1]))
			{
				return ExpressionResult{std::min(resultToFloat(args[0]), resultToFloat(args[1]))};
			}
			return ExpressionResult{std::min(resultToInt(args[0]), resultToInt(args[1]))};
		}

		if (name == "max" && args.size() >= 2)
		{
			if (std::holds_alternative<float>(args[0]) || std::holds_alternative<float>(args[1]))
			{
				return ExpressionResult{std::max(resultToFloat(args[0]), resultToFloat(args[1]))};
			}
			return ExpressionResult{std::max(resultToInt(args[0]), resultToInt(args[1]))};
		}

		if (name == "abs" && args.size() >= 1)
		{
			if (std::holds_alternative<float>(args[0]))
			{
				return ExpressionResult{std::abs(resultToFloat(args[0]))};
			}
			return ExpressionResult{std::abs(resultToInt(args[0]))};
		}

		if (name == "random" && args.size() >= 2)
		{
			int lo = resultToInt(args[0]);
			int hi = resultToInt(args[1]);
			if (lo > hi) std::swap(lo, hi);
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<int> dist(lo, hi);
			return ExpressionResult{dist(gen)};
		}

		if (name == "strlen" && args.size() >= 1)
		{
			auto s = resultToString(args[0]);
			return ExpressionResult{static_cast<int>(s.size())};
		}

		if (name == "substr" && args.size() >= 2)
		{
			auto s = resultToString(args[0]);
			int start = resultToInt(args[1]);
			if (start < 0) start = 0;
			if (static_cast<std::size_t>(start) >= s.size())
			{
				return ExpressionResult{std::string("")};
			}
			if (args.size() >= 3)
			{
				int len = resultToInt(args[2]);
				if (len < 0) len = 0;
				return ExpressionResult{s.substr(static_cast<std::size_t>(start),
					static_cast<std::size_t>(len))};
			}
			return ExpressionResult{s.substr(static_cast<std::size_t>(start))};
		}

		// 未知の関数は0を返す
		return ExpressionResult{0};
	}

	// ── 比較ヘルパー ─────────────────────────────────────────

	/// @brief 2つのExpressionResultを比較する
	[[nodiscard]] static bool compareResults(
		const ExpressionResult& lhs, ExprTokenKind op, const ExpressionResult& rhs)
	{
		// 文字列同士
		if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs))
		{
			const auto& l = std::get<std::string>(lhs);
			const auto& r = std::get<std::string>(rhs);
			switch (op)
			{
			case ExprTokenKind::EqEq:     return l == r;
			case ExprTokenKind::NotEq:    return l != r;
			case ExprTokenKind::Less:     return l < r;
			case ExprTokenKind::LessEq:   return l <= r;
			case ExprTokenKind::Greater:  return l > r;
			case ExprTokenKind::GreaterEq: return l >= r;
			default: return false;
			}
		}

		// bool同士
		if (std::holds_alternative<bool>(lhs) && std::holds_alternative<bool>(rhs))
		{
			bool l = std::get<bool>(lhs);
			bool r = std::get<bool>(rhs);
			switch (op)
			{
			case ExprTokenKind::EqEq:  return l == r;
			case ExprTokenKind::NotEq: return l != r;
			default: return false;
			}
		}

		// 数値比較（float昇格）
		float l = resultToFloat(lhs);
		float r = resultToFloat(rhs);
		switch (op)
		{
		case ExprTokenKind::EqEq:      return l == r;
		case ExprTokenKind::NotEq:     return l != r;
		case ExprTokenKind::Less:      return l < r;
		case ExprTokenKind::LessEq:    return l <= r;
		case ExprTokenKind::Greater:   return l > r;
		case ExprTokenKind::GreaterEq: return l >= r;
		default: return false;
		}
	}
};

// ════════════════════════════════════════════════════════════════════
//  便利関数
// ════════════════════════════════════════════════════════════════════

/// @brief 式を評価して結果を返す（フリー関数版）
/// @param expression 式文字列
/// @param flags 変数解決用のFlagManager
/// @return 評価結果
[[nodiscard]] inline ExpressionResult evaluateExpression(
	std::string_view expression, const FlagManager& flags)
{
	ExpressionEvaluator evaluator;
	return evaluator.evaluate(expression, flags);
}

/// @brief 条件式をboolで評価する（フリー関数版）
/// @param expression 条件式
/// @param flags 変数解決用のFlagManager
/// @return bool結果
[[nodiscard]] inline bool evaluateCondition(
	std::string_view expression, const FlagManager& flags)
{
	ExpressionEvaluator evaluator;
	return evaluator.evaluateBool(expression, flags);
}

} // namespace mitiru::vn
