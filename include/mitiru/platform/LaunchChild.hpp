/// @file LaunchChild.hpp
/// @brief 配布用ランチャーが host を起動し、終了まで待つ処理。
///
/// ランチャーは engine をリンクしない Win32 専用の exe のため、Win32 と標準ライブラリにのみ依存する。
/// Steam の起動オプションや headless での確認に使うため、ランチャーの引数は host へそのまま渡す。
/// 標準出力と標準エラーに出力先があれば host に引き継ぎ、なければ host がログファイルへ書く。
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace mitiru::platform
{

/// 自分の exe のパス。MAX_PATH で切ると、深い置き場で末尾が欠けた別のパスになる
[[nodiscard]] inline std::wstring thisExePath()
{
	std::wstring buf(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
		if (n == 0) { return {}; }
		if (n < buf.size()) { buf.resize(n); return buf; }
		buf.resize(buf.size() * 2);
	}
}

[[nodiscard]] inline std::wstring commandLineTail()
{
	const wchar_t* p = GetCommandLineW();
	if (p == nullptr) { return {}; }
	if (*p == L'"')
	{
		++p;
		while (*p != L'\0' && *p != L'"') { ++p; }
		if (*p == L'"') { ++p; }
	}
	else
	{
		while (*p != L'\0' && *p != L' ' && *p != L'\t') { ++p; }
	}
	while (*p == L' ' || *p == L'\t') { ++p; }
	return p;
}

[[nodiscard]] inline bool hasStdHandle(DWORD which)
{
	const HANDLE h = GetStdHandle(which);
	return h != nullptr && h != INVALID_HANDLE_VALUE;
}

inline bool runAndWait(const std::wstring& exe, std::wstring cmd, const std::wstring& cwd, DWORD& exitCode)
{
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	const bool passStd = hasStdHandle(STD_OUTPUT_HANDLE) || hasStdHandle(STD_ERROR_HANDLE);
	if (passStd)
	{
		si.dwFlags    = STARTF_USESTDHANDLES;
		si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
		si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
	}
	PROCESS_INFORMATION pi{};
	if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, passStd ? TRUE : FALSE, 0, nullptr,
	                    cwd.c_str(), &si, &pi))
	{
		return false;
	}
	CloseHandle(pi.hThread);
	WaitForSingleObject(pi.hProcess, INFINITE);
	exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	return true;
}

}  // namespace mitiru::platform

#endif  // _WIN32
