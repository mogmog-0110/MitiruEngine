#pragma once

/// @file Assert.hpp
/// @brief デバッグ用アサーションマクロと実行時前提条件チェック
///
/// SGC_ASSERT はデバッグビルドでのみ有効なアサーション。
/// SGC_ENSURE はリリースビルドでも有効な前提条件チェック。
/// どちらも失敗時に式・メッセージ・ソース位置を表示して abort する。

#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace sgc
{
namespace detail
{

/// @brief デフォルトのアサーション失敗ハンドラ
///
/// 標準エラー出力にアサーション情報を出力し、プログラムを異常終了させる。
///
/// @param expression 失敗した条件式の文字列表現
/// @param message    ユーザーが指定したエラーメッセージ
/// @param location   アサーション失敗箇所のソース位置
[[noreturn]] inline void defaultAssertHandler(
	const char* expression,
	const char* message,
	const std::source_location& location)
{
	std::fprintf(
		stderr,
		"SGC ASSERTION FAILED\n"
		"  Expression: %s\n"
		"  Message:    %s\n"
		"  File:       %s\n"
		"  Line:       %u\n"
		"  Function:   %s\n",
		expression,
		message,
		location.file_name(),
		location.line(),
		location.function_name());
	std::abort();
}

} // namespace detail
} // namespace sgc

/// @brief デバッグビルド専用アサーションマクロ
///
/// NDEBUGが定義されている場合（リリースビルド）は何もしない。
/// 条件が false の場合、式・メッセージ・ファイル/行情報を表示して abort する。
///
/// @param condition チェックする条件式
/// @param message   失敗時に表示するメッセージ文字列
///
/// @code
/// SGC_ASSERT(ptr != nullptr, "ポインタがnullです");
/// SGC_ASSERT(index < size, "インデックスが範囲外です");
/// @endcode
#ifdef NDEBUG
	#define SGC_ASSERT(condition, message) ((void)0)
#else
	#define SGC_ASSERT(condition, message)                                     \
		do                                                                     \
		{                                                                      \
			if (!(condition)) [[unlikely]]                                     \
			{                                                                  \
				::sgc::detail::defaultAssertHandler(                           \
					#condition,                                                \
					(message),                                                 \
					std::source_location::current());                          \
			}                                                                  \
		}                                                                      \
		while (false)
#endif

/// @brief 実行時前提条件チェックマクロ（リリースビルドでも有効）
///
/// 関数のAPI境界で入力値を検証するために使用する。
/// SGC_ASSERT と異なり、NDEBUG に関係なく常に検査を行う。
///
/// @param condition チェックする条件式
/// @param message   失敗時に表示するメッセージ文字列
///
/// @code
/// void setHealth(int hp)
/// {
///     SGC_ENSURE(hp >= 0, "HPは0以上でなければなりません");
///     SGC_ENSURE(hp <= MAX_HP, "HPが上限を超えています");
///     m_health = hp;
/// }
/// @endcode
#define SGC_ENSURE(condition, message)                                         \
	do                                                                         \
	{                                                                          \
		if (!(condition)) [[unlikely]]                                         \
		{                                                                      \
			::sgc::detail::defaultAssertHandler(                               \
				#condition,                                                    \
				(message),                                                     \
				std::source_location::current());                              \
		}                                                                      \
	}                                                                          \
	while (false)
