#include <catch2/catch_test_macros.hpp>

#include <mitiru/platform/PlatformInfo.hpp>

// ============================================================
// OsType enum
// ============================================================

TEST_CASE("OsType - enum values exist", "[platform]")
{
	using mitiru::OsType;

	REQUIRE(OsType::Unknown != OsType::Windows);
	REQUIRE(OsType::Linux != OsType::Windows);
	REQUIRE(OsType::MacOS != OsType::Windows);
	REQUIRE(OsType::Emscripten != OsType::Windows);
}

// ============================================================
// CpuArch enum
// ============================================================

TEST_CASE("CpuArch - enum values exist", "[platform]")
{
	using mitiru::CpuArch;

	REQUIRE(CpuArch::Unknown != CpuArch::X64);
	REQUIRE(CpuArch::X86 != CpuArch::X64);
	REQUIRE(CpuArch::Arm32 != CpuArch::X64);
	REQUIRE(CpuArch::Arm64 != CpuArch::X64);
	REQUIRE(CpuArch::Wasm != CpuArch::X64);
}

// ============================================================
// AvailableBackends
// ============================================================

TEST_CASE("AvailableBackends - default has null only", "[platform]")
{
	const mitiru::AvailableBackends backends;

	REQUIRE(backends.null == true);
	REQUIRE(backends.count() >= 1);
}

TEST_CASE("AvailableBackends - list returns names", "[platform]")
{
	mitiru::AvailableBackends backends;
	backends.dx11 = true;
	backends.vulkan = true;
	backends.null = true;

	const auto names = backends.list();

	REQUIRE(names.size() == 3);
	REQUIRE(names[0] == "DX11");
	REQUIRE(names[1] == "Vulkan");
	REQUIRE(names[2] == "Null");
}

TEST_CASE("AvailableBackends - count matches enabled backends", "[platform]")
{
	mitiru::AvailableBackends backends;
	backends.dx11 = true;
	backends.dx12 = true;
	backends.vulkan = false;
	backends.opengl = false;
	backends.webgl = false;
	backends.null = true;

	REQUIRE(backends.count() == 3);
}

// ============================================================
// AvailableWindowBackends
// ============================================================

TEST_CASE("AvailableWindowBackends - default is all false", "[platform]")
{
	const mitiru::AvailableWindowBackends backends;

	REQUIRE(backends.win32 == false);
	REQUIRE(backends.sdl2 == false);
	REQUIRE(backends.glfw == false);
}

TEST_CASE("AvailableWindowBackends - list returns names", "[platform]")
{
	mitiru::AvailableWindowBackends backends;
	backends.win32 = true;
	backends.sdl2 = true;

	const auto names = backends.list();

	REQUIRE(names.size() == 2);
	REQUIRE(names[0] == "Win32");
	REQUIRE(names[1] == "SDL2");
}

// ============================================================
// PlatformInfo
// ============================================================

TEST_CASE("PlatformInfo - detect returns valid info", "[platform]")
{
	const auto info = mitiru::PlatformInfo::detect();

	/// 何らかのOSが検出される
	REQUIRE(info.os != mitiru::OsType::Unknown);

	/// 何らかのアーキテクチャが検出される
	REQUIRE(info.arch != mitiru::CpuArch::Unknown);

	/// NullDeviceは常に利用可能
	REQUIRE(info.backends.null == true);
}

TEST_CASE("PlatformInfo - detect on Windows", "[platform]")
{
#ifdef _WIN32
	const auto info = mitiru::PlatformInfo::detect();

	REQUIRE(info.os == mitiru::OsType::Windows);
	REQUIRE(info.backends.dx11 == true);
	REQUIRE(info.backends.dx12 == true);
	REQUIRE(info.windowBackends.win32 == true);
#endif
}

TEST_CASE("PlatformInfo - detect x64 architecture", "[platform]")
{
#if defined(_M_X64) || defined(__x86_64__)
	const auto info = mitiru::PlatformInfo::detect();

	REQUIRE(info.arch == mitiru::CpuArch::X64);
	REQUIRE(info.archName() == "x64");
#endif
}

TEST_CASE("PlatformInfo - osName returns valid string", "[platform]")
{
	const auto info = mitiru::PlatformInfo::detect();

	const auto name = info.osName();
	REQUIRE_FALSE(name.empty());
	REQUIRE(name != "Unknown");
}

TEST_CASE("PlatformInfo - archName returns valid string", "[platform]")
{
	const auto info = mitiru::PlatformInfo::detect();

	const auto name = info.archName();
	REQUIRE_FALSE(name.empty());
	REQUIRE(name != "Unknown");
}

TEST_CASE("PlatformInfo - backends list is not empty", "[platform]")
{
	const auto info = mitiru::PlatformInfo::detect();

	const auto backendNames = info.backends.list();
	REQUIRE_FALSE(backendNames.empty());
}

TEST_CASE("PlatformInfo - vulkan backend reflects compile flag", "[platform]")
{
	const auto info = mitiru::PlatformInfo::detect();

#ifdef MITIRU_HAS_VULKAN
	REQUIRE(info.backends.vulkan == true);
#else
	REQUIRE(info.backends.vulkan == false);
#endif
}

TEST_CASE("PlatformInfo - SDL2 backend reflects compile flag", "[platform]")
{
	const auto info = mitiru::PlatformInfo::detect();

#ifdef MITIRU_HAS_SDL2
	REQUIRE(info.windowBackends.sdl2 == true);
#else
	REQUIRE(info.windowBackends.sdl2 == false);
#endif
}

TEST_CASE("PlatformInfo - GLFW backend reflects compile flag", "[platform]")
{
	const auto info = mitiru::PlatformInfo::detect();

#ifdef MITIRU_HAS_GLFW
	REQUIRE(info.windowBackends.glfw == true);
#else
	REQUIRE(info.windowBackends.glfw == false);
#endif
}
