#pragma once

/// @file Log.hpp
/// @brief グローバルログアクセサ
/// @details エンジン全体で使用するグローバルロガーへのアクセスを提供する。
///          デフォルトはNullLogger（出力なし）。
///          setLogger()でConsoleLogger/FileLoggerに切り替え可能。
///
/// @code
/// // 初期化時
/// mitiru::debug::Log::setLogger(std::make_shared<mitiru::debug::ConsoleLogger>());
///
/// // 任意の場所から使用
/// MITIRU_LOG_DEBUG("Render", "Pipeline initialized");
/// MITIRU_LOG_WARN("Audio", "Mixer channel full");
/// @endcode

#include <memory>

#include <mitiru/debug/ILogger.hpp>
#include <mitiru/debug/NullLogger.hpp>

namespace mitiru::debug
{

/// @brief グローバルログアクセサ
class Log
{
public:
	/// @brief グローバルロガーを取得する
	/// @return 現在のロガーへの参照
	[[nodiscard]] static ILogger& get() noexcept
	{
		return *instance().m_logger;
	}

	/// @brief グローバルロガーを設定する
	/// @param logger 新しいロガー（nullptrの場合はNullLoggerに戻る）
	static void setLogger(std::shared_ptr<ILogger> logger)
	{
		if (logger)
		{
			instance().m_logger = std::move(logger);
		}
		else
		{
			instance().m_logger = std::make_shared<NullLogger>();
		}
	}

	/// @brief ロガーをデフォルト（NullLogger）にリセットする
	static void reset()
	{
		instance().m_logger = std::make_shared<NullLogger>();
	}

private:
	Log()
		: m_logger(std::make_shared<NullLogger>())
	{
	}

	[[nodiscard]] static Log& instance()
	{
		static Log s_instance;
		return s_instance;
	}

	std::shared_ptr<ILogger> m_logger;
};

} // namespace mitiru::debug

// ── 便利マクロ ──

/// @brief トレースログ出力マクロ
#define MITIRU_LOG_TRACE(cat, msg) \
	::mitiru::debug::Log::get().trace(cat, msg)

/// @brief デバッグログ出力マクロ
#define MITIRU_LOG_DEBUG(cat, msg) \
	::mitiru::debug::Log::get().debug(cat, msg)

/// @brief 情報ログ出力マクロ
#define MITIRU_LOG_INFO(cat, msg) \
	::mitiru::debug::Log::get().info(cat, msg)

/// @brief 警告ログ出力マクロ
#define MITIRU_LOG_WARN(cat, msg) \
	::mitiru::debug::Log::get().warn(cat, msg)

/// @brief エラーログ出力マクロ
#define MITIRU_LOG_ERROR(cat, msg) \
	::mitiru::debug::Log::get().error(cat, msg)
