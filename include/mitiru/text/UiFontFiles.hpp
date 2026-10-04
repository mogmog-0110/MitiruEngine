#pragma once

/// @file UiFontFiles.hpp
/// @brief RML の UI が読む書体のファイルと、その順番。RmlRuntime と訳の点検 (TranslationCheck.hpp) が同じ並びを使う
/// @details 同梱の書体は、配布物では exe の隣、開発中はビルド先から数段上のリポジトリにある。
///          読む順で代替書体の並びが決まるので、フォルダの中はファイル名の順に並べ、列挙の順に頼らない。

#include <algorithm>
#include <array>
#include <filesystem>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace mitiru::text
{

/// 仮名と漢字の代替書体を兼ねる同梱の書体。これがあるフォルダを同梱の書体のフォルダとみなす
inline constexpr const char* kUiFallbackFont = "MPLUSRounded1c-Regular.ttf";

[[nodiscard]] inline std::filesystem::path executableDirPath()
{
#ifdef _WIN32
	wchar_t buf[MAX_PATH] = {};
	const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
	return std::filesystem::current_path();
#endif
}

/// @brief exe のフォルダから 5 段上まで relative/probe を探す。見つからなければ exe の隣の relative
[[nodiscard]] inline std::filesystem::path findEngineDir(const std::filesystem::path& relative, const char* probe)
{
	std::error_code ec;
	std::filesystem::path dir = executableDirPath();
	for (int up = 0; up < 5 && !dir.empty(); ++up, dir = dir.parent_path())
	{
		if (std::filesystem::exists(dir / relative / probe, ec)) { return dir / relative; }
	}
	return executableDirPath() / relative;
}

[[nodiscard]] inline std::filesystem::path engineFontDir()
{
	return findEngineDir("assets/fonts", kUiFallbackFont);
}

/// @brief dir の .ttf と .otf をファイル名の順に
[[nodiscard]] inline std::vector<std::filesystem::path> fontFilesIn(const std::filesystem::path& dir)
{
	std::vector<std::filesystem::path> out;
	std::error_code ec;
	for (const auto& e : std::filesystem::directory_iterator(dir, ec))
	{
		const auto ext = e.path().extension().string();
		if (e.is_regular_file(ec) && (ext == ".ttf" || ext == ".otf")) { out.push_back(e.path()); }
	}
	std::sort(out.begin(), out.end());
	return out;
}

/// @brief 文書 documentDir の UI が読む書体のフォルダを読む順に。同梱の書体、文書の隣の fonts/、一つ上の fonts/
///        (assets/ui/ の文書なら assets/ui/fonts/ と assets/fonts/)
[[nodiscard]] inline std::array<std::filesystem::path, 3> uiFontDirs(const std::filesystem::path& engineFonts,
                                                                    const std::filesystem::path& documentDir)
{
	return {engineFonts, documentDir / "fonts", documentDir.parent_path() / "fonts"};
}

} // namespace mitiru::text
