#pragma once

/// @file DateTimeFormat.hpp
/// @brief 日付・時刻フォーマット
///
/// パターンベースの日時フォーマット機能を提供する。
/// システム時計に依存しない純粋関数設計。
///
/// @code
/// sgc::i18n::DateTime dt{2026, 3, 12, 14, 30, 0};
/// auto text = sgc::i18n::DateTimeFormatter::format(dt, "YYYY-MM-DD");
/// // => "2026-03-12"
///
/// auto jp = sgc::i18n::DateTimeFormatter::format(dt, "YYYY/MM/DD", "ja");
/// // => "2026/03/12"
/// @endcode

#include <array>
#include <string>
#include <string_view>

namespace sgc::i18n
{

/// @brief 日時構造体
struct DateTime
{
	int year{2000};     ///< 年
	int month{1};       ///< 月（1〜12）
	int day{1};         ///< 日（1〜31）
	int hour{0};        ///< 時（0〜23）
	int minute{0};      ///< 分（0〜59）
	int second{0};      ///< 秒（0〜59）
};

/// @brief 日付・時刻フォーマッター
///
/// パターン文字列に基づいて日時をフォーマットする。
///
/// 対応パターン:
/// - YYYY: 4桁年
/// - YY: 2桁年
/// - MM: 2桁月
/// - DD: 2桁日
/// - HH: 2桁時（24時間制）
/// - mm: 2桁分
/// - ss: 2桁秒
/// - MONTH: 月名（ロケール依存）
/// - DAY: 曜日名（未実装、予約）
class DateTimeFormatter
{
public:
	/// @brief 日時をフォーマットする
	/// @param dt 日時
	/// @param pattern フォーマットパターン
	/// @param locale ロケール（"en" または "ja"）
	/// @return フォーマットされた文字列
	[[nodiscard]] static std::string format(
		const DateTime& dt,
		std::string_view pattern,
		std::string_view locale = "en")
	{
		std::string result;
		result.reserve(pattern.size() + 16);

		std::size_t i = 0;
		while (i < pattern.size())
		{
			if (tryMatch(pattern, i, "YYYY"))
			{
				result += padNumber(dt.year, 4);
				i += 4;
			}
			else if (tryMatch(pattern, i, "YY"))
			{
				result += padNumber(dt.year % 100, 2);
				i += 2;
			}
			else if (tryMatch(pattern, i, "MONTH"))
			{
				result += monthName(dt.month, locale);
				i += 5;
			}
			else if (tryMatch(pattern, i, "MM"))
			{
				result += padNumber(dt.month, 2);
				i += 2;
			}
			else if (tryMatch(pattern, i, "DD"))
			{
				result += padNumber(dt.day, 2);
				i += 2;
			}
			else if (tryMatch(pattern, i, "HH"))
			{
				result += padNumber(dt.hour, 2);
				i += 2;
			}
			else if (tryMatch(pattern, i, "mm"))
			{
				result += padNumber(dt.minute, 2);
				i += 2;
			}
			else if (tryMatch(pattern, i, "ss"))
			{
				result += padNumber(dt.second, 2);
				i += 2;
			}
			else
			{
				result += pattern[i];
				++i;
			}
		}

		return result;
	}

	/// @brief 英語の月名を取得する
	/// @param month 月（1〜12）
	/// @return 月名
	[[nodiscard]] static std::string_view englishMonthName(int month) noexcept
	{
		static constexpr std::array<std::string_view, 12> names
		{
			"January", "February", "March", "April",
			"May", "June", "July", "August",
			"September", "October", "November", "December"
		};

		if (month < 1 || month > 12) return "Unknown";
		return names[static_cast<std::size_t>(month - 1)];
	}

	/// @brief 日本語の月名を取得する
	/// @param month 月（1〜12）
	/// @return 月名（例: "3月"）
	[[nodiscard]] static std::string japaneseMonthName(int month)
	{
		if (month < 1 || month > 12) return "Unknown";
		return std::to_string(month) + "\xE6\x9C\x88"; // "月" in UTF-8
	}

private:
	/// @brief パターンマッチを試みる
	/// @param text テキスト
	/// @param pos 現在位置
	/// @param token トークン
	/// @return マッチしたらtrue
	[[nodiscard]] static bool tryMatch(
		std::string_view text, std::size_t pos, std::string_view token) noexcept
	{
		if (pos + token.size() > text.size()) return false;
		return text.substr(pos, token.size()) == token;
	}

	/// @brief 数値をゼロ埋めした文字列にする
	/// @param value 数値
	/// @param width 最小幅
	/// @return ゼロ埋め文字列
	[[nodiscard]] static std::string padNumber(int value, int width)
	{
		const bool negative = value < 0;
		int absVal = negative ? -value : value;

		std::string s = std::to_string(absVal);
		while (static_cast<int>(s.size()) < width)
		{
			s.insert(s.begin(), '0');
		}

		if (negative)
		{
			s.insert(s.begin(), '-');
		}

		return s;
	}

	/// @brief ロケールに応じた月名を取得する
	/// @param month 月
	/// @param locale ロケール
	/// @return 月名
	[[nodiscard]] static std::string monthName(int month, std::string_view locale)
	{
		if (locale == "ja")
		{
			return japaneseMonthName(month);
		}
		return std::string(englishMonthName(month));
	}
};

} // namespace sgc::i18n
