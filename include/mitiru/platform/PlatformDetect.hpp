#pragma once

/// @file PlatformDetect.hpp
/// @brief constexpr プラットフォーム検出ユーティリティ
/// @details コンパイル時にOS・アーキテクチャ・プラットフォームカテゴリを判定する。
///          PlatformInfo.hpp のランタイム検出に対し、こちらは完全に constexpr。
///
/// @code
/// using namespace mitiru::platform;
/// static_assert(currentOS() == OS::Windows);
/// if constexpr (isDesktop()) { /* desktop-only code */ }
/// @endcode

#include <cstdint>

#if defined(__APPLE__)
	#include <TargetConditionals.h>
#endif

namespace mitiru::platform
{

/// @brief OS種別
enum class OS : std::uint8_t
{
	Windows,  ///< Windows
	Linux,    ///< Linux
	macOS,    ///< macOS / Mac Catalyst
	iOS,      ///< iOS / iPadOS
	Android,  ///< Android (NDK)
	Web,      ///< Emscripten / WebAssembly
	Unknown   ///< 未検出
};

/// @brief CPUアーキテクチャ種別
enum class Arch : std::uint8_t
{
	x86,      ///< x86 (32-bit)
	x64,      ///< x86_64 / AMD64
	ARM,      ///< ARM (32-bit)
	ARM64,    ///< AArch64 / ARM64
	WASM,     ///< WebAssembly
	Unknown   ///< 未検出
};

// ── OS detection ────────────────────────────────────────────

/// @brief コンパイル時のOS種別を返す
/// @return 現在のOS
[[nodiscard]] constexpr OS currentOS() noexcept
{
#if defined(__EMSCRIPTEN__)
	return OS::Web;
#elif defined(__ANDROID__)
	return OS::Android;
#elif defined(__APPLE__)
	#if TARGET_OS_IOS || TARGET_OS_SIMULATOR
		return OS::iOS;
	#else
		return OS::macOS;
	#endif
#elif defined(_WIN32)
	return OS::Windows;
#elif defined(__linux__)
	return OS::Linux;
#else
	return OS::Unknown;
#endif
}

// ── Architecture detection ──────────────────────────────────

/// @brief コンパイル時のCPUアーキテクチャを返す
/// @return 現在のアーキテクチャ
[[nodiscard]] constexpr Arch currentArch() noexcept
{
#if defined(__EMSCRIPTEN__) || defined(__wasm__)
	return Arch::WASM;
#elif defined(_M_ARM64) || defined(__aarch64__)
	return Arch::ARM64;
#elif defined(_M_ARM) || defined(__arm__)
	return Arch::ARM;
#elif defined(_M_X64) || defined(__x86_64__)
	return Arch::x64;
#elif defined(_M_IX86) || defined(__i386__)
	return Arch::x86;
#else
	return Arch::Unknown;
#endif
}

// ── Platform categories ─────────────────────────────────────

/// @brief デスクトッププラットフォームか判定する
/// @return Windows / Linux / macOS なら true
[[nodiscard]] constexpr bool isDesktop() noexcept
{
	return currentOS() == OS::Windows
		|| currentOS() == OS::Linux
		|| currentOS() == OS::macOS;
}

/// @brief モバイルプラットフォームか判定する
/// @return iOS / Android なら true
[[nodiscard]] constexpr bool isMobile() noexcept
{
	return currentOS() == OS::iOS
		|| currentOS() == OS::Android;
}

/// @brief Webプラットフォームか判定する
/// @return Emscripten/WASM なら true
[[nodiscard]] constexpr bool isWeb() noexcept
{
	return currentOS() == OS::Web;
}

// ── Name strings ────────────────────────────────────────────

/// @brief OS名を文字列リテラルで返す
/// @return OS名（null終端文字列）
[[nodiscard]] constexpr const char* osName() noexcept
{
	switch (currentOS())
	{
	case OS::Windows: return "Windows";
	case OS::Linux:   return "Linux";
	case OS::macOS:   return "macOS";
	case OS::iOS:     return "iOS";
	case OS::Android: return "Android";
	case OS::Web:     return "Web";
	case OS::Unknown: return "Unknown";
	}
	return "Unknown";
}

/// @brief アーキテクチャ名を文字列リテラルで返す
/// @return アーキテクチャ名（null終端文字列）
[[nodiscard]] constexpr const char* archName() noexcept
{
	switch (currentArch())
	{
	case Arch::x86:     return "x86";
	case Arch::x64:     return "x64";
	case Arch::ARM:     return "ARM";
	case Arch::ARM64:   return "ARM64";
	case Arch::WASM:    return "WASM";
	case Arch::Unknown: return "Unknown";
	}
	return "Unknown";
}

} // namespace mitiru::platform
