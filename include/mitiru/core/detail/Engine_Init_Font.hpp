// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/resource/AssetPath.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace mitiru::detail
{

/// @brief 既定の書体を探す順番。同梱の日本語書体を最優先にする。
[[nodiscard]] inline std::vector<std::string> defaultFontSearchPaths(const std::string& userPath)
{
	std::vector<std::string> paths;
	if (!userPath.empty()) { paths.push_back(userPath); }
	// 同梱書体は CMake が host exe の隣へ配る。exe 相対を先に、次にリポジトリ相対を見る。
	const std::string exeDir = mitiru::resource::AssetPath::executableDir();
	for (const std::string& base : {exeDir + "/", std::string{}, std::string{"../"}, std::string{"../../"}})
	{
		paths.push_back(base + "assets/fonts/MPLUSRounded1c-Regular.ttf");
		paths.push_back(base + "assets/fonts/PixelMplus12-Regular.ttf");
		paths.push_back(base + "assets/fonts/default.ttf");
	}
#ifdef _WIN32
	if (const char* windir = std::getenv("WINDIR"))
	{
		const std::string fonts = std::string(windir) + "\\Fonts\\";
		for (const char* name : {"CascadiaMono.ttf", "CascadiaCode.ttf", "consola.ttf",
		                         "YuGothM.ttc", "YuGothR.ttc", "meiryo.ttc", "msgothic.ttc",
		                         "segoeui.ttf"})
		{
			paths.push_back(fonts + name);
		}
	}
#endif
	return paths;
}

} // namespace mitiru::detail

MITIRU_INLINE void mitiru::Engine::initFont(const std::string& userPath)
{
	if (!m_screen) { return; }
	std::string firstError;
	for (const std::string& path : detail::defaultFontSearchPaths(userPath))
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) { continue; }
		std::string error;
		auto face = text::FontFace::loadFile(path, error);
		if (!face)
		{
			if (firstError.empty()) { firstError = path + ": " + error; }
			continue;
		}
		m_font = std::make_unique<text::ScreenFont>(std::move(face));
		m_font->attachTo(*m_screen);
		return;
	}
	if (!firstError.empty())
	{
		std::fprintf(stderr, "[mitiru] font: %s (8x8 bitmap font is used)\n", firstError.c_str());
	}
}
