#pragma once

/// @file ConsoleOut.hpp
/// @brief engine・host が端末へ出す文の出口
/// @details 普通に起動して普通に動いた実行では何も出さない。出すのは 2 種類だけにする。
///          notice は使う人が何かを直す必要がある知らせで、常に出す。verbose はエンジンやゲームを
///          作る人が中を調べるときの詳細で、`MITIRU_LOG=verbose` か host の `--verbose` のときだけ出す。
///          どちらも行頭に `mitiru: ` を付ける。ゲーム DLL に入ったこのヘッダーは host と別の static を
///          持つので、詳細を出すかどうかは環境変数を正にする (host の --verbose は環境変数も立てる)。

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace mitiru::console
{

/// @brief 1 行を受け取る出力先。既定は stderr。テストは差し替えて中身を確かめる
using Sink = void (*)(std::string_view line);

namespace detail
{

inline void stderrSink(std::string_view line)
{
	std::fwrite(line.data(), 1, line.size(), stderr);
	std::fflush(stderr);
}

[[nodiscard]] inline std::atomic<Sink>& sinkSlot() noexcept
{
	static std::atomic<Sink> s{&stderrSink};
	return s;
}

/// -1 = まだ環境変数を読んでいない、0 = 静か、1 = 詳細も出す
[[nodiscard]] inline std::atomic<int>& verboseSlot() noexcept
{
	static std::atomic<int> v{-1};
	return v;
}

[[nodiscard]] inline bool envSaysVerbose() noexcept
{
	const char* e = std::getenv("MITIRU_LOG");
	return e != nullptr && std::strcmp(e, "verbose") == 0;
}

inline void emit(std::string_view msg)
{
	std::string line;
	line.reserve(msg.size() + 9);
	line += "mitiru: ";
	line += msg;
	if (line.back() != '\n') { line += '\n'; }
	sinkSlot().load()(line);
}

[[nodiscard]] inline std::string vformat(const char* fmt, va_list ap)
{
	std::array<char, 1024> buf{};
	const int n = std::vsnprintf(buf.data(), buf.size(), fmt, ap);
	if (n <= 0) { return {}; }
	const auto len = static_cast<std::size_t>(n) < buf.size() ? static_cast<std::size_t>(n) : buf.size() - 1;
	return std::string(buf.data(), len);
}

} // namespace detail

/// @brief 詳細も出す実行か。初回に `MITIRU_LOG` を読み、以後は setVerbose で変えたものを返す
[[nodiscard]] inline bool isVerbose() noexcept
{
	auto& v = detail::verboseSlot();
	int cur = v.load(std::memory_order_relaxed);
	if (cur < 0)
	{
		cur = detail::envSaysVerbose() ? 1 : 0;
		v.store(cur, std::memory_order_relaxed);
	}
	return cur == 1;
}

inline void setVerbose(bool on) noexcept
{
	detail::verboseSlot().store(on ? 1 : 0, std::memory_order_relaxed);
}

/// @brief 次の isVerbose で環境変数を読み直させる (テスト用)
inline void resetVerboseForTest() noexcept
{
	detail::verboseSlot().store(-1, std::memory_order_relaxed);
}

/// @brief 出力先を差し替え、前の出力先を返す。nullptr で stderr に戻る
inline Sink setSink(Sink sink) noexcept
{
	return detail::sinkSlot().exchange(sink != nullptr ? sink : &detail::stderrSink);
}

/// @brief 使う人が何かを直す必要がある知らせ。何が起きたかと、どうすればよいかを 1〜2 文で書く
inline void notice(std::string_view msg) { detail::emit(msg); }

inline void noticef(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const std::string msg = detail::vformat(fmt, ap);
	va_end(ap);
	detail::emit(msg);
}

/// @brief 中を調べる人のための詳細。詳細を出す実行でだけ出す
inline void verbose(std::string_view msg)
{
	if (isVerbose()) { detail::emit(msg); }
}

inline void verbosef(const char* fmt, ...)
{
	if (!isVerbose()) { return; }
	va_list ap;
	va_start(ap, fmt);
	const std::string msg = detail::vformat(fmt, ap);
	va_end(ap);
	detail::emit(msg);
}

} // namespace mitiru::console
