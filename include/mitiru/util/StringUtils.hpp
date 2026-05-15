#pragma once

/// @file StringUtils.hpp
/// @brief 汎用文字列ユーティリティ
/// @details エンジン全体で使用する文字列操作関数群。
///          全て header-only、外部依存なし。constexpr/noexcept を可能な限り付与。
///
/// @code
/// using namespace mitiru::util;
///
/// auto parts = StringUtils::split("a,b,c", ",");
/// auto joined = StringUtils::join(parts, " | ");      // "a | b | c"
/// auto trimmed = StringUtils::trim("  hello  ");       // "hello"
/// bool ok = StringUtils::startsWith("hello.png", "hello"); // true
/// auto num = StringUtils::toNumber<int>("42");         // optional(42)
/// @endcode

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::util
{

/// @brief 文字列ユーティリティ（全て静的メソッド）
class StringUtils
{
public:
	StringUtils() = delete;

	// ── 分割・結合 ──

	/// @brief 文字列をデリミタで分割する
	/// @param str 入力文字列
	/// @param delimiter デリミタ文字列
	/// @return 分割された文字列のベクタ
	[[nodiscard]] static std::vector<std::string> split(std::string_view str,
	                                                    std::string_view delimiter)
	{
		std::vector<std::string> result;
		if (delimiter.empty())
		{
			result.emplace_back(str);
			return result;
		}

		std::size_t start = 0;
		std::size_t pos = str.find(delimiter, start);

		while (pos != std::string_view::npos)
		{
			result.emplace_back(str.substr(start, pos - start));
			start = pos + delimiter.size();
			pos = str.find(delimiter, start);
		}
		result.emplace_back(str.substr(start));
		return result;
	}

	/// @brief 文字列のベクタをデリミタで結合する
	/// @param strings 文字列の配列
	/// @param delimiter デリミタ文字列
	/// @return 結合された文字列
	[[nodiscard]] static std::string join(const std::vector<std::string>& strings,
	                                      std::string_view delimiter)
	{
		if (strings.empty())
		{
			return {};
		}

		std::string result = strings[0];
		for (std::size_t i = 1; i < strings.size(); ++i)
		{
			result.append(delimiter);
			result.append(strings[i]);
		}
		return result;
	}

	// ── トリム ──

	/// @brief 先頭の空白文字を除去する
	[[nodiscard]] static std::string trimLeft(std::string_view str)
	{
		auto it = std::find_if_not(str.begin(), str.end(),
		                           [](unsigned char c) { return std::isspace(c); });
		return std::string(it, str.end());
	}

	/// @brief 末尾の空白文字を除去する
	[[nodiscard]] static std::string trimRight(std::string_view str)
	{
		auto it = std::find_if_not(str.rbegin(), str.rend(),
		                           [](unsigned char c) { return std::isspace(c); });
		return std::string(str.begin(), it.base());
	}

	/// @brief 先頭と末尾の空白文字を除去する
	[[nodiscard]] static std::string trim(std::string_view str)
	{
		auto left = std::find_if_not(str.begin(), str.end(),
		                             [](unsigned char c) { return std::isspace(c); });
		auto right = std::find_if_not(str.rbegin(), str.rend(),
		                              [](unsigned char c) { return std::isspace(c); });
		if (left >= right.base())
		{
			return {};
		}
		return std::string(left, right.base());
	}

	// ── 大文字・小文字変換 ──

	/// @brief 全文字を小文字に変換する
	[[nodiscard]] static std::string toLower(std::string_view str)
	{
		std::string result(str);
		std::transform(result.begin(), result.end(), result.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	/// @brief 全文字を大文字に変換する
	[[nodiscard]] static std::string toUpper(std::string_view str)
	{
		std::string result(str);
		std::transform(result.begin(), result.end(), result.begin(),
		               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return result;
	}

	// ── 判定 ──

	/// @brief 文字列が指定プレフィックスで始まるか判定する
	[[nodiscard]] static constexpr bool startsWith(std::string_view str,
	                                               std::string_view prefix) noexcept
	{
		return str.size() >= prefix.size()
		       && str.substr(0, prefix.size()) == prefix;
	}

	/// @brief 文字列が指定サフィックスで終わるか判定する
	[[nodiscard]] static constexpr bool endsWith(std::string_view str,
	                                             std::string_view suffix) noexcept
	{
		return str.size() >= suffix.size()
		       && str.substr(str.size() - suffix.size()) == suffix;
	}

	/// @brief 文字列が部分文字列を含むか判定する
	[[nodiscard]] static constexpr bool contains(std::string_view str,
	                                             std::string_view substr) noexcept
	{
		return str.find(substr) != std::string_view::npos;
	}

	// ── 置換 ──

	/// @brief 最初の出現箇所を置換する
	[[nodiscard]] static std::string replace(std::string_view str,
	                                         std::string_view from,
	                                         std::string_view to)
	{
		std::string result(str);
		if (from.empty())
		{
			return result;
		}

		auto pos = result.find(from);
		if (pos != std::string::npos)
		{
			result.replace(pos, from.size(), to);
		}
		return result;
	}

	/// @brief 全ての出現箇所を置換する
	[[nodiscard]] static std::string replaceAll(std::string_view str,
	                                            std::string_view from,
	                                            std::string_view to)
	{
		std::string result(str);
		if (from.empty())
		{
			return result;
		}

		std::size_t pos = 0;
		while ((pos = result.find(from, pos)) != std::string::npos)
		{
			result.replace(pos, from.size(), to);
			pos += to.size();
		}
		return result;
	}

	// ── フォーマット ──

	/// @brief printf形式の文字列フォーマット
	/// @tparam Args フォーマット引数の型
	/// @param fmt フォーマット文字列
	/// @param args フォーマット引数
	/// @return フォーマット済み文字列
	/// @warning fmt must be a compile-time constant, never user input.
	/// Passing user-controlled strings as fmt enables format string injection attacks.
	template <typename... Args>
	[[nodiscard]] [[deprecated("Use with caution: fmt must be a compile-time constant, never user input")]]
	static std::string format(const char* fmt, Args&&... args)
	{
		// サイズ計算（null終端分を含む）
		int size = std::snprintf(nullptr, 0, fmt, std::forward<Args>(args)...);
		if (size <= 0)
		{
			return {};
		}

		std::string result(static_cast<std::size_t>(size), '\0');
		std::snprintf(result.data(), static_cast<std::size_t>(size + 1),
		              fmt, std::forward<Args>(args)...);
		return result;
	}

	// ── 数値変換 ──

	/// @brief 文字列を数値に安全に変換する
	/// @tparam T 変換先の数値型
	/// @param str 入力文字列
	/// @return 変換成功時は値を含むoptional、失敗時はnullopt
	template <typename T>
	[[nodiscard]] static std::optional<T> toNumber(std::string_view str) noexcept
	{
		if (str.empty())
		{
			return std::nullopt;
		}

		if constexpr (std::is_integral_v<T>)
		{
			T value{};
			auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
			if (ec == std::errc{} && ptr == str.data() + str.size())
			{
				return value;
			}
			return std::nullopt;
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			// from_charsのfloat/double対応はコンパイラ依存
			// 安全なフォールバック
			try
			{
				std::string s(str);
				std::size_t pos = 0;
				T value{};
				if constexpr (std::is_same_v<T, float>)
				{
					value = std::stof(s, &pos);
				}
				else
				{
					value = std::stod(s, &pos);
				}
				if (pos == s.size())
				{
					return value;
				}
			}
			catch (...)
			{
				// 変換失敗
			}
			return std::nullopt;
		}
		else
		{
			return std::nullopt;
		}
	}

	/// @brief 数値を文字列に変換する
	/// @tparam T 数値型
	/// @param value 値
	/// @return 文字列表現
	template <typename T>
	[[nodiscard]] static std::string fromNumber(const T& value)
	{
		if constexpr (std::is_integral_v<T>)
		{
			return std::to_string(value);
		}
		else
		{
			std::ostringstream oss;
			oss << value;
			return oss.str();
		}
	}

	// ── パディング ──

	/// @brief 左側をパディングする
	/// @param str 入力文字列
	/// @param width 目標幅
	/// @param padChar パディング文字
	/// @return パディング済み文字列
	[[nodiscard]] static std::string padLeft(std::string_view str,
	                                         std::size_t width,
	                                         char padChar = ' ')
	{
		if (str.size() >= width)
		{
			return std::string(str);
		}
		return std::string(width - str.size(), padChar) + std::string(str);
	}

	/// @brief 右側をパディングする
	[[nodiscard]] static std::string padRight(std::string_view str,
	                                          std::size_t width,
	                                          char padChar = ' ')
	{
		if (str.size() >= width)
		{
			return std::string(str);
		}
		return std::string(str) + std::string(width - str.size(), padChar);
	}

	// ── リピート ──

	/// @brief 文字列を指定回数繰り返す
	/// @param str 入力文字列
	/// @param count 繰り返し回数
	/// @return 繰り返された文字列
	[[nodiscard]] static std::string repeat(std::string_view str, std::size_t count)
	{
		if (count > 0 && str.size() > std::numeric_limits<std::size_t>::max() / count)
		{
			throw std::length_error("StringUtils::repeat: result too large");
		}
		std::string result;
		result.reserve(str.size() * count);
		for (std::size_t i = 0; i < count; ++i)
		{
			result.append(str);
		}
		return result;
	}
};

} // namespace mitiru::util
