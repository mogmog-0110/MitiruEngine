#pragma once

/// @file HostCrashHandler.hpp
/// @brief game の callback の外 (host 本体や game が立てたスレッド) で落ちた時に、minidump と報告を残す。
/// @details ModuleFaultGuard が拾えない所の最後の受け皿で、書き終えたらプロセスはそのまま終わる。何も送らない。
///          送るのは次の起動で利用者が許した後 (CrashInbox.hpp)。sentry-native を組み込んだ host では、
///          後から入る sentry の受け皿が先に呼ばれる。

#include <filesystem>
#include <string_view>

#include <mitiru/debug/CrashReport.hpp>
#include <mitiru/module/ModuleFaultGuard.hpp>

#if MITIRU_HAS_SEH_GUARD
#include <crtdbg.h>
#include <csignal>
#include <cstdlib>
#include <cwchar>
#endif

namespace mitiru::debug
{

#if MITIRU_HAS_SEH_GUARD

namespace detail
{

struct HostCrashState
{
	wchar_t       logPath[1024] = {};
	volatile LONG entered = 0;
	bool          installed = false;
};

inline HostCrashState& hostCrashState() noexcept
{
	static HostCrashState state;
	return state;
}

// 次の起動は last_run.log を空にするので、落ちた実行のログは報告の隣へ同じ名前で写しておく。
inline void copyRunLog(const module::ModuleFault& fault) noexcept
{
	const auto& state = hostCrashState();
	if (state.logPath[0] == L'\0' || fault.dumpPath[0] == '\0') { return; }
	wchar_t dst[1100] = {};
	if (::MultiByteToWideChar(CP_UTF8, 0, fault.dumpPath, -1, dst, 1024) <= 0) { return; }
	wchar_t* dot = ::wcsrchr(dst, L'.');
	if (dot == nullptr) { return; }
	::wcscpy_s(dot, static_cast<rsize_t>(dst + 1100 - dot), L".log");
	::CopyFileW(state.logPath, dst, FALSE);
}

inline LONG WINAPI hostCrashFilter(EXCEPTION_POINTERS* ep)
{
	// 受け皿の中でまた落ちたら、何もせずに OS へ渡す。
	if (::InterlockedExchange(&hostCrashState().entered, 1) != 0) { return EXCEPTION_CONTINUE_SEARCH; }
	module::ModuleFault fault{};
	module::detail::captureFault(ep, fault);
	copyRunLog(fault);
	(void)writeCrashReport(fault, crashContext(), "host stopped");
	return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] inline void raiseHostFault(DWORD code) noexcept
{
	::RaiseException(code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	std::_Exit(3);
}

// abort は terminate の中から呼ばれることがある。その間は捕まえていない例外がまだ生きているので、
// host が理由を読み出せるよう、受け皿に入る前に 1 回だけ呼ぶ。MSVC の set_terminate は呼んだスレッドにしか効かず、
// 別スレッドの例外はここを通る
using AbortHook = void (*)();

inline AbortHook& abortHook() noexcept
{
	static AbortHook hook = nullptr;
	return hook;
}

inline void onPureCall() { raiseHostFault(0x40000015u); }
inline void onAbortSignal(int)
{
	if (const AbortHook hook = abortHook(); hook != nullptr) { hook(); }
	raiseHostFault(0x40000015u);
}
inline void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t)
{
	raiseHostFault(0xC000000Du);
}

}  // namespace detail

/// @brief host の main の最初に 1 回呼ぶ。debugger の下では OS がこの受け皿を呼ばない (落ちた行で止まる)。
inline void installHostCrashHandler()
{
	auto& state = detail::hostCrashState();
	if (state.installed) { return; }
	state.installed = true;
	module::setFaultDumpDirectory(crashDirectory());
	::SetUnhandledExceptionFilter(&detail::hostCrashFilter);
	// abort・純粋仮想関数の呼び出し・CRT の引数の誤りは、そのままでは受け皿を通らずに終わる。例外に変えて通す。
	_set_purecall_handler(&detail::onPureCall);
	_set_invalid_parameter_handler(&detail::onInvalidParameter);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	std::signal(SIGABRT, &detail::onAbortSignal);
	// スタックが溢れた後でも受け皿が動けるよう、溢れた時に使える分を残す。
	ULONG guarantee = 64 * 1024;
	::SetThreadStackGuarantee(&guarantee);
}

/// @brief abort (捕まえていない例外の terminate を含む) の直前に 1 回呼ぶ関数を決める
inline void setHostAbortHook(detail::AbortHook hook) noexcept { detail::abortHook() = hook; }

/// @brief 人が見ていない実行 (headless・入力の台本・replay) で、Debug の CRT の assert と警告を標準エラーへ流す。
///        既定のままだとメッセージボックスが人の画面に出て、実行もそこで止まる
inline void routeCrtReportsToStderr()
{
	::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_set_error_mode(_OUT_TO_STDERR);
	for (const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
	}
}

/// @brief 出力先の無い GUI の host が書くログ (HostGuiLog の last_run.log)。落ちた時に報告の隣へ写す。
inline void setHostCrashLogFile(const std::filesystem::path& log)
{
	::wcsncpy_s(detail::hostCrashState().logPath, log.native().c_str(), _TRUNCATE);
}

#else

inline void installHostCrashHandler() {}
inline void setHostAbortHook(void (*)()) noexcept {}
inline void routeCrtReportsToStderr() {}
inline void setHostCrashLogFile(const std::filesystem::path&) {}

#endif

/// @brief 出荷するゲームの名前を決める。報告の置き場が %LOCALAPPDATA%/MitiruGames/<ゲーム>/crashes になる。
inline void setCrashGameName(std::string_view gameName)
{
	setCrashContextText(crashContext().gameName, gameName);
	module::setFaultDumpDirectory(crashDirectory());
}

}  // namespace mitiru::debug
