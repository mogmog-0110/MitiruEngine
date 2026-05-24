#pragma once

/// @file DebugPrint.hpp
/// @brief ユーザーコードから inspector window へ任意行を送るための薄い API
/// @details
/// gameplay コードが `mitiru::debug::println("hp = " + std::to_string(hp))`
/// と書くと、その文字列が process-local の ring buffer に積まれる。
/// inspector subsystem は snapshot 出力時にここを drain して JSON に乗せ、
/// inspector window が "Debug Log" セクションとして時系列描画する。
///
/// 設計判断:
/// - thread-safe (内部 mutex)。複数スレッドから安全に呼べる
/// - ring buffer 上限 256 行。古いものから捨てる
/// - printf-style 別エントリポイント (`printf`) を用意 — 既存 C コード移植が楽
/// - 出力先 logger とは独立 (ILogger は category 付きで別 concern)
///
/// @code
/// // game code, anywhere:
/// mitiru::debug::println("checkpoint reached");
/// mitiru::debug::printf("hp=%d remaining=%.1f", hp, remaining);
///
/// // engine internal: read & format for the inspector snapshot
/// for (const auto& line : mitiru::debug::DebugPrintBuffer::snapshot()) { ... }
/// @endcode

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace mitiru::debug
{

/// @brief プロセス内 1 インスタンスの ring buffer
/// @details すべて static — シングルトン (mutex 同期付き)。
///          tests / fixture から手動 reset したい場合は `clear()`。
class DebugPrintBuffer
{
public:
	/// @brief 行を 1 つ追加する。古いものから捨てて上限 kCap を守る
	static void push(std::string line)
	{
		std::lock_guard<std::mutex> lock(mutex());
		auto& buf = lines();
		buf.push_back(std::move(line));
		while (buf.size() > kCap)
		{
			buf.pop_front();
		}
	}

	/// @brief 現時点の行のコピーを返す (drain しない)
	[[nodiscard]] static std::vector<std::string> snapshot()
	{
		std::lock_guard<std::mutex> lock(mutex());
		const auto& buf = lines();
		return {buf.begin(), buf.end()};
	}

	/// @brief 全行を削除する
	static void clear()
	{
		std::lock_guard<std::mutex> lock(mutex());
		lines().clear();
	}

	/// @brief 現在の行数
	[[nodiscard]] static std::size_t size()
	{
		std::lock_guard<std::mutex> lock(mutex());
		return lines().size();
	}

	/// @brief 容量上限 (ring buffer cap)
	static constexpr std::size_t kCap = 256;

private:
	static std::mutex& mutex()
	{
		static std::mutex m;
		return m;
	}
	static std::deque<std::string>& lines()
	{
		static std::deque<std::string> buf;
		return buf;
	}
};

/// @brief 1 行を inspector の Debug Log に流す
/// @details string literal / std::string / std::string_view すべて受ける。
///          C 文字列 + std::string の overload を分けると ambiguity になるので
///          string_view 1 本に統一してある。
inline void println(std::string_view line)
{
	DebugPrintBuffer::push(std::string{line});
}

/// @brief printf-style で 1 行流す
/// @details 内部で 512 バイトの stack buffer に format、超過分は truncate
inline void printf(const char* fmt, ...)
{
	std::array<char, 512> buf{};
	va_list ap;
	va_start(ap, fmt);
	const int n = std::vsnprintf(buf.data(), buf.size(), fmt, ap);
	va_end(ap);
	if (n <= 0) { return; }
	const std::size_t take = (static_cast<std::size_t>(n) < buf.size())
		? static_cast<std::size_t>(n)
		: buf.size() - 1;
	DebugPrintBuffer::push(std::string(buf.data(), take));
}

}  // namespace mitiru::debug
