#pragma once

/// @file MathParser.hpp
/// @brief 数式パーサー・評価器
///
/// 文字列の数式を再帰下降パーサーで解析し、浮動小数点値として評価する。
/// 変数・関数・定数をサポート。
///
/// @code
/// sgc::math::MathParser parser;
/// parser.setVariable("x", 3.0f);
/// auto result = parser.evaluate("2*x+1"); // 7.0f
/// @endcode

#include <cmath>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <functional>
#include <cstddef>
#include <numbers>

namespace sgc::math
{

/// @brief 数式パーサー・評価器
///
/// 再帰下降パーサーにより四則演算・べき乗・関数・変数・定数を解析・評価する。
/// スレッドセーフ: 変数設定後の読み取り専用評価は複数スレッドから安全。
class MathParser
{
public:
	/// @brief 変数を設定する
	/// @param name 変数名
	/// @param value 変数値
	void setVariable(const std::string& name, float value)
	{
		m_variables[name] = value;
	}

	/// @brief 変数を取得する
	/// @param name 変数名
	/// @return 変数値（未設定の場合はnullopt）
	[[nodiscard]] std::optional<float> getVariable(const std::string& name) const
	{
		const auto it = m_variables.find(name);
		if (it != m_variables.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	/// @brief 数式を評価する
	/// @param expr 数式文字列
	/// @return 評価結果（エラーの場合はnullopt）
	[[nodiscard]] std::optional<float> evaluate(const std::string& expr)
	{
		m_input = expr;
		m_pos = 0;
		m_lastError.clear();

		skipWhitespace();
		if (m_pos >= m_input.size())
		{
			m_lastError = "Empty expression";
			return std::nullopt;
		}

		auto result = parseExpression();
		if (!result.has_value())
		{
			return std::nullopt;
		}

		skipWhitespace();
		if (m_pos < m_input.size())
		{
			m_lastError = "Unexpected character: " + std::string(1, m_input[m_pos]);
			return std::nullopt;
		}

		return result;
	}

	/// @brief 数式の構文が正しいか検証する
	/// @param expr 数式文字列
	/// @return 構文が正しい場合true
	[[nodiscard]] bool isValid(const std::string& expr)
	{
		return evaluate(expr).has_value();
	}

	/// @brief 最後のエラーメッセージを取得する
	/// @return エラーメッセージ（エラーがない場合は空文字列）
	[[nodiscard]] const std::string& getLastError() const noexcept
	{
		return m_lastError;
	}

private:
	std::string m_input;                                  ///< 現在解析中の入力
	std::size_t m_pos = 0;                                ///< 現在の解析位置
	std::string m_lastError;                              ///< 最後のエラー
	std::unordered_map<std::string, float> m_variables;   ///< 変数テーブル

	/// @brief 空白をスキップする
	void skipWhitespace() noexcept
	{
		while (m_pos < m_input.size() && (m_input[m_pos] == ' ' || m_input[m_pos] == '\t'))
		{
			++m_pos;
		}
	}

	/// @brief 現在の文字を取得する
	[[nodiscard]] char peek() const noexcept
	{
		if (m_pos < m_input.size())
		{
			return m_input[m_pos];
		}
		return '\0';
	}

	/// @brief 現在の文字を消費して進める
	char advance() noexcept
	{
		if (m_pos < m_input.size())
		{
			return m_input[m_pos++];
		}
		return '\0';
	}

	/// @brief expression = term (('+' | '-') term)*
	[[nodiscard]] std::optional<float> parseExpression()
	{
		auto left = parseTerm();
		if (!left.has_value()) return std::nullopt;

		skipWhitespace();
		while (m_pos < m_input.size() && (peek() == '+' || peek() == '-'))
		{
			const char op = advance();
			auto right = parseTerm();
			if (!right.has_value()) return std::nullopt;

			if (op == '+')
			{
				left = *left + *right;
			}
			else
			{
				left = *left - *right;
			}
			skipWhitespace();
		}

		return left;
	}

	/// @brief term = power (('*' | '/') power)*
	[[nodiscard]] std::optional<float> parseTerm()
	{
		auto left = parsePower();
		if (!left.has_value()) return std::nullopt;

		skipWhitespace();
		while (m_pos < m_input.size() && (peek() == '*' || peek() == '/'))
		{
			const char op = advance();
			auto right = parsePower();
			if (!right.has_value()) return std::nullopt;

			if (op == '*')
			{
				left = *left * *right;
			}
			else
			{
				if (*right == 0.0f)
				{
					m_lastError = "Division by zero";
					return std::nullopt;
				}
				left = *left / *right;
			}
			skipWhitespace();
		}

		return left;
	}

	/// @brief power = unary ('^' unary)*
	/// 右結合: 2^3^2 = 2^(3^2) = 512
	[[nodiscard]] std::optional<float> parsePower()
	{
		auto base = parseUnary();
		if (!base.has_value()) return std::nullopt;

		skipWhitespace();
		if (m_pos < m_input.size() && peek() == '^')
		{
			advance();
			auto exp = parsePower(); // 右結合のため再帰
			if (!exp.has_value()) return std::nullopt;
			return std::pow(*base, *exp);
		}

		return base;
	}

	/// @brief unary = '-'? primary
	[[nodiscard]] std::optional<float> parseUnary()
	{
		skipWhitespace();
		if (peek() == '-')
		{
			advance();
			auto val = parseUnary();
			if (!val.has_value()) return std::nullopt;
			return -*val;
		}
		if (peek() == '+')
		{
			advance();
			return parseUnary();
		}
		return parsePrimary();
	}

	/// @brief primary = number | identifier | function(expr) | '(' expr ')'
	[[nodiscard]] std::optional<float> parsePrimary()
	{
		skipWhitespace();

		// 括弧
		if (peek() == '(')
		{
			advance();
			auto val = parseExpression();
			if (!val.has_value()) return std::nullopt;
			skipWhitespace();
			if (peek() != ')')
			{
				m_lastError = "Expected closing parenthesis";
				return std::nullopt;
			}
			advance();
			return val;
		}

		// 数値リテラル
		if (isDigitOrDot(peek()))
		{
			return parseNumber();
		}

		// 識別子（変数 or 関数 or 定数）
		if (isAlpha(peek()))
		{
			return parseIdentifier();
		}

		m_lastError = "Unexpected character: " + std::string(1, peek());
		return std::nullopt;
	}

	/// @brief 数値をパースする
	[[nodiscard]] std::optional<float> parseNumber()
	{
		const std::size_t start = m_pos;
		bool hasDot = false;

		while (m_pos < m_input.size() && (isDigit(m_input[m_pos]) || m_input[m_pos] == '.'))
		{
			if (m_input[m_pos] == '.')
			{
				if (hasDot)
				{
					m_lastError = "Invalid number format";
					return std::nullopt;
				}
				hasDot = true;
			}
			++m_pos;
		}

		const std::string numStr = m_input.substr(start, m_pos - start);
		try
		{
			return std::stof(numStr);
		}
		catch (...)
		{
			m_lastError = "Invalid number: " + numStr;
			return std::nullopt;
		}
	}

	/// @brief 識別子（変数・関数・定数）をパースする
	[[nodiscard]] std::optional<float> parseIdentifier()
	{
		const std::size_t start = m_pos;
		while (m_pos < m_input.size() && (isAlpha(m_input[m_pos]) || isDigit(m_input[m_pos]) || m_input[m_pos] == '_'))
		{
			++m_pos;
		}

		const std::string name = m_input.substr(start, m_pos - start);

		// 定数チェック
		if (name == "pi")
		{
			return std::numbers::pi_v<float>;
		}
		if (name == "e")
		{
			return std::numbers::e_v<float>;
		}

		// 関数チェック（名前の後に括弧が続く場合）
		skipWhitespace();
		if (peek() == '(')
		{
			return parseFunction(name);
		}

		// 変数
		const auto it = m_variables.find(name);
		if (it != m_variables.end())
		{
			return it->second;
		}

		m_lastError = "Unknown variable: " + name;
		return std::nullopt;
	}

	/// @brief 関数呼び出しをパースする
	/// @param name 関数名
	[[nodiscard]] std::optional<float> parseFunction(const std::string& name)
	{
		// '('を消費
		advance();

		// min/maxは2引数
		if (name == "min" || name == "max")
		{
			auto arg1 = parseExpression();
			if (!arg1.has_value()) return std::nullopt;

			skipWhitespace();
			if (peek() != ',')
			{
				m_lastError = "Expected ',' in " + name + "()";
				return std::nullopt;
			}
			advance();

			auto arg2 = parseExpression();
			if (!arg2.has_value()) return std::nullopt;

			skipWhitespace();
			if (peek() != ')')
			{
				m_lastError = "Expected closing parenthesis";
				return std::nullopt;
			}
			advance();

			if (name == "min") return (*arg1 < *arg2) ? *arg1 : *arg2;
			return (*arg1 > *arg2) ? *arg1 : *arg2;
		}

		// 1引数関数
		auto arg = parseExpression();
		if (!arg.has_value()) return std::nullopt;

		skipWhitespace();
		if (peek() != ')')
		{
			m_lastError = "Expected closing parenthesis";
			return std::nullopt;
		}
		advance();

		return applyFunction(name, *arg);
	}

	/// @brief 1引数関数を適用する
	/// @param name 関数名
	/// @param arg 引数
	[[nodiscard]] std::optional<float> applyFunction(const std::string& name, float arg) const
	{
		if (name == "sin")   return std::sin(arg);
		if (name == "cos")   return std::cos(arg);
		if (name == "tan")   return std::tan(arg);
		if (name == "abs")   return std::abs(arg);
		if (name == "sqrt")
		{
			if (arg < 0.0f)
			{
				// 負の数のsqrtは未定義だがNaNを返す
				return std::sqrt(arg);
			}
			return std::sqrt(arg);
		}
		if (name == "log")   return std::log10(arg);
		if (name == "ln")    return std::log(arg);
		if (name == "exp")   return std::exp(arg);
		if (name == "floor") return std::floor(arg);
		if (name == "ceil")  return std::ceil(arg);

		// constメソッドなのでm_lastErrorには書けない
		return std::nullopt;
	}

	/// @brief 文字が数字かどうか
	[[nodiscard]] static bool isDigit(char c) noexcept
	{
		return c >= '0' && c <= '9';
	}

	/// @brief 文字が数字またはドットかどうか
	[[nodiscard]] static bool isDigitOrDot(char c) noexcept
	{
		return isDigit(c) || c == '.';
	}

	/// @brief 文字がアルファベットかどうか
	[[nodiscard]] static bool isAlpha(char c) noexcept
	{
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	}
};

} // namespace sgc::math
