#pragma once

/// @file FileLogger.hpp
/// @brief ファイル出力ロガー
/// @details ログをファイルに書き出す。プロダクション環境での診断用。

#include <fstream>
#include <string>
#include <string_view>

#include <mitiru/debug/ILogger.hpp>

namespace mitiru::debug
{

/// @brief ファイル出力ロガー
class FileLogger final : public ILogger
{
public:
	/// @brief コンストラクタ
	/// @param filePath 出力ファイルパス
	/// @param append trueなら追記モード
	explicit FileLogger(const std::string& filePath, bool append = false)
		: m_stream(filePath, append ? (std::ios::out | std::ios::app) : std::ios::out)
	{
	}

	/// @brief ファイルが正常にオープンされているか
	[[nodiscard]] bool isOpen() const noexcept { return m_stream.is_open(); }

	void log(LogLevel level,
	         std::string_view category,
	         std::string_view message) override
	{
		if (!m_stream.is_open()) return;

		m_stream << "[" << levelTag(level) << "] "
		         << "[" << category << "] "
		         << message << "\n";
		m_stream.flush();
	}

private:
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

	std::ofstream m_stream;
};

} // namespace mitiru::debug
