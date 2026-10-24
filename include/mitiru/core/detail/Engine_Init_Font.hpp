// mitiru::Engine の detail header。直接 include 禁止。core/Engine.hpp 経由で include される
#pragma once

#include <mitiru/core/Env.hpp>
#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/debug/ConsoleOut.hpp>
#include <mitiru/resource/AssetPath.hpp>

#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <string>

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
	if (const std::string windir = env::value("WINDIR"); !windir.empty())
	{
		const std::string fonts = windir + "\\Fonts\\";
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

/// 既定の書体を探して読んだ結果。font が空なら読めた書体が無く、firstError が最初の失敗の理由
struct LoadedFont
{
	std::unique_ptr<text::ScreenFont> font;
	std::string firstError;
};

/// CPU だけで済む (GPU にも Screen にも触れない) ので、別スレッドで呼んでよい
[[nodiscard]] inline LoadedFont loadDefaultScreenFont(const std::string& userPath)
{
	LoadedFont out;
	for (const std::string& path : defaultFontSearchPaths(userPath))
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) { continue; }
		std::string error;
		auto face = text::FontFace::loadFile(path, error);
		if (!face)
		{
			if (out.firstError.empty()) { out.firstError = path + ": " + error; }
			continue;
		}
		out.font = std::make_unique<text::ScreenFont>(std::move(face));
		return out;
	}
	return out;
}

/// Engine::run が D3D12 のデバイス作りを待つ間に、書体を別スレッドで読んでおく入れ物。initFont が同じ userPath なら受け取る
struct PrefetchedFont
{
	std::string userPath;
	std::future<LoadedFont> job;
};

[[nodiscard]] inline PrefetchedFont& prefetchedFont()
{
	static PrefetchedFont p;
	return p;
}

inline void prefetchDefaultFont(const std::string& userPath)
{
	PrefetchedFont& p = prefetchedFont();
	if (p.job.valid()) { return; }
	p.userPath = userPath;
	p.job = std::async(std::launch::async, [userPath] { return loadDefaultScreenFont(userPath); });
}

} // namespace mitiru::detail

MITIRU_INLINE void mitiru::Engine::initFont(const std::string& userPath)
{
	if (!m_screen) { return; }
	detail::PrefetchedFont& pre = detail::prefetchedFont();
	detail::LoadedFont loaded = (pre.job.valid() && pre.userPath == userPath) ? pre.job.get() : detail::loadDefaultScreenFont(userPath);
	if (loaded.font)
	{
		m_font = std::move(loaded.font);
		m_font->attachTo(*m_screen);
		return;
	}
	if (!loaded.firstError.empty())
	{
		console::noticef("書体を読めないので 8x8 ドットの書体で描きます (%s)", loaded.firstError.c_str());
	}
}
