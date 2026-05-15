#pragma once

/// @file ConsoleLogger.hpp
/// @brief コンソール出力ロガー
/// @details ログをstd::cerrに出力する。開発・デバッグ用。

#include <iostream>
#include <string_view>

#include <mitiru/debug/ILogger.hpp>

namespace mitiru::debug
{

/// @brief コンソール出力ロガー
class ConsoleLogger final : public ILogger
{
public:
	void log(LogLevel level,
	         std::string_view category,
	         std::string_view message) override
	{
		std::cerr << "[" << levelTag(level) << "] "
		          << "[" << category << "] "
		          << message << "\n";
	}

private:
	/// @brief ログレベルの短縮タグを返す
	[[nodiscard]] static constexpr const char* levelTag(LogLevel level) noexcept
	{
		switch (level)
		{
		case LogLevel::Trace: return "TRACE";
		case LogLevel::Debug: return "DEBUG";
		case LogLevel::Info:  return "INFO ";
		case LogLevel::Warn:  return "WARN ";
		case LogLevel::Error: return "ERROR";
		default:              return "?????";
		}
	}
};

} // namespace mitiru::debug
