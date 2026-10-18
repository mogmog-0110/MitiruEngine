#pragma once

/// @file UserDataDir.hpp
/// @brief 出荷したゲームがセーブと設定を置く場所 (Windows は %APPDATA%/<game>/)。
/// @details exe の隣は Program Files に入ると書けず、Steam の検証で消されることもある。
///          ユーザーごとのデータはユーザーのプロファイルへ置く。

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <mitiru/core/Env.hpp>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#  include <ShlObj.h>
#  pragma comment(lib, "shell32.lib")
#  pragma comment(lib, "ole32.lib")
#endif

namespace mitiru::save
{

/// @brief ゲーム名をフォルダ名に使える形にする。パス区切りと Windows で使えない文字を `_` にし、
///        末尾の空白とピリオドを落とす。UTF-8 の日本語はそのまま残す。空になったら "MitiruGame"。
[[nodiscard]] inline std::string sanitizeDirName(std::string_view name)
{
	std::string out;
	out.reserve(name.size());
	for (const char c : name)
	{
		const auto u = static_cast<unsigned char>(c);
		const bool bad = u < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/'
		              || c == '\\' || c == '|' || c == '?' || c == '*';
		out.push_back(bad ? '_' : c);
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '.')) { out.pop_back(); }
	if (out.empty()) { out = "MitiruGame"; }
	return out;
}

/// @brief ユーザーごとのデータの根。Windows は Roaming AppData、他は $XDG_DATA_HOME か ~/.local/share。
///        取れなければ今の作業フォルダ。Windows は環境変数 MITIRU_USER_DATA_ROOT があればそれを根にする。
[[nodiscard]] inline std::filesystem::path userDataRoot()
{
#ifdef _WIN32
	// 利用者のフォルダの名前に CP932 に無い文字がある場合のテストと、データを別の場所へ置く運用のための指定。
	// SHGetKnownFolderPath は環境変数に従わないので、ここで先に読む
	if (const std::wstring forced = env::wideValue(L"MITIRU_USER_DATA_ROOT"); !forced.empty()) { return std::filesystem::path(forced); }
	PWSTR appData = nullptr;
	std::filesystem::path base;
	if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) { base = appData; }
	::CoTaskMemFree(appData);
	if (!base.empty()) { return base; }
#else
	if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') { return xdg; }
	if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
	{
		return std::filesystem::path(home) / ".local" / "share";
	}
#endif
	std::error_code ec;
	return std::filesystem::current_path(ec);
}

/// @brief <根>/<ゲーム名>。gameName は UTF-8。
[[nodiscard]] inline std::filesystem::path gameDataDir(std::string_view gameName)
{
	const std::string dir = sanitizeDirName(gameName);
	return userDataRoot() / std::filesystem::path(std::u8string(dir.begin(), dir.end()));
}

}  // namespace mitiru::save
