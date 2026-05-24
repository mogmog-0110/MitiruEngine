#pragma once

/// @file PluralRules.hpp
/// @brief 複数形ルール
///
/// 言語ごとの複数形カテゴリを判定する。
/// CLDR複数形ルールに基づく英語・日本語・ロシア語のルールを提供。
///
/// @code
/// sgc::i18n::EnglishPluralRule rule;
/// auto cat = sgc::i18n::resolvePlural(1, rule);
/// // => PluralCategory::One
///
/// auto cat2 = sgc::i18n::resolvePlural(5, rule);
/// // => PluralCategory::Other
/// @endcode

#include <cmath>
#include <concepts>

namespace sgc::i18n
{

/// @brief 複数形カテゴリ（CLDR準拠）
enum class PluralCategory
{
	Zero,   ///< ゼロ
	One,    ///< 単数
	Two,    ///< 双数
	Few,    ///< 少数
	Many,   ///< 多数
	Other   ///< その他
};

/// @brief 複数形ルールのコンセプト
template <typename T>
concept PluralRule = requires(const T rule, int count)
{
	{ rule.resolve(count) } -> std::same_as<PluralCategory>;
};

/// @brief 英語の複数形ルール
///
/// 1のときOne、それ以外はOther。
class EnglishPluralRule
{
public:
	/// @brief 数値からカテゴリを判定する
	/// @param count 数値
	/// @return 複数形カテゴリ
	[[nodiscard]] constexpr PluralCategory resolve(int count) const noexcept
	{
		const int absCount = (count >= 0) ? count : -count;
		return (absCount == 1) ? PluralCategory::One : PluralCategory::Other;
	}
};

/// @brief 日本語の複数形ルール
///
/// 日本語は複数形を区別しないため、常にOther。
class JapanesePluralRule
{
public:
	/// @brief 数値からカテゴリを判定する
	/// @param count 数値（使用しない）
	/// @return 常にPluralCategory::Other
	[[nodiscard]] constexpr PluralCategory resolve([[maybe_unused]] int count) const noexcept
	{
		return PluralCategory::Other;
	}
};

/// @brief ロシア語の複数形ルール（CLDR準拠）
///
/// - 末尾が1（ただし11を除く）→ One
/// - 末尾が2〜4（ただし12〜14を除く）→ Few
/// - 末尾が0、末尾が5〜9、末尾が11〜14 → Many
class RussianPluralRule
{
public:
	/// @brief 数値からカテゴリを判定する
	/// @param count 数値
	/// @return 複数形カテゴリ
	[[nodiscard]] constexpr PluralCategory resolve(int count) const noexcept
	{
		const int absCount = (count >= 0) ? count : -count;
		const int mod10 = absCount % 10;
		const int mod100 = absCount % 100;

		if (mod10 == 1 && mod100 != 11)
		{
			return PluralCategory::One;
		}

		if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
		{
			return PluralCategory::Few;
		}

		return PluralCategory::Many;
	}
};

/// @brief 複数形カテゴリを解決する
/// @tparam Rule 複数形ルール型（PluralRuleコンセプトを満たす型）
/// @param count 数値
/// @param rule 使用するルール
/// @return 複数形カテゴリ
template <PluralRule Rule>
[[nodiscard]] constexpr PluralCategory resolvePlural(int count, const Rule& rule) noexcept
{
	return rule.resolve(count);
}

} // namespace sgc::i18n
