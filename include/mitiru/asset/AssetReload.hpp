#pragma once

/// @file AssetReload.hpp
/// @brief `mitiru_host --watch-assets` が変更されたファイルをどう扱うかの判定
/// @details データ (.json / .baked) は game へ `asset.reloaded` を届けるだけ。モデル (.gltf / .glb / .obj / .fbx) は
///          それに加えて描画側の読み込み済みモデルを捨て、次の描画で読み直させる。Blender から書き出した
///          レベルの glb は両方に当たる (描画は drawModel、配置は game が level::loadLevelFile で読み直す)。
///          取り込みが自分で書く派生ファイル (`<x>.fbx.glb` 等) と書き出し途中の一時ファイルは無視する。

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mitiru::asset
{

enum class AssetChange
{
	Data,      ///< game に知らせるだけ
	Model,     ///< 描画側のモデルも読み直す
	Derived,   ///< 取り込みの cache や一時ファイル。無視する
};

/// @brief --watch-assets が見る拡張子 (FileWatcher::watchDirectory に渡す)
[[nodiscard]] inline std::vector<std::string> watchedAssetExtensions()
{
	return {".json", ".baked", ".gltf", ".glb", ".obj", ".fbx"};
}

namespace detail
{

[[nodiscard]] inline std::string lowerAscii(std::string s)
{
	for (auto& c : s)
	{
		if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
	}
	return s;
}

[[nodiscard]] inline bool endsWith(std::string_view s, std::string_view suffix)
{
	return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

}  // namespace detail

[[nodiscard]] inline AssetChange classifyAssetChange(const std::filesystem::path& changed)
{
	const auto u8 = changed.filename().u8string();
	const std::string name = detail::lowerAscii(std::string(u8.begin(), u8.end()));
	// '.' で始まる名前と .tmp は書き出し途中の一時ファイル、.fbx.glb は FBX の取り込みの cache
	if (name.empty() || name.front() == '.' || detail::endsWith(name, ".tmp") || detail::endsWith(name, ".fbx.glb"))
	{
		return AssetChange::Derived;
	}
	for (const std::string_view ext : {".gltf", ".glb", ".obj", ".fbx"})
	{
		if (detail::endsWith(name, ext)) { return AssetChange::Model; }
	}
	return AssetChange::Data;
}

/// @brief 描画側が覚えているパス (game が渡したまま) と、変更されたファイルが同じものか
/// @details 相対パスは vfs::readGlobal と同じく cwd → MITIRU_ASSET_ROOT の順に解く。綴りや大文字小文字の違いは
///          ファイルの同一性 (std::filesystem::equivalent) で吸収する。
[[nodiscard]] inline bool sameAssetFile(std::string_view key, const std::filesystem::path& changed)
{
	namespace fs = std::filesystem;
	const fs::path given(std::u8string(key.begin(), key.end()));
	std::vector<fs::path> candidates{given};
	if (given.is_relative())
	{
		if (const char* root = std::getenv("MITIRU_ASSET_ROOT"); root != nullptr && root[0] != '\0')
		{
			candidates.push_back(fs::path(root) / given);
		}
	}
	for (const auto& c : candidates)
	{
		std::error_code ec;
		if (fs::equivalent(c, changed, ec) && !ec) { return true; }
	}
	return false;
}

}  // namespace mitiru::asset
