/// @file CodePageGuard.hpp
/// @brief ANSI が UTF-8 でない Windows で、ASCII 以外のパスに入ったときの知らせ。
///
/// exe の manifest (cmake/mitiru_utf8.manifest) が効くのは Windows 10 1903 以降。それより古いと
/// ANSI が CP932 のままで、UTF-8 の文字列から作る path が例外になり、日本語のフォルダに置いた
/// ゲームは何も出さずに終わる。起動の最初に見つけて、短い文で理由を知らせる。
#pragma once

#ifdef _WIN32

#include <initializer_list>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mitiru/platform/Utf8Args.hpp>

namespace mitiru::platform
{

inline constexpr const char* kCodePageNotice =
	"この Windows は古く、日本語などを含むフォルダでは動かないので、英数字だけのフォルダに置いてください";

[[nodiscard]] inline bool hasNonAscii(std::wstring_view w)
{
	for (const wchar_t c : w)
	{
		if (c >= 0x80) { return true; }
	}
	return false;
}

/// @brief ANSI が UTF-8 でなく、places のどれかに ASCII 以外が入っていれば知らせの文を返す。問題なければ空
[[nodiscard]] inline std::string codePageProblem(UINT activeCodePage, std::initializer_list<std::wstring_view> places)
{
	if (activeCodePage == CP_UTF8) { return {}; }
	for (const std::wstring_view p : places)
	{
		if (hasNonAscii(p)) { return kCodePageNotice; }
	}
	return {};
}

/// @brief 今のプロセスの exe、作業フォルダ、コマンドライン、利用者のデータの置き場を調べる
[[nodiscard]] inline std::string currentCodePageProblem()
{
	if (GetACP() == CP_UTF8) { return {}; }
	const auto env = [](const wchar_t* name) {
		std::wstring v;
		const DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
		if (need > 0)
		{
			v.resize(need);
			const DWORD got = GetEnvironmentVariableW(name, v.data(), need);
			v.resize(got < need ? got : 0);
		}
		return v;
	};
	std::wstring exe(MAX_PATH, L'\0');
	exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
	std::wstring cwd(MAX_PATH, L'\0');
	cwd.resize(GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()), cwd.data()));
	const wchar_t* cmd = GetCommandLineW();
	return codePageProblem(GetACP(), {exe, cwd, cmd != nullptr ? std::wstring_view(cmd) : std::wstring_view(),
	                                  env(L"LOCALAPPDATA"), env(L"APPDATA")});
}

}  // namespace mitiru::platform

#endif  // _WIN32
