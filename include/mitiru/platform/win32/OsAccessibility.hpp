#pragma once

/// @file OsAccessibility.hpp
/// @brief Windows のハイコントラスト、アニメーション効果、透明効果の設定を読み、InputSnapshot::osAccessibility の bit にする。
/// @details 環境変数 MITIRU_OS_A11Y があれば、PC の設定に関係なく同じ値で撮影やスクリプトの実行ができるように、その値を使う。値の形式は module/detail/HostEnvironment62.hpp の parseOsAccessibilityOverride に従う。Windows 以外では 0。

#include <cstdint>
#include <optional>
#include <string>

#include <mitiru/core/Env.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/module/detail/HostEnvironment62.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace mitiru::platform
{

/// OS から読んだ生の値。OS を使わずに bit への変換を単体試験できるよう、読み取りと解釈を分けている
struct OsAccessibilityRaw
{
	bool highContrastOn = false;
	bool clientAreaAnimationOn = true;
	bool transparencyOn = true;
};

#ifdef _WIN32
[[nodiscard]] inline OsAccessibilityRaw readOsAccessibilityRaw()
{
	OsAccessibilityRaw raw;
	HIGHCONTRASTW hc{};
	hc.cbSize = sizeof(hc);
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) { raw.highContrastOn = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0; }
	BOOL animation = TRUE;
	if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0)) { raw.clientAreaAnimationOn = animation != FALSE; }
	// 透明効果は SPI ではなく、設定アプリが書くレジストリの値を使う。保存場所は ui_rml/SystemTheme.hpp と同じ
	DWORD transparency = 1;
	DWORD size = sizeof(transparency);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
	                 L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size) == ERROR_SUCCESS)
	{
		raw.transparencyOn = transparency != 0;
	}
	return raw;
}
#else
[[nodiscard]] inline OsAccessibilityRaw readOsAccessibilityRaw() { return {}; }
#endif

/// 環境変数 MITIRU_OS_A11Y が読める値なら、その bit を返す。無い場合や読めない場合は nullopt。PC の設定に関係なく同じ値でスクリプトや撮影を実行できるよう、値を明示したときだけ使う
[[nodiscard]] inline std::optional<std::uint8_t> forcedOsAccessibility()
{
	const std::string forced = env::value("MITIRU_OS_A11Y");
	if (forced.empty()) { return std::nullopt; }
	if (const auto bits = module::detail::parseOsAccessibilityOverride(forced)) { return bits; }
	debug::verboseOnce("os_a11y.env", "MITIRU_OS_A11Y の値 " + forced + " を読めないので、OS の設定を使います。"
	                   "high_contrast、reduce_motion、reduce_transparency、none のどれかを , で区切って書いてください");
	return std::nullopt;
}

/// 環境変数 MITIRU_OS_A11Y が読める値ならその値を使い、無ければ OS の設定を使う
[[nodiscard]] inline std::uint8_t readOsAccessibility()
{
	if (const auto forced = forcedOsAccessibility()) { return *forced; }
	const OsAccessibilityRaw raw = readOsAccessibilityRaw();
	return module::detail::osAccessibilityBits(raw.highContrastOn, raw.clientAreaAnimationOn, raw.transparencyOn);
}

}  // namespace mitiru::platform
