#pragma once

/// @file SpdlogBridge.hpp
/// @brief spdlog統合ブリッジ
/// @details spdlogが利用可能な場合はspdlogを使用し、
///          利用不可の場合は既存のILoggerシステムにフォールバックする。
///
/// @code
/// // 初期化
/// mitiru::debug::LogSystem::init("game.log", mitiru::debug::LogSystemLevel::Debug);
///
/// // ログ出力（マクロ経由）
/// MITIRU_SLOG_INFO("Engine initialized: version={}", 2);
/// MITIRU_SLOG_WARN("Frame budget exceeded");
/// @endcode

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/debug/ILogger.hpp>
#include <mitiru/debug/Log.hpp>
#include <mitiru/debug/ConsoleLogger.hpp>
#include <mitiru/debug/FileLogger.hpp>

// ── spdlog統合マクロ ──

#ifdef MITIRU_HAS_SPDLOG

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/async.h>

#define MITIRU_SLOG_TRACE(...)    spdlog::trace(__VA_ARGS__)
#define MITIRU_SLOG_DEBUG(...)    spdlog::debug(__VA_ARGS__)
#define MITIRU_SLOG_INFO(...)     spdlog::info(__VA_ARGS__)
#define MITIRU_SLOG_WARN(...)     spdlog::warn(__VA_ARGS__)
#define MITIRU_SLOG_ERROR(...)    spdlog::error(__VA_ARGS__)
#define MITIRU_SLOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)

#else

// spdlog未使用時: 既存ILoggerシステムへルーティング
// NOTE: フォールバックモードではfmt形式の引数置換は行われない。
//       msgの文字列リテラルのみがログ出力される。
#define MITIRU_SLOG_TRACE(msg, ...)  \
	::mitiru::debug::Log::get().debug("App", msg)
#define MITIRU_SLOG_DEBUG(msg, ...)  \
	::mitiru::debug::Log::get().debug("App", msg)
#define MITIRU_SLOG_INFO(msg, ...)  \
	::mitiru::debug::Log::get().info("App", msg)
#define MITIRU_SLOG_WARN(msg, ...)  \
	::mitiru::debug::Log::get().warn("App", msg)
#define MITIRU_SLOG_ERROR(msg, ...) \
	::mitiru::debug::Log::get().error("App", msg)
#define MITIRU_SLOG_CRITICAL(msg, ...) \
	::mitiru::debug::Log::get().error("App", msg)

#endif

namespace mitiru::debug
{

/// @brief LogSystem用のログレベル
enum class LogSystemLevel : std::uint8_t
{
	Trace = 0,
	Debug,
	Info,
	Warn,
	Error,
	Critical,
	Off,
};

/// @brief ログシンクの種別
enum class SinkType : std::uint8_t
{
	Console, ///< コンソール出力
	File,    ///< ファイル出力
	Custom,  ///< カスタムILoggerベース
};

/// @brief シンク設定
struct SinkConfig
{
	SinkType type = SinkType::Console;
	std::string filePath;                        ///< File型の場合のパス
	std::shared_ptr<ILogger> customLogger;       ///< Custom型の場合のロガー
};

/// @brief spdlog統合ログシステム
/// @details spdlog利用可能時はasyncモード・パターンフォーマット・カラーコンソールを提供。
///          利用不可時は既存のConsoleLogger/FileLoggerに委譲する。
/// @note スレッド安全性: init()/setLevel()/addSink()/shutdown()は
///       単一スレッドから呼び出すこと。spdlogモードではspdlog自身がスレッド安全性を保証するが、
///       フォールバックモードのLogSystem内部状態は排他制御されていない。
class LogSystem
{
public:
	/// @brief ログシステムを初期化する
	/// @param logFile ログファイルパス（空文字列の場合はファイル出力なし）
	/// @param level 初期ログレベル
	static void init(const std::string& logFile = "",
	                 LogSystemLevel level = LogSystemLevel::Info)
	{
		auto& sys = instance();
		sys.m_level = level;
		sys.m_logFile = logFile;
		sys.m_sinks.clear();

#ifdef MITIRU_HAS_SPDLOG
		// spdlog: asyncモードで初期化
		spdlog::init_thread_pool(8192, 1);

		std::vector<spdlog::sink_ptr> sinks;

		// カラーコンソールシンク
		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		sinks.push_back(consoleSink);

		// ファイルシンク
		if (!logFile.empty())
		{
			auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
			sinks.push_back(fileSink);
		}

		auto logger = std::make_shared<spdlog::async_logger>(
			"mitiru", sinks.begin(), sinks.end(),
			spdlog::thread_pool(),
			spdlog::async_overflow_policy::block);

		logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		logger->set_level(toSpdlogLevel(level));

		spdlog::set_default_logger(std::move(logger));
#else
		// フォールバック: 既存ロガーを使用
		auto consoleLogger = std::make_shared<ConsoleLogger>();
		consoleLogger->setMinLevel(toILoggerLevel(level));

		if (!logFile.empty())
		{
			// ファイルロガーをラップ
			sys.m_fileLogger = std::make_shared<FileLogger>(logFile);
			sys.m_fileLogger->setMinLevel(toILoggerLevel(level));
		}

		Log::setLogger(consoleLogger);
		sys.m_consoleLogger = consoleLogger;
#endif
	}

	/// @brief ログレベルを変更する
	/// @param level 新しいログレベル
	static void setLevel(LogSystemLevel level)
	{
		auto& sys = instance();
		sys.m_level = level;

#ifdef MITIRU_HAS_SPDLOG
		spdlog::set_level(toSpdlogLevel(level));
#else
		if (sys.m_consoleLogger)
		{
			sys.m_consoleLogger->setMinLevel(toILoggerLevel(level));
		}
		if (sys.m_fileLogger)
		{
			sys.m_fileLogger->setMinLevel(toILoggerLevel(level));
		}
#endif
	}

	/// @brief バッファをフラッシュする
	static void flush()
	{
#ifdef MITIRU_HAS_SPDLOG
		spdlog::default_logger()->flush();
#else
		// ILoggerベースのロガーは毎行flushしているため追加操作不要
#endif
	}

	/// @brief シンクを追加する
	/// @param config シンク設定
	static void addSink(const SinkConfig& config)
	{
		auto& sys = instance();
		sys.m_sinks.push_back(config);

#ifdef MITIRU_HAS_SPDLOG
		// spdlogの場合: 動的にシンクを追加
		auto logger = spdlog::default_logger();
		switch (config.type)
		{
		case SinkType::Console:
		{
			auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			logger->sinks().push_back(sink);
			break;
		}
		case SinkType::File:
		{
			if (!config.filePath.empty())
			{
				auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
					config.filePath, true);
				logger->sinks().push_back(sink);
			}
			break;
		}
		case SinkType::Custom:
			// spdlogモードではカスタムシンクは設定として保持のみ
			break;
		}
#else
		// フォールバック: カスタムロガーの場合はLogに設定
		if (config.type == SinkType::Custom && config.customLogger)
		{
			config.customLogger->setMinLevel(toILoggerLevel(sys.m_level));
		}
		else if (config.type == SinkType::File && !config.filePath.empty())
		{
			sys.m_fileLogger = std::make_shared<FileLogger>(config.filePath);
			sys.m_fileLogger->setMinLevel(toILoggerLevel(sys.m_level));
		}
#endif
	}

	/// @brief 現在のログレベルを取得する
	[[nodiscard]] static LogSystemLevel level() noexcept
	{
		return instance().m_level;
	}

	/// @brief ログシステムをシャットダウンする
	static void shutdown()
	{
#ifdef MITIRU_HAS_SPDLOG
		spdlog::shutdown();
#else
		auto& sys = instance();
		sys.m_consoleLogger.reset();
		sys.m_fileLogger.reset();
		sys.m_sinks.clear();
		Log::reset();
#endif
	}

private:
	LogSystem() = default;

	[[nodiscard]] static LogSystem& instance()
	{
		static LogSystem s_instance;
		return s_instance;
	}

#ifdef MITIRU_HAS_SPDLOG
	/// @brief LogSystemLevelをspdlog::levelに変換する
	[[nodiscard]] static spdlog::level::level_enum toSpdlogLevel(LogSystemLevel level) noexcept
	{
		switch (level)
		{
		case LogSystemLevel::Trace:    return spdlog::level::trace;
		case LogSystemLevel::Debug:    return spdlog::level::debug;
		case LogSystemLevel::Info:     return spdlog::level::info;
		case LogSystemLevel::Warn:     return spdlog::level::warn;
		case LogSystemLevel::Error:    return spdlog::level::err;
		case LogSystemLevel::Critical: return spdlog::level::critical;
		case LogSystemLevel::Off:      return spdlog::level::off;
		default:                       return spdlog::level::info;
		}
	}
#endif

	/// @brief LogSystemLevelをILogger::LogLevelに変換する
	[[nodiscard]] static LogLevel toILoggerLevel(LogSystemLevel level) noexcept
	{
		switch (level)
		{
		case LogSystemLevel::Trace:    return LogLevel::Trace;
		case LogSystemLevel::Debug:    return LogLevel::Debug;
		case LogSystemLevel::Info:     return LogLevel::Info;
		case LogSystemLevel::Warn:     return LogLevel::Warn;
		case LogSystemLevel::Error:    return LogLevel::Error;
		case LogSystemLevel::Critical: return LogLevel::Error;
		case LogSystemLevel::Off:      return LogLevel::Error;
		default:                       return LogLevel::Info;
		}
	}

	LogSystemLevel m_level = LogSystemLevel::Info;
	std::string m_logFile;
	std::vector<SinkConfig> m_sinks;
	std::shared_ptr<ConsoleLogger> m_consoleLogger;
	std::shared_ptr<FileLogger> m_fileLogger;
};

} // namespace mitiru::debug
