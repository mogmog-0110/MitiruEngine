#pragma once

/// @file ILogger.hpp
/// @brief ロガーインターフェース
/// @details エンジン全体で統一されたログ出力を提供する。
///          ServiceRegistryに登録してDI的に使用する。

#include <cstdint>
#include <string>
#include <string_view>

namespace mitiru::debug
{

/// @brief ログレベル
enum class LogLevel : std::uint8_t
{
	Trace = 0, ///< 詳細トレース（フレーム毎のデータ等）
	Debug,     ///< デバッグ情報
	Info,      ///< 一般情報
	Warn,      ///< 警告
	Error,     ///< エラー
};

/// @brief ロガーインターフェース
/// @details 全ログ出力はこのインターフェースを通じて行う。
///
/// @code
/// auto& logger = mitiru::debug::Log::get();
/// logger.info("Engine", "Initialized successfully");
/// logger.warn("Render", "Fallback to software rasterizer");
/// @endcode
class ILogger
{
public:
	virtual ~ILogger() = default;

	/// @brief ログメッセージを出力する
	/// @param level ログレベル
	/// @param category カテゴリ名（サブシステム識別用）
	/// @param message メッセージ本文
	virtual void log(LogLevel level,
	                 std::string_view category,
	                 std::string_view message) = 0;

	/// @brief 最小出力レベルを設定する
	/// @param level このレベル以上のみ出力する
	virtual void setMinLevel(LogLevel level) noexcept { m_minLevel = level; }

	/// @brief 最小出力レベルを取得する
	[[nodiscard]] LogLevel minLevel() const noexcept { return m_minLevel; }

	/// @brief 指定レベルが出力対象か判定する
	[[nodiscard]] bool shouldLog(LogLevel level) const noexcept
	{
		return static_cast<uint8_t>(level) >= static_cast<uint8_t>(m_minLevel);
	}

	// ── 便利メソッド ──

	void trace(std::string_view category, std::string_view msg)
	{
		if (shouldLog(LogLevel::Trace)) log(LogLevel::Trace, category, msg);
	}

	void debug(std::string_view category, std::string_view msg)
	{
		if (shouldLog(LogLevel::Debug)) log(LogLevel::Debug, category, msg);
	}

	void info(std::string_view category, std::string_view msg)
	{
		if (shouldLog(LogLevel::Info)) log(LogLevel::Info, category, msg);
	}

	void warn(std::string_view category, std::string_view msg)
	{
		if (shouldLog(LogLevel::Warn)) log(LogLevel::Warn, category, msg);
	}

	void error(std::string_view category, std::string_view msg)
	{
		if (shouldLog(LogLevel::Error)) log(LogLevel::Error, category, msg);
	}

protected:
	LogLevel m_minLevel = LogLevel::Debug;
};

} // namespace mitiru::debug
