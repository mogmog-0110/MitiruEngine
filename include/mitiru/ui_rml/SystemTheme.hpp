#pragma once

/// @file SystemTheme.hpp
/// @brief ツール窓や開発用の窓が、Windows のアプリのテーマ (ライト / ダーク) に合わせるための問い合わせ
/// @details 環境変数 MITIRU_UI_THEME=light|dark があればそれを優先する (撮影と、もう一方の見た目の確認用)。
///          Windows 以外は常にライト。

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace mitiru::ui_rml
{

[[nodiscard]] inline bool systemPrefersDark()
{
	if (const char* forced = std::getenv("MITIRU_UI_THEME"))
	{
		if (std::strcmp(forced, "dark") == 0) { return true; }
		if (std::strcmp(forced, "light") == 0) { return false; }
	}
#ifdef _WIN32
	DWORD value = 1;
	DWORD size = sizeof(value);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
	                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
	{
		return value == 0;
	}
#endif
	return false;
}

} // namespace mitiru::ui_rml
