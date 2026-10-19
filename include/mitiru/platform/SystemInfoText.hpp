#pragma once

/// @file SystemInfoText.hpp
/// @brief Windows と CPU の診断用文字列を返す、グラフィックス API に依存しない宣言。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <string>

namespace mitiru::platform::sysinfo
{

[[nodiscard]] inline std::string osVersionText()
{
	using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
	OSVERSIONINFOW v{};
	v.dwOSVersionInfoSize = sizeof(v);
	if (HMODULE nt = ::GetModuleHandleW(L"ntdll.dll"))
	{
		if (auto fn = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(nt, "RtlGetVersion")); fn != nullptr && fn(&v) == 0)
		{
			char b[64];
			std::snprintf(b, sizeof(b), "Windows %lu.%lu (build %lu)", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
			return b;
		}
	}
	return "Windows (版を読めない)";
}

/// @brief CPU がこの実行で持つ拡張命令。AVX2 を持たない CPU で落ちたときの切り分けに使う
[[nodiscard]] inline std::string cpuFeatureText()
{
	std::string s;
	auto add = [&s](DWORD feature, const char* name) {
		if (::IsProcessorFeaturePresent(feature))
		{
			s += s.empty() ? "" : " ";
			s += name;
		}
	};
	add(PF_SSE3_INSTRUCTIONS_AVAILABLE, "SSE3");
	add(PF_SSSE3_INSTRUCTIONS_AVAILABLE, "SSSE3");
	add(PF_SSE4_1_INSTRUCTIONS_AVAILABLE, "SSE4.1");
	add(PF_SSE4_2_INSTRUCTIONS_AVAILABLE, "SSE4.2");
	add(PF_AVX_INSTRUCTIONS_AVAILABLE, "AVX");
	add(PF_AVX2_INSTRUCTIONS_AVAILABLE, "AVX2");
	add(PF_AVX512F_INSTRUCTIONS_AVAILABLE, "AVX512F");
	return s.empty() ? std::string("SSE2 のみ") : s;
}

}  // namespace mitiru::platform::sysinfo

#endif  // _WIN32
