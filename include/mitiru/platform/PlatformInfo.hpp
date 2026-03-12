#pragma once

/// @file PlatformInfo.hpp
/// @brief プラットフォーム検出
/// @details コンパイル時・ランタイムのプラットフォーム情報を検出する。

#include <cstdint>
#include <string>
#include <vector>

namespace mitiru
{

/// @brief OS種別
enum class OsType : std::uint8_t
{
	Unknown = 0,  ///< 不明
	Windows,      ///< Windows
	Linux,        ///< Linux
	MacOS,        ///< macOS
	Emscripten    ///< Emscripten/WASM
};

/// @brief CPU アーキテクチャ種別
enum class CpuArch : std::uint8_t
{
	Unknown = 0,  ///< 不明
	X86,          ///< x86（32bit）
	X64,          ///< x86_64（64bit）
	Arm32,        ///< ARM（32bit）
	Arm64,        ///< ARM（64bit / AArch64）
	Wasm          ///< WebAssembly
};

/// @brief グラフィックスバックエンド利用可否
struct AvailableBackends
{
	bool dx11 = false;       ///< DirectX 11
	bool dx12 = false;       ///< DirectX 12
	bool vulkan = false;     ///< Vulkan
	bool opengl = false;     ///< OpenGL
	bool webgl = false;      ///< WebGL
	bool null = true;        ///< NullDevice（常に利用可能）

	/// @brief 利用可能なバックエンド名のリストを取得する
	/// @return バックエンド名の文字列リスト
	[[nodiscard]] std::vector<std::string> list() const
	{
		std::vector<std::string> result;
		if (dx11)    result.emplace_back("DX11");
		if (dx12)    result.emplace_back("DX12");
		if (vulkan)  result.emplace_back("Vulkan");
		if (opengl)  result.emplace_back("OpenGL");
		if (webgl)   result.emplace_back("WebGL");
		if (null)    result.emplace_back("Null");
		return result;
	}

	/// @brief 利用可能なバックエンド数を取得する
	/// @return 利用可能数
	[[nodiscard]] int count() const noexcept
	{
		int n = 0;
		if (dx11)   ++n;
		if (dx12)   ++n;
		if (vulkan) ++n;
		if (opengl) ++n;
		if (webgl)  ++n;
		if (null)   ++n;
		return n;
	}
};

/// @brief ウィンドウバックエンド利用可否
struct AvailableWindowBackends
{
	bool win32 = false;   ///< Win32
	bool sdl2 = false;    ///< SDL2
	bool glfw = false;    ///< GLFW

	/// @brief 利用可能なウィンドウバックエンド名のリストを取得する
	/// @return バックエンド名の文字列リスト
	[[nodiscard]] std::vector<std::string> list() const
	{
		std::vector<std::string> result;
		if (win32) result.emplace_back("Win32");
		if (sdl2)  result.emplace_back("SDL2");
		if (glfw)  result.emplace_back("GLFW");
		return result;
	}
};

/// @brief プラットフォーム情報
/// @details コンパイル時のOS・アーキテクチャ・利用可能なバックエンドを保持する。
///
/// @code
/// auto info = PlatformInfo::detect();
/// if (info.backends.vulkan)
/// {
///     // Vulkanバックエンドが利用可能
/// }
/// for (const auto& name : info.backends.list())
/// {
///     // 利用可能なバックエンド名を列挙
/// }
/// @endcode
struct PlatformInfo
{
	OsType os = OsType::Unknown;           ///< OS種別
	CpuArch arch = CpuArch::Unknown;       ///< CPUアーキテクチャ
	AvailableBackends backends;             ///< グラフィックスバックエンド利用可否
	AvailableWindowBackends windowBackends; ///< ウィンドウバックエンド利用可否

	/// @brief OS名を文字列で取得する
	/// @return OS名
	[[nodiscard]] std::string osName() const
	{
		switch (os)
		{
		case OsType::Windows:    return "Windows";
		case OsType::Linux:      return "Linux";
		case OsType::MacOS:      return "macOS";
		case OsType::Emscripten: return "Emscripten";
		default:                 return "Unknown";
		}
	}

	/// @brief CPUアーキテクチャ名を文字列で取得する
	/// @return アーキテクチャ名
	[[nodiscard]] std::string archName() const
	{
		switch (arch)
		{
		case CpuArch::X86:   return "x86";
		case CpuArch::X64:   return "x64";
		case CpuArch::Arm32: return "ARM32";
		case CpuArch::Arm64: return "ARM64";
		case CpuArch::Wasm:  return "WASM";
		default:              return "Unknown";
		}
	}

	/// @brief コンパイル時のプラットフォーム情報を検出する
	/// @return 現在のプラットフォームの情報
	[[nodiscard]] static PlatformInfo detect() noexcept
	{
		PlatformInfo info;

		/// --- OS検出 ---
#if defined(_WIN32)
		info.os = OsType::Windows;
#elif defined(__EMSCRIPTEN__)
		info.os = OsType::Emscripten;
#elif defined(__APPLE__)
		info.os = OsType::MacOS;
#elif defined(__linux__)
		info.os = OsType::Linux;
#endif

		/// --- アーキテクチャ検出 ---
#if defined(__EMSCRIPTEN__)
		info.arch = CpuArch::Wasm;
#elif defined(_M_ARM64) || defined(__aarch64__)
		info.arch = CpuArch::Arm64;
#elif defined(_M_ARM) || defined(__arm__)
		info.arch = CpuArch::Arm32;
#elif defined(_M_X64) || defined(__x86_64__)
		info.arch = CpuArch::X64;
#elif defined(_M_IX86) || defined(__i386__)
		info.arch = CpuArch::X86;
#endif

		/// --- グラフィックスバックエンド ---
#ifdef _WIN32
		info.backends.dx11 = true;
		info.backends.dx12 = true;
#endif

#ifdef MITIRU_HAS_VULKAN
		info.backends.vulkan = true;
#endif

#ifdef __EMSCRIPTEN__
		info.backends.webgl = true;
#endif

		info.backends.null = true;

		/// --- ウィンドウバックエンド ---
#ifdef _WIN32
		info.windowBackends.win32 = true;
#endif

#ifdef MITIRU_HAS_SDL2
		info.windowBackends.sdl2 = true;
#endif

#ifdef MITIRU_HAS_GLFW
		info.windowBackends.glfw = true;
#endif

		return info;
	}
};

} // namespace mitiru
