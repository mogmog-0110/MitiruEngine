#pragma once

/// @file StringUtils.hpp
/// @brief 汎用文字列ユーティリティ関数群
///
/// trim / split / join / toLower / toUpper / startsWith / endsWith /
/// contains / replace / replaceAll を提供する。
/// ASCII範囲のみ対応（ゲーム開発での一般的な用途を想定）。
///
/// @code
/// auto parts = sgc::split("a,b,c", ',');
/// auto joined = sgc::join(parts, "-"); // "a-b-c"
/// auto trimmed = sgc::trim("  hello  "); // "hello"
/// @endcode

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace sgc
{

/// @brief 先頭の空白を除去する
/// @param s 入力文字列
/// @return 先頭空白を除去した文字列ビュー
[[nodiscard]] constexpr std::string_view trimLeft(std::string_view s) noexcept
{
	std::size_t i = 0;
	while (i < s.size()
		&& (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
	{
		++i;
	}
	return s.substr(i);
}

/// @brief 末尾の空白を除去する
/// @param s 入力文字列
/// @return 末尾空白を除去した文字列ビュー
[[nodiscard]] constexpr std::string_view trimRight(std::string_view s) noexcept
{
	std::size_t end = s.size();
	while (end > 0
		&& (s[end - 1] == ' ' || s[end - 1] == '\t'
			|| s[end - 1] == '\n' || s[end - 1] == '\r'))
	{
		--end;
	}
	return s.substr(0, end);
}

/// @brief 先頭と末尾の空白を除去する
/// @param s 入力文字列
/// @return 空白を除去した文字列ビュー
[[nodiscard]] constexpr std::string_view trim(std::string_view s) noexcept
{
	return trimRight(trimLeft(s));
}

/// @brief 文字列を区切り文字で分割する
/// @param s 入力文字列
/// @param delimiter 区切り文字
/// @return 分割された文字列ビューのベクタ
[[nodiscard]] inline std::vector<std::string_view>
split(std::string_view s, char delimiter)
{
	std::vector<std::string_view> result;
	std::size_t start = 0;
	while (start <= s.size())
	{
		const auto pos = s.find(delimiter, start);
		if (pos == std::string_view::npos)
		{
			result.push_back(s.substr(start));
			break;
		}
		result.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	return result;
}

/// @brief 文字列を区切り文字列で分割する
/// @param s 入力文字列
/// @param delimiter 区切り文字列
/// @return 分割された文字列ビューのベクタ
[[nodiscard]] inline std::vector<std::string_view>
split(std::string_view s, std::string_view delimiter)
{
	std::vector<std::string_view> result;
	if (delimiter.empty())
	{
		result.push_back(s);
		return result;
	}
	std::size_t start = 0;
	while (start <= s.size())
	{
		const auto pos = s.find(delimiter, start);
		if (pos == std::string_view::npos)
		{
			result.push_back(s.substr(start));
			break;
		}
		result.push_back(s.substr(start, pos - start));
		start = pos + delimiter.size();
	}
	return result;
}

/// @brief 文字列ビューの配列をセパレータで結合する
/// @param parts 結合する文字列ビュー配列
/// @param separator セパレータ
/// @return 結合された文字列
[[nodiscard]] inline std::string
join(const std::vector<std::string_view>& parts, std::string_view separator)
{
	if (parts.empty()) return "";
	std::string result;
	// 必要サイズを事前計算
	std::size_t totalSize = 0;
	for (const auto& p : parts) totalSize += p.size();
	totalSize += separator.size() * (parts.size() - 1);
	result.reserve(totalSize);

	result.append(parts[0]);
	for (std::size_t i = 1; i < parts.size(); ++i)
	{
		result.append(separator);
		result.append(parts[i]);
	}
	return result;
}

/// @brief ASCII小文字に変換する
/// @param s 入力文字列
/// @return 小文字化された文字列
[[nodiscard]] inline std::string toLower(std::string_view s)
{
	std::string result(s);
	for (auto& c : result)
	{
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
	}
	return result;
}

/// @brief ASCII大文字に変換する
/// @param s 入力文字列
/// @return 大文字化された文字列
[[nodiscard]] inline std::string toUpper(std::string_view s)
{
	std::string result(s);
	for (auto& c : result)
	{
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
	}
	return result;
}

/// @brief 文字列が指定プレフィックスで始まるか判定する
/// @param s 対象文字列
/// @param prefix プレフィックス
/// @return プレフィックスで始まればtrue
[[nodiscard]] constexpr bool startsWith(std::string_view s, std::string_view prefix) noexcept
{
	return s.starts_with(prefix);
}

/// @brief 文字列が指定サフィックスで終わるか判定する
/// @param s 対象文字列
/// @param suffix サフィックス
/// @return サフィックスで終わればtrue
[[nodiscard]] constexpr bool endsWith(std::string_view s, std::string_view suffix) noexcept
{
	return s.ends_with(suffix);
}

/// @brief 文字列が部分文字列を含むか判定する
/// @param s 対象文字列
/// @param sub 部分文字列
/// @return 含んでいればtrue
[[nodiscard]] constexpr bool contains(std::string_view s, std::string_view sub) noexcept
{
	return s.find(sub) != std::string_view::npos;
}

/// @brief 最初に一致した部分文字列を置換する
/// @param s 対象文字列
/// @param from 置換元
/// @param to 置換先
/// @return 置換後の文字列
[[nodiscard]] inline std::string
replace(std::string_view s, std::string_view from, std::string_view to)
{
	std::string result(s);
	if (from.empty()) return result;
	const auto pos = result.find(from);
	if (pos != std::string::npos)
	{
		result.replace(pos, from.size(), to);
	}
	return result;
}

/// @brief すべての一致する部分文字列を置換する
/// @param s 対象文字列
/// @param from 置換元
/// @param to 置換先
/// @return 置換後の文字列
[[nodiscard]] inline std::string
replaceAll(std::string_view s, std::string_view from, std::string_view to)
{
	std::string result(s);
	if (from.empty()) return result;
	std::size_t pos = 0;
	while ((pos = result.find(from, pos)) != std::string::npos)
	{
		result.replace(pos, from.size(), to);
		pos += to.size();
	}
	return result;
}

} // namespace sgc
