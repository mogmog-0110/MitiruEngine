#pragma once

/// @file Dx12CallStack.hpp
/// @brief 検証エラーを起こした呼び出し元（関数名とファイル名:行番号）を端末に出力する機能の宣言。`MITIRU_D3D12_DEBUG_STACK=1` のときだけ有効。
/// @details debug layer の callback は、エラーを起こした API 呼び出しと同じ thread で呼ばれる。その場で call stack を取得すると、原因となったエンジンの描画処理を特定できる。
/// dbghelp.dll は実行時に読み込み、link しない。行番号は pdb が exe の隣にあるときだけ出力する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dbghelp.h>

#include <atomic>
#include <mutex>
#include <cstdio>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/ConsoleOut.hpp>

namespace mitiru::gfx::validation
{

namespace detail
{

struct DbgHelpApi
{
	using SymInitializeFn = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
	using SymFromAddrFn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
	using SymGetLineFn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
	using SymSetOptionsFn = DWORD(WINAPI*)(DWORD);

	SymFromAddrFn fromAddr = nullptr;
	SymGetLineFn line = nullptr;
	bool ready = false;
};

[[nodiscard]] inline DbgHelpApi& dbgHelp()
{
	static DbgHelpApi api = [] {
		DbgHelpApi a;
		HMODULE mod = ::LoadLibraryW(L"dbghelp.dll");
		if (mod == nullptr) { return a; }
		const auto init = reinterpret_cast<DbgHelpApi::SymInitializeFn>(::GetProcAddress(mod, "SymInitialize"));
		const auto options = reinterpret_cast<DbgHelpApi::SymSetOptionsFn>(::GetProcAddress(mod, "SymSetOptions"));
		a.fromAddr = reinterpret_cast<DbgHelpApi::SymFromAddrFn>(::GetProcAddress(mod, "SymFromAddr"));
		a.line = reinterpret_cast<DbgHelpApi::SymGetLineFn>(::GetProcAddress(mod, "SymGetLineFromAddr64"));
		if (init == nullptr || options == nullptr || a.fromAddr == nullptr || a.line == nullptr) { return a; }
		options(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
		a.ready = init(::GetCurrentProcess(), nullptr, TRUE) != FALSE;
		return a;
	}();
	return api;
}

}  // namespace detail

/// @brief 現在の call stack を 1 行ずつ端末に出力する。エラーの連鎖による大量出力を防ぐため、上限回数を超えたら出力しない。
inline void printCallStack(const char* what)
{
	static std::atomic<int> printed{0};
	constexpr int kMaxPrints = 4;
	constexpr int kMaxFrames = 28;
	if (printed.fetch_add(1) >= kMaxPrints) { return; }
	// dbghelp は処理全体で状態を共有し、複数の thread から同時には呼べない
	static std::mutex mutex;
	const std::lock_guard<std::mutex> lock(mutex);

	void* frames[kMaxFrames] = {};
	const USHORT count = ::CaptureStackBackTrace(1, kMaxFrames, frames, nullptr);
	auto& api = detail::dbgHelp();
	console::noticef("呼び出しの場所 (%s):", what);
	const HANDLE process = ::GetCurrentProcess();
	for (USHORT i = 0; i < count; ++i)
	{
		const auto addr = reinterpret_cast<DWORD64>(frames[i]);
		char name[256] = "(不明)";
		char where[300] = "";
		if (api.ready)
		{
			alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
			auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			sym->MaxNameLen = 255;
			if (api.fromAddr(process, addr, nullptr, sym)) { std::snprintf(name, sizeof(name), "%s", sym->Name); }
			IMAGEHLP_LINE64 line{};
			line.SizeOfStruct = sizeof(line);
			DWORD displacement = 0;
			if (api.line(process, addr, &displacement, &line)) { std::snprintf(where, sizeof(where), " (%s:%lu)", line.FileName, line.LineNumber); }
		}
		console::noticef("  #%u %s%s", static_cast<unsigned>(i), name, where);
	}
}

[[nodiscard]] inline bool callStackRequested()
{
	static const bool requested = env::flag("MITIRU_D3D12_DEBUG_STACK");
	return requested;
}

}  // namespace mitiru::gfx::validation

#endif // _WIN32
