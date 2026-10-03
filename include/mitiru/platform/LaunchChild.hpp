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

/// @brief exe の読み込み中に終了したときのコードを利用者向けの文にする。該当しなければ nullptr
/// @details main より前の失敗は host 自身で通知できない。
[[nodiscard]] inline const wchar_t* loaderFailureText(DWORD code)
{
	switch (code)
	{
	case 0xC0000135: return L"ゲームに必要な DLL が見つかりません (0xC0000135)。";
	case 0xC0000139: return L"ゲームの DLL の版が合いません (0xC0000139)。";
	case 0xC000007B: return L"ゲームの DLL が壊れているか、64 bit 版ではありません (0xC000007B)。";
	default: return nullptr;
	}
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
