#pragma once

/// @file OptionalDll.hpp
/// @brief 遅延読み込みにした DLL を、使う前に読めるか確かめる。
/// @details 遅延読み込みの DLL が読めないと、最初の呼び出しで SEH 例外 (0xC06D007E) が飛ぶ。C++ の catch では捕まらず
///          host ごと落ちるため、呼ぶ側はこの確認を先に通して、読めなければその機能だけを諦める。
///          MITIRU_TEST_DLL_MISSING に DLL 名をカンマで並べると、その DLL は読めないものとして扱う (試験用)。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cwchar>
#include <string>

namespace mitiru::platform
{

[[nodiscard]] inline bool dllMarkedMissing(const wchar_t* dll)
{
	wchar_t buf[512] = {};
	const DWORD n = GetEnvironmentVariableW(L"MITIRU_TEST_DLL_MISSING", buf, 512);
	if (n == 0 || n >= 512) { return false; }
	const std::wstring list = std::wstring(L",") + buf + L",";
	return list.find(std::wstring(L",") + dll + L",") != std::wstring::npos;
}

/// @brief exe の隣と System32 から dll を読めるか。読めたものは解放せず、遅延読み込みがそのまま使う
[[nodiscard]] inline bool optionalDllLoadable(const wchar_t* dll)
{
	if (dllMarkedMissing(dll)) { return false; }
	if (GetModuleHandleW(dll) != nullptr) { return true; }
	return LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_APPLICATION_DIR) != nullptr;
}

}  // namespace mitiru::platform

#else

namespace mitiru::platform
{
[[nodiscard]] inline bool optionalDllLoadable(const wchar_t*) { return true; }
}  // namespace mitiru::platform

#endif
