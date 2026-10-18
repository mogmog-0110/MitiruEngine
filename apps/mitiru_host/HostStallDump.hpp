#pragma once

/// @file HostStallDump.hpp
/// @brief フレームが止まったときに全スレッドのスタックを minidump に残し、端末に保存先を表示する
/// @details 書き込みには installHostCrashHandler が読み込んだ dbghelp を使う。止まったスレッドが heap の鍵を持っていても書けるよう、名前は固定長バッファで作る。dump の書き込みが失敗しても残るよう、同じ場所の stall.log に 1 件 1 行で先に書く。窓の応答は、frame thread が最後に伝えた HWND へ監視スレッドから問い合わせる (窓の入れ替わりと競合しない)。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/debug/FrameStallWatch.hpp>
#include <mitiru/module/ModuleFaultGuard.hpp>

namespace mitiru::host
{

/// ウィンドウが「応答なし」になる 5 秒より前に書き終える
inline constexpr std::chrono::milliseconds kFrameStallLimit{3000};
/// 起動直後に UI の文字を焼く数フレーム（数秒かかる）は、停止とみなさない
inline constexpr std::uint32_t kFrameStallArmAfter = 120;

#if MITIRU_HAS_SEH_GUARD
/// frame thread が毎フレーム更新する。監視スレッドは engine.window() を直接触らない
struct StallProbeState
{
	std::atomic<std::uintptr_t> hwnd{0};
};

inline bool windowNotAnswering(void* user)
{
	const auto h = reinterpret_cast<HWND>(static_cast<StallProbeState*>(user)->hwnd.load(std::memory_order_acquire));
	if (h == nullptr || !::IsWindow(h)) { return false; }
	if (::IsHungAppWindow(h)) { return true; }
	DWORD_PTR ignored = 0;
	return ::SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored) == 0;
}

/// heap を使わずに追記する。停止したスレッドが heap の鍵を持っていても書ける
inline void appendStallLog(const wchar_t* dir, const char* line, int len)
{
	wchar_t path[1100] = {};
	::swprintf_s(path, L"%s\\stall.log", dir);
	const HANDLE f = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
	                               FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f == INVALID_HANDLE_VALUE) { return; }
	DWORD written = 0;
	::WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
	::CloseHandle(f);
}

inline void writeStallDump(void*, const debug::FrameStallWatch::Info& info)
{
	const bool frames = info.kind == debug::FrameStallWatch::Kind::FramesStalled;
	const char* cond  = frames ? "frames stalled" : "window not answering";
	auto& state = module::detail::faultDumpState();
	if (state.writeDump == nullptr || state.dir[0] == L'\0')
	{
		console::notice("止まった場所を書く dbghelp.dll を読めませんでした");
		return;
	}
	module::detail::createDirectoryChain(state.dir);
	SYSTEMTIME t{};
	::GetLocalTime(&t);
	wchar_t path[1400] = {};
	::swprintf_s(path, L"%s\\%04u%02u%02u-%02u%02u%02u-%lu-stall.dmp", state.dir, t.wYear, t.wMonth, t.wDay,
	             t.wHour, t.wMinute, t.wSecond, ::GetCurrentProcessId());
	char utf8[1400] = {};
	module::detail::wideToUtf8(path, utf8, static_cast<int>(sizeof(utf8)));

	char line[1600] = {};
	const int n = ::sprintf_s(line, "%04u-%02u-%02u %02u:%02u:%02u %s stalled=%lldms frame=%llu dump=%s\r\n", t.wYear,
	                          t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, cond,
	                          static_cast<long long>(info.stalledFor.count()),
	                          static_cast<unsigned long long>(info.frame), utf8);
	if (n > 0) { appendStallLog(state.dir, line, n); }

	const HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		static constexpr char kOpenFailed[] = "  dump file could not be created\r\n";
		appendStallLog(state.dir, kOpenFailed, static_cast<int>(sizeof(kOpenFailed) - 1));
		return;
	}
	const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
	const BOOL ok = state.writeDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type, nullptr, nullptr, nullptr);
	::CloseHandle(file);
	if (!ok)
	{
		::DeleteFileW(path);
		static constexpr char kWriteFailed[] = "  MiniDumpWriteDump failed\r\n";
		appendStallLog(state.dir, kWriteFailed, static_cast<int>(sizeof(kWriteFailed) - 1));
		return;
	}
	if (frames) { console::noticef("フレームが止まったので、%s に書きました", utf8); }
	else { console::noticef("窓が応答しないので、%s に書きました", utf8); }
}
#else
struct StallProbeState
{
	std::atomic<std::uintptr_t> hwnd{0};
};
inline bool windowNotAnswering(void*) { return false; }
inline void writeStallDump(void*, const debug::FrameStallWatch::Info&) {}
#endif

} // namespace mitiru::host
