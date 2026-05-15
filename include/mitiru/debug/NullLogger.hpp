#pragma once

/// @file NullLogger.hpp
/// @brief 無出力ロガー
/// @details 全ログを破棄する。テストやリリースビルドで使用する。

#include <mitiru/debug/ILogger.hpp>

namespace mitiru::debug
{

/// @brief 無出力ロガー（全ログを破棄）
class NullLogger final : public ILogger
{
public:
	void log(LogLevel /*level*/,
	         std::string_view /*category*/,
	         std::string_view /*message*/) override
	{
		// 意図的に何もしない
	}
};

} // namespace mitiru::debug
