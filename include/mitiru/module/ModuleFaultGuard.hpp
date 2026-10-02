#pragma once

/// @file ModuleFaultGuard.hpp
/// @brief game DLL の callback を SEH で包み、フォールトから host を守る宣言。
/// @details フォールトの情報を ModuleFault に記録し、設定されていれば minidump を書く。Windows と MSVC 以外ではそのまま呼ぶ。

#include <cstdint>
#include <filesystem>
#include <type_traits>
#include <utility>

#if defined(_WIN32) && defined(_MSC_VER)
#define MITIRU_HAS_SEH_GUARD 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <malloc.h>
#include <cstdio>
#include <cwchar>
#else
#define MITIRU_HAS_SEH_GUARD 0
#endif

namespace mitiru::module
{

/// @brief game callback 内のフォールト情報。例外処理中でも書けるよう、すべて固定長で持つ
struct ModuleFault
{
	std::uint32_t  code          = 0;   ///< 例外コード (EXCEPTION_ACCESS_VIOLATION 等)
	std::uintptr_t address       = 0;   ///< 例外が起きた命令のアドレス
	std::uintptr_t moduleOffset  = 0;   ///< address の所属モジュール先頭からのオフセット
	std::uintptr_t accessAddress = 0;   ///< アクセス違反の対象アドレス
	int            accessKind    = -1;  ///< 0=読み 1=書き 8=実行 (アクセス違反以外は -1)
	char           moduleName[260] = {};  ///< 所属モジュールのファイル名 (UTF-8)
	char           dumpPath[1024]  = {};  ///< 書いた minidump (UTF-8、書いていなければ空)
	const char*    callback      = "";  ///< 落ちた callback 名 ("on_update" 等の静的文字列)
};

/// @brief 例外コードの短い名前。表にないコードは "UNKNOWN_EXCEPTION"
[[nodiscard]] inline const char* describeFaultCode(std::uint32_t code) noexcept
{
	switch (code)
	{
	case 0xC0000005u: return "ACCESS_VIOLATION";
	case 0xC00000FDu: return "STACK_OVERFLOW";
	case 0xC0000094u: return "INT_DIVIDE_BY_ZERO";
	case 0xC0000095u: return "INT_OVERFLOW";
	case 0xC000008Eu: return "FLT_DIVIDE_BY_ZERO";
	case 0xC0000090u: return "FLT_INVALID_OPERATION";
	case 0xC000001Du: return "ILLEGAL_INSTRUCTION";
	case 0xC0000096u: return "PRIVILEGED_INSTRUCTION";
	case 0xC000008Cu: return "ARRAY_BOUNDS_EXCEEDED";
	case 0xC0000006u: return "IN_PAGE_ERROR";
	case 0xC0000374u: return "HEAP_CORRUPTION";
	case 0x80000003u: return "BREAKPOINT";
	case 0xE06D7363u: return "CPP_EXCEPTION";
	default:          return "UNKNOWN_EXCEPTION";
	}
}

#if MITIRU_HAS_SEH_GUARD

namespace detail
{

using MiniDumpWriteDumpFn = decltype(&::MiniDumpWriteDump);

// フォールト処理中に heap を使わないよう、書き先は固定長で持つ。
struct FaultDumpState
{
	wchar_t             dir[1024] = {};
	wchar_t             label[260] = {};  ///< ファイル名に入れる名前 (空なら落ちたモジュール名)
	volatile LONG       sequence = 0;     ///< 同じ秒に続けて落ちても名前が重ならないための通し番号
	MiniDumpWriteDumpFn writeDump = nullptr;
};

inline FaultDumpState& faultDumpState() noexcept
{
	static FaultDumpState state;
	return state;
}

inline void wideToUtf8(const wchar_t* src, char* dst, int dstBytes) noexcept
{
	const int n = ::WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstBytes, nullptr, nullptr);
	if (n <= 0 && dstBytes > 0) { dst[0] = '\0'; }
}

inline void resolveFaultModule(ModuleFault& out) noexcept
{
	HMODULE mod = nullptr;
	if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
	                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                          reinterpret_cast<LPCWSTR>(out.address), &mod))
	{
		return;
	}
	out.moduleOffset = out.address - reinterpret_cast<std::uintptr_t>(mod);
	wchar_t path[MAX_PATH] = {};
	if (::GetModuleFileNameW(mod, path, MAX_PATH) == 0) { return; }
	const wchar_t* base = path;
	for (const wchar_t* p = path; *p != L'\0'; ++p)
	{
		if (*p == L'\\' || *p == L'/') { base = p + 1; }
	}
	wideToUtf8(base, out.moduleName, static_cast<int>(sizeof(out.moduleName)));
}

// 壊れた heap でも動くよう、std::filesystem を使わず Win32 で親から順に作る。
inline void createDirectoryChain(wchar_t* dir) noexcept
{
	for (std::size_t i = 1; dir[i] != L'\0'; ++i)
	{
		const bool sep = (dir[i] == L'\\' || dir[i] == L'/') && dir[i - 1] != L':';
		if (!sep) { continue; }
		const wchar_t keep = dir[i];
		dir[i] = L'\0';
		::CreateDirectoryW(dir, nullptr);
		dir[i] = keep;
	}
	::CreateDirectoryW(dir, nullptr);
}

struct FaultDumpJob
{
	EXCEPTION_POINTERS* exception = nullptr;
	DWORD               threadId  = 0;
	ModuleFault*        out       = nullptr;
};

inline void writeFaultDump(const FaultDumpJob& job) noexcept
{
	auto& state = faultDumpState();
	if (state.dir[0] == L'\0' || state.writeDump == nullptr) { return; }

	wchar_t dir[1024] = {};
	::wcsncpy_s(dir, state.dir, _TRUNCATE);
	createDirectoryChain(dir);

	wchar_t stem[260] = {};
	if (state.label[0] != L'\0')
	{
		::wcsncpy_s(stem, state.label, _TRUNCATE);
	}
	else
	{
		const char* name = job.out->moduleName[0] != '\0' ? job.out->moduleName : "unknown";
		::MultiByteToWideChar(CP_UTF8, 0, name, -1, stem, 260);
		if (wchar_t* dot = ::wcsrchr(stem, L'.')) { dot[0] = L'\0'; }
	}

	SYSTEMTIME t{};
	::GetLocalTime(&t);
	wchar_t path[1400] = {};
	::swprintf_s(path, L"%s\\%04u%02u%02u-%02u%02u%02u-%lu-%ld-%s.dmp", dir,
	             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
	             ::GetCurrentProcessId(), ::InterlockedIncrement(&state.sequence), stem);

	const HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
	                                  FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) { return; }
	MINIDUMP_EXCEPTION_INFORMATION info{};
	info.ThreadId          = job.threadId;
	info.ExceptionPointers = job.exception;
	info.ClientPointers    = FALSE;
	const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);
	const BOOL ok = state.writeDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type,
	                                &info, nullptr, nullptr);
	::CloseHandle(file);
	if (ok) { wideToUtf8(path, job.out->dumpPath, static_cast<int>(sizeof(job.out->dumpPath))); }
	else    { ::DeleteFileW(path); }
}

inline DWORD WINAPI faultWorker(LPVOID param)
{
	const auto& job = *static_cast<const FaultDumpJob*>(param);
	resolveFaultModule(*job.out);
	writeFaultDump(job);
	return 0;
}

/// @brief EXCEPTION_POINTERS から ModuleFault を埋め、minidump を書く。
/// @details スタック溢れに備え、別スレッドのスタックで処理する。
inline void captureFault(EXCEPTION_POINTERS* ep, ModuleFault& out) noexcept
{
	out = ModuleFault{};
	const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
	out.code    = rec->ExceptionCode;
	out.address = reinterpret_cast<std::uintptr_t>(rec->ExceptionAddress);
	if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
	    && rec->NumberParameters >= 2)
	{
		out.accessKind    = static_cast<int>(rec->ExceptionInformation[0]);
		out.accessAddress = static_cast<std::uintptr_t>(rec->ExceptionInformation[1]);
	}
	FaultDumpJob job{ep, ::GetCurrentThreadId(), &out};
	const HANDLE worker = ::CreateThread(nullptr, 0, &faultWorker, &job, 0, nullptr);
	if (worker == nullptr)
	{
		faultWorker(&job);
		return;
	}
	// 落ちたスレッドが heap のロックを持ったままだと、dump を書くスレッドは永久に待つ。
	// 待ちを打ち切るなら、このスタック上の job を後から触られないよう止めてから戻る。
	if (::WaitForSingleObject(worker, 15000) != WAIT_OBJECT_0)
	{
		::TerminateThread(worker, 1);
		out.dumpPath[0] = '\0';
	}
	::CloseHandle(worker);
}

inline LONG faultFilter(EXCEPTION_POINTERS* ep, ModuleFault* out) noexcept
{
	// debugger では原因の行で止めるため、フォールトを捕捉しない。
	if (::IsDebuggerPresent()) { return EXCEPTION_CONTINUE_SEARCH; }
	captureFault(ep, *out);
	return EXCEPTION_EXECUTE_HANDLER;
}

// __try を持つ関数はデストラクタ付きの局所変数を持てないため、呼び出しを分ける。
inline bool sehInvoke(void (*fn)(void*), void* ctx, ModuleFault* out)
{
	__try
	{
		fn(ctx);
		return true;
	}
	__except (faultFilter(GetExceptionInformation(), out))
	{
		if (out->code == EXCEPTION_STACK_OVERFLOW) { ::_resetstkoflw(); }
		return false;
	}
}

}  // namespace detail

/// @brief minidump の書き先を設定する。空の path ならフォールトだけを記録する
/// @param label ファイル名に入れる名前。空なら落ちたモジュールの名前
inline void setFaultDumpDirectory(const std::filesystem::path& dir, const std::filesystem::path& label = {})
{
	auto& state = detail::faultDumpState();
	::wcsncpy_s(state.dir, dir.native().c_str(), _TRUNCATE);
	::wcsncpy_s(state.label, label.native().c_str(), _TRUNCATE);
	if (state.dir[0] != L'\0' && state.writeDump == nullptr)
	{
		// consumer が dbghelp.lib を link せずに済むよう、実行時に読み込む。
		if (HMODULE dbghelp = ::LoadLibraryW(L"dbghelp.dll"))
		{
			state.writeDump = reinterpret_cast<detail::MiniDumpWriteDumpFn>(
				::GetProcAddress(dbghelp, "MiniDumpWriteDump"));
		}
	}
}

#else

inline void setFaultDumpDirectory(const std::filesystem::path&, const std::filesystem::path& = {}) {}

#endif

/// @brief fn() を呼び、フォールトを out に記録して false を返す
/// @details 成功時は out を変えない。毎フレーム呼ぶため、heap を確保しない。C ABI を越えた C++ 例外もフォールトとして扱う。
template <class F>
[[nodiscard]] bool callGuarded(const char* callback, ModuleFault& out, F&& fn)
{
#if MITIRU_HAS_SEH_GUARD
	using Fn = std::remove_reference_t<F>;
	auto thunk = [](void* p) { (*static_cast<Fn*>(p))(); };
	if (detail::sehInvoke(thunk, const_cast<void*>(static_cast<const void*>(&fn)), &out)) { return true; }
	out.callback = callback;
	return false;
#else
	// SEH の無い環境では C++ 例外だけを拾う (ヌル書き込み等のシグナルは拾えない)
	try
	{
		std::forward<F>(fn)();
		return true;
	}
	catch (...)
	{
		out.code     = 0xE06D7363u;
		out.callback = callback;
		return false;
	}
#endif
}

}  // namespace mitiru::module
