#pragma once

/// @file Logger.hpp
/// @brief ログシステム
///
/// レベル付きロガー。std::format ベースのフォーマット、
/// カスタムSink対応、マクロによるsource_location自動付与。

#include <atomic>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sgc
{

/// @brief ログレベル
enum class LogLevel : std::uint8_t
{
	Trace = 0,
	Debug,
	Info,
	Warn,
	Error,
	Fatal,
	Off  ///< すべてのログを無効化
};

/// @brief ログレベルの文字列表現を返す
/// @param level ログレベル
/// @return レベルの短縮文字列
[[nodiscard]] inline constexpr std::string_view logLevelToString(LogLevel level) noexcept
{
	switch (level)
	{
	case LogLevel::Trace: return "TRACE";
	case LogLevel::Debug: return "DEBUG";
	case LogLevel::Info:  return "INFO";
	case LogLevel::Warn:  return "WARN";
	case LogLevel::Error: return "ERROR";
	case LogLevel::Fatal: return "FATAL";
	case LogLevel::Off:   return "OFF";
	}
	return "UNKNOWN";
}

/// @brief ログメッセージ構造体
struct LogMessage
{
	LogLevel level;                      ///< ログレベル
	std::string text;                    ///< フォーマット済みメッセージ
	std::source_location location;       ///< ソース位置
};

/// @brief ログSinkのコンセプト
/// @tparam T Sink型
template <typename T>
concept LogSink = requires(T sink, const LogMessage& msg)
{
	{ sink.write(msg) };
};

/// @brief 標準エラー出力Sink
struct StderrSink
{
	/// @brief ログメッセージを標準エラーに出力する
	void write(const LogMessage& msg) const
	{
		std::cerr << "[" << logLevelToString(msg.level) << "] "
			<< msg.text << " ("
			<< msg.location.file_name() << ":"
			<< msg.location.line() << ")\n";
	}
};

namespace detail
{

/// @brief フォーマット文字列 + ソース位置のラッパー
///
/// 便利メソッド（info, debug等）の引数として使用する。
/// 呼び出し側でconsteval変換が発生し、source_locationが自動キャプチャされる。
///
/// @tparam Args フォーマット引数の型
template <typename... Args>
struct FormatWithLocation
{
	std::format_string<Args...> fmt;  ///< フォーマット文字列
	std::source_location loc;         ///< ソース位置

	/// @brief 文字列リテラルからの暗黙変換コンストラクタ
	/// @param s フォーマット文字列に変換可能な値
	/// @param l ソース位置（呼び出し元が自動付与）
	template <typename T>
	consteval FormatWithLocation(const T& s,
		std::source_location l = std::source_location::current())
		: fmt(s), loc(l)
	{
	}
};

} // namespace detail

/// @brief ログシステム
///
/// @code
/// sgc::Logger logger;
/// logger.setLevel(sgc::LogLevel::Debug);
/// logger.info("Player {} took {} damage", playerName, damage);
///
/// // マクロ版でも同じ結果（source_location自動付与）
/// SGC_LOG_INFO(logger, "Player {} took {} damage", playerName, damage);
/// @endcode
class Logger
{
public:
	/// @brief デフォルトコンストラクタ（StderrSink付き、Infoレベル）
	Logger()
		: m_level(LogLevel::Info)
	{
		m_sinks.push_back([](const LogMessage& msg) {
			StderrSink{}.write(msg);
		});
	}

	/// @brief 指定レベルでメッセージをログ出力する
	/// @tparam Args フォーマット引数の型
	/// @param level ログレベル
	/// @param loc ソース位置
	/// @param fmt フォーマット文字列
	/// @param args フォーマット引数
	template <typename... Args>
	void log(LogLevel level, std::source_location loc, std::format_string<Args...> fmt, Args&&... args)
	{
		if (level < m_level.load(std::memory_order_relaxed)) return;

		LogMessage msg{
			level,
			std::format(fmt, std::forward<Args>(args)...),
			loc
		};

		std::lock_guard lock(m_mutex);
		for (const auto& sink : m_sinks)
		{
			sink(msg);
		}
	}

	/// @brief Traceレベルでログ出力する（source_location自動付与）
	template <typename... Args>
	void trace(detail::FormatWithLocation<std::type_identity_t<Args>...> fwl, Args&&... args)
	{
		log(LogLevel::Trace, fwl.loc, fwl.fmt, std::forward<Args>(args)...);
	}

	/// @brief Debugレベルでログ出力する（source_location自動付与）
	template <typename... Args>
	void debug(detail::FormatWithLocation<std::type_identity_t<Args>...> fwl, Args&&... args)
	{
		log(LogLevel::Debug, fwl.loc, fwl.fmt, std::forward<Args>(args)...);
	}

	/// @brief Infoレベルでログ出力する（source_location自動付与）
	template <typename... Args>
	void info(detail::FormatWithLocation<std::type_identity_t<Args>...> fwl, Args&&... args)
	{
		log(LogLevel::Info, fwl.loc, fwl.fmt, std::forward<Args>(args)...);
	}

	/// @brief Warnレベルでログ出力する（source_location自動付与）
	template <typename... Args>
	void warn(detail::FormatWithLocation<std::type_identity_t<Args>...> fwl, Args&&... args)
	{
		log(LogLevel::Warn, fwl.loc, fwl.fmt, std::forward<Args>(args)...);
	}

	/// @brief Errorレベルでログ出力する（source_location自動付与）
	template <typename... Args>
	void error(detail::FormatWithLocation<std::type_identity_t<Args>...> fwl, Args&&... args)
	{
		log(LogLevel::Error, fwl.loc, fwl.fmt, std::forward<Args>(args)...);
	}

	/// @brief Fatalレベルでログ出力する（source_location自動付与）
	template <typename... Args>
	void fatal(detail::FormatWithLocation<std::type_identity_t<Args>...> fwl, Args&&... args)
	{
		log(LogLevel::Fatal, fwl.loc, fwl.fmt, std::forward<Args>(args)...);
	}

	/// @brief ログレベルを設定する（スレッド安全）
	void setLevel(LogLevel level) noexcept { m_level.store(level, std::memory_order_relaxed); }

	/// @brief 現在のログレベルを返す（スレッド安全）
	[[nodiscard]] LogLevel getLevel() const noexcept { return m_level.load(std::memory_order_relaxed); }

	/// @brief Sinkを追加する（スレッド安全）
	/// @tparam T LogSinkコンセプトを満たす型
	/// @param sink Sinkオブジェクト
	template <LogSink T>
	void addSink(T sink)
	{
		std::lock_guard lock(m_mutex);
		m_sinks.push_back([s = std::move(sink)](const LogMessage& msg) {
			s.write(msg);
		});
	}

	/// @brief 全Sinkを削除する（スレッド安全）
	void clearSinks()
	{
		std::lock_guard lock(m_mutex);
		m_sinks.clear();
	}

private:
	std::atomic<LogLevel> m_level;
	std::vector<std::function<void(const LogMessage&)>> m_sinks;
	mutable std::mutex m_mutex;  ///< m_sinks アクセス用ミューテックス
};

} // namespace sgc

// ── マクロ（source_location自動付与）────────────────────────────

/// @brief Traceレベルログマクロ
#define SGC_LOG_TRACE(logger, ...) \
	(logger).log(sgc::LogLevel::Trace, std::source_location::current(), __VA_ARGS__)

/// @brief Debugレベルログマクロ
#define SGC_LOG_DEBUG(logger, ...) \
	(logger).log(sgc::LogLevel::Debug, std::source_location::current(), __VA_ARGS__)

/// @brief Infoレベルログマクロ
#define SGC_LOG_INFO(logger, ...) \
	(logger).log(sgc::LogLevel::Info, std::source_location::current(), __VA_ARGS__)

/// @brief Warnレベルログマクロ
#define SGC_LOG_WARN(logger, ...) \
	(logger).log(sgc::LogLevel::Warn, std::source_location::current(), __VA_ARGS__)

/// @brief Errorレベルログマクロ
#define SGC_LOG_ERROR(logger, ...) \
	(logger).log(sgc::LogLevel::Error, std::source_location::current(), __VA_ARGS__)

/// @brief Fatalレベルログマクロ
#define SGC_LOG_FATAL(logger, ...) \
	(logger).log(sgc::LogLevel::Fatal, std::source_location::current(), __VA_ARGS__)
