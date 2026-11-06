/// @file FloatEnvironment.hpp
/// @brief 数学関数の経路を host の起動時に固定し、使った環境を調べる (Windows x64)。
///
/// UCRT の超越関数は FMA3 を持つ CPU では別のアルゴリズムに入る。ゲームの DLL は動的 CRT で host と
/// ucrtbase.dll を共有するので、host が先に 1 回切れば DLL の sin / cos にも効く。
/// 切っても Windows のバージョン差は残る (その分は `ucrt` として記録するだけ)。
#pragma once

#include <mitiru/platform/FloatEnvTag.hpp>

#if defined(_WIN32) && defined(_M_X64)

#include <cmath>
#include <cstdlib>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#pragma comment(lib, "version.lib")

namespace mitiru::platform
{

namespace detail
{
[[nodiscard]] inline bool cpuHasFma3()
{
	int r[4] = {};
	__cpuid(r, 1);
	const bool fma = (r[2] & (1 << 12)) != 0;
	const bool osxsave = (r[2] & (1 << 27)) != 0;
	const bool avx = (r[2] & (1 << 28)) != 0;
	return fma && osxsave && avx;
}

/// Debug の CRT (/MDd) は別の DLL で、_set_FMA3_enable もそちらを切り替える。計算している DLL の方を
/// 調べ、Debug は末尾に d を付けて Release の記録と区別する
[[nodiscard]] inline std::string ucrtVersion()
{
#ifdef _DEBUG
	const HMODULE mod = GetModuleHandleW(L"ucrtbased.dll");
	const char* suffix = "d";
#else
	const HMODULE mod = GetModuleHandleW(L"ucrtbase.dll");
	const char* suffix = "";
#endif
	if (mod == nullptr) { return {}; }
	wchar_t path[MAX_PATH] = {};
	if (GetModuleFileNameW(mod, path, MAX_PATH) == 0) { return {}; }
	const DWORD size = GetFileVersionInfoSizeW(path, nullptr);
	if (size == 0) { return {}; }
	std::vector<char> buf(size);
	if (!GetFileVersionInfoW(path, 0, size, buf.data())) { return {}; }
	void* fixed = nullptr;
	UINT len = 0;
	if (!VerQueryValueW(buf.data(), L"\\", &fixed, &len) || len < sizeof(VS_FIXEDFILEINFO)) { return {}; }
	const auto* v = static_cast<const VS_FIXEDFILEINFO*>(fixed);
	return std::to_string(HIWORD(v->dwProductVersionMS)) + "." + std::to_string(LOWORD(v->dwProductVersionMS)) + "."
	     + std::to_string(HIWORD(v->dwProductVersionLS)) + "." + std::to_string(LOWORD(v->dwProductVersionLS)) + suffix;
}
}  // namespace detail

/// @brief 今の数学関数の環境を調べる (切り替えはしない)
[[nodiscard]] inline FloatEnv currentFloatEnv()
{
	FloatEnv e;
	e.cpuFma3 = detail::cpuHasFma3();
	e.fma3InUse = _get_FMA3_enable() != 0;
	e.ucrt = detail::ucrtVersion();
	return e;
}

/// @brief FMA3 を使わない経路に固定してから環境を返す。`MITIRU_FMA3=auto` なら CRT の既定のまま。
/// @details FMA3 が有る PC と無い PC で結果が変わらないようにするのが目的。FMA3 の有る PC で録った
///          古い録画を同じ種類の PC で再生し直すときのために、既定へ戻す口を残す。
inline FloatEnv pinFma3Off()
{
	const char* mode = std::getenv("MITIRU_FMA3");
	if (mode == nullptr || std::string_view(mode) != "auto") { (void)_set_FMA3_enable(0); }
	return currentFloatEnv();
}

}  // namespace mitiru::platform

#else

namespace mitiru::platform
{
/// Windows x64 以外には UCRT の切り替えが無い。調べられないので空の環境を返す
[[nodiscard]] inline FloatEnv currentFloatEnv() { return {}; }
inline FloatEnv pinFma3Off() { return {}; }
}  // namespace mitiru::platform

#endif
