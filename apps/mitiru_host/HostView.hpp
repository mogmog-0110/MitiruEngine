#pragma once

/// @file HostView.hpp
/// @brief mitiru_host に glb / glTF のパスを渡すと、DLL なしで並べて表示する起動の引数処理。

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/animation/ModelView.hpp>

namespace mitiru::host
{

struct ViewArgs
{
	std::vector<std::string> models;   ///< 絶対パス (相対で渡されたら、起動した場所からの絶対にする)
	std::string clip;
	bool toon = false;
	bool checkOnly = false;
	bool clipGiven = false;
	[[nodiscard]] bool active() const noexcept { return !models.empty(); }
	[[nodiscard]] bool anyOption() const noexcept { return clipGiven || toon || checkOnly; }
};

/// 起動時の作業ディレクトリ。host が cwd を exe の隣に固定する前に保存する
[[nodiscard]] inline std::filesystem::path& launchDir()
{
	static std::filesystem::path dir;
	return dir;
}

inline void rememberLaunchDir()
{
	std::error_code ec;
	launchDir() = std::filesystem::current_path(ec);
}

[[nodiscard]] inline bool isModelFile(std::string_view arg) noexcept
{
	const auto dot = arg.find_last_of('.');
	if (dot == std::string_view::npos) { return false; }
	std::string ext(arg.substr(dot));
	for (auto& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
	return ext == ".glb" || ext == ".gltf";
}

/// 描画側は cwd と MITIRU_ASSET_ROOT でしか相対パスを解決しないため、絶対パスで渡す
[[nodiscard]] inline std::string absoluteUtf8(const std::filesystem::path& base, std::string_view arg)
{
	const std::filesystem::path given{std::u8string(arg.begin(), arg.end())};
	const std::filesystem::path joined = given.is_absolute() ? given : base / given;
	std::error_code ec;
	const auto norm = std::filesystem::weakly_canonical(joined, ec);
	const std::u8string u = (ec ? joined : norm).generic_u8string();
	return std::string(u.begin(), u.end());
}

struct ViewPlan
{
	std::vector<animation::ModelCheck> checks;
	int exitCode = 0;   ///< 0 = 問題なし、1 = 警告あり、2 = 読めない・クリップが無い
};

[[nodiscard]] inline ViewPlan inspectViewModels(const ViewArgs& view)
{
	ViewPlan plan;
	for (const auto& path : view.models)
	{
		auto check = animation::checkModelFile(path);
		(void)animation::requireClip(check, view.clip);
		const std::string text = animation::formatModelCheck(check);
		std::fwrite(text.data(), 1, text.size(), stdout);
		plan.exitCode = std::max(plan.exitCode, check.failed() ? 2 : (check.warnings.empty() ? 0 : 1));
		plan.checks.push_back(std::move(check));
	}
	if (const std::string note = animation::scaleMismatchNote(plan.checks); !note.empty())
	{
		std::fwrite(note.data(), 1, note.size(), stdout);
		plan.exitCode = std::max(plan.exitCode, 1);
	}
	std::fflush(stdout);
	return plan;
}

/// DLL に渡す JSON。_putenv の文字コードに左右されないよう、ASCII 以外をエスケープする
[[nodiscard]] inline std::string viewEnvironmentJson(const ViewPlan& plan, const ViewArgs& view)
{
	const auto config = animation::makeModelViewConfig(plan.checks, view.clip, view.toon);
	return animation::toJson(config);
}

} // namespace mitiru::host
