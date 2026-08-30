/// @file Utf8Args.hpp
/// @brief Windows の ANSI 境界を UTF-8 で越えるための変換 (引数・コマンドライン)。
///
/// エンジンは文字列を全部 UTF-8 で持つ。ところが Windows には UTF-8 でない境界が
/// 二つある:
///
///   1. CRT の `argv`。コマンドラインを **ANSI (日本語環境では CP932)** へ変換した
///      ものが渡る。UTF-8 として読むと日本語の引数が化ける。
///   2. `CreateProcessW` へ渡すコマンドライン。UTF-16。UTF-8 の文字列を
///      `std::wstring(s.begin(), s.end())` のようにバイト単位で広げると、1 バイトが
///      1 文字になって壊れる。
///
/// この 2 か所を素通りさせたせいで、配布物の窓の表題が化けた (`--title` に日本語を
/// 渡す経路)。同じ罠を各 app が個別に踏まないよう、変換はここに 1 つだけ置く。
///
/// Windows 以外では OS が UTF-8 なので、素通しの実装になる。
#pragma once

#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
// windows.h の min/max マクロは、後から来る std::min / std::max を壊す
// (sgc の Vec3 がこれで落ちた)。このヘッダが先に読まれても巻き込まないようにする。
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW (WIN32_LEAN_AND_MEAN では windows.h から外れる)
#ifdef _MSC_VER
// CommandLineToArgvW は shell32。engine に link しない小さな app (ランチャ・梱包
// ツール) がこのヘッダだけ使えるよう、ここで link 指定まで済ませる。
#pragma comment(lib, "shell32")
#endif
#endif

namespace mitiru::platform
{

#ifdef _WIN32

/// @brief UTF-16 → UTF-8。
inline std::string wideToUtf8(std::wstring_view w)
{
	if (w.empty()) { return {}; }
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
	                                  nullptr, 0, nullptr, nullptr);
	if (n <= 0) { return {}; }
	std::string out(static_cast<std::size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
	                    out.data(), n, nullptr, nullptr);
	return out;
}

/// @brief UTF-8 → UTF-16。
///
/// `std::wstring(s.begin(), s.end())` の代わりに必ずこれを使うこと。あちらは
/// UTF-8 のバイト列を 1 バイト 1 文字として広げるので、ASCII 以外が壊れる。
inline std::wstring utf8ToWide(std::string_view s)
{
	if (s.empty()) { return {}; }
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0) { return {}; }
	std::wstring out(static_cast<std::size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

/// @brief プロセスのコマンドラインを UTF-8 の argv として取り直す。
///
/// CRT の `argv` は ANSI 変換済みで日本語が壊れているので、UTF-16 の原本から作る。
/// 失敗したときは空を返す (呼び手は CRT の argv を使い続けてよい)。
inline std::vector<std::string> commandLineUtf8Args()
{
	std::vector<std::string> out;
	int     wargc = 0;
	LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
	if (wargv == nullptr) { return out; }
	out.reserve(static_cast<std::size_t>(wargc));
	for (int i = 0; i < wargc; ++i) { out.push_back(wideToUtf8(wargv[i])); }
	LocalFree(wargv);
	return out;
}

#else

inline std::string  wideToUtf8(std::wstring_view) { return {}; }
inline std::wstring utf8ToWide(std::string_view)  { return {}; }

/// 非 Windows では argv がそのまま UTF-8。取り直す意味が無いので空を返す。
inline std::vector<std::string> commandLineUtf8Args() { return {}; }

#endif

}  // namespace mitiru::platform
