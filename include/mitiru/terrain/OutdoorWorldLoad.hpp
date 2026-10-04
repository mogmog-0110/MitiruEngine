#pragma once

/// @file OutdoorWorldLoad.hpp
/// @brief `*.world.json` を読み、host の描画とゲーム DLL で使う OutdoorWorld を作る
/// @details 書式は docs/TERRAIN_AND_WATER.md。パスは world.json の場所からの相対。
/// "level" に Blender のレベル (glb)を書くと、`mitiru_type` が terrain、water、
/// scatter の Entity も追加する (OutdoorLevel.hpp)。
/// ヒープとファイルを使うため、DLL の読み込み時に 1 度だけ呼ぶ。

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/terrain/HeightfieldLoad.hpp>
#include <mitiru/terrain/OutdoorLevel.hpp>
#include <mitiru/terrain/OutdoorWorld.hpp>
#include <mitiru/terrain/detail/OutdoorWorldParse.hpp>
#include <mitiru/terrain/detail/SplatRules.hpp>

namespace mitiru::terrain
{

struct OutdoorWorldLoadResult
{
	std::unique_ptr<OutdoorWorld> world;  ///< 読めなければ null (理由は error)
	std::string                   error;
};

/// @brief world.json の文字列から作る。baseDir は相対パスの起点で、末尾の `/` はあってもなくてもよい。
[[nodiscard]] inline OutdoorWorldLoadResult loadOutdoorWorldFromText(std::string_view text, std::string_view baseDir,
                                                                    std::string_view sourcePath = {})
{
	OutdoorWorldLoadResult r;
	std::string syntaxError;
	const auto json = data::parseJsonText(text, sourcePath.empty() ? std::string_view("world.json") : sourcePath, syntaxError);
	if (!json.is_object())
	{
		r.error = syntaxError.empty() ? "JSON の書き方が間違っているか、一番外側が { } のオブジェクトではありません" : syntaxError;
		return r;
	}
	auto world = std::make_unique<OutdoorWorld>();
	world->path = std::string(sourcePath);
	const detail::WorldPaths paths{std::string(baseDir)};
	detail::TerrainSource source;
	if (!detail::parseTerrainSource(json, paths, source, r.error)) { return r; }
	const auto level = detail::loadLevelFor(json, paths, *world);
	if (level) { applyLevelTerrain(*level, source.placement, world->warnings); }
	if (!detail::loadTerrain(source, *world, r.error)) { return r; }
	detail::parseLayers(json, paths, *world);
	detail::parseGrass(json, paths, *world);
	detail::parseScatter(json, paths, *world);
	detail::parseWater(json, *world);
	if (level) { applyLevelMarks(*level, paths.dirWithSlash(), *world); }
	detail::finishWorld(*world);
	r.world = std::move(world);
	return r;
}

/// @brief vfs::readAsset で読み、pack 内でも同じパスを使う。
[[nodiscard]] inline OutdoorWorldLoadResult loadOutdoorWorld(std::string_view path)
{
	const auto text = vfs::readAssetText(path);
	if (!text)
	{
		OutdoorWorldLoadResult r;
		r.error = "world.json を開けない: " + std::string(path);
		return r;
	}
	const std::size_t slash = path.find_last_of("/\\");
	const std::string_view dir = (slash == std::string_view::npos) ? std::string_view{} : path.substr(0, slash + 1);
	auto r = loadOutdoorWorldFromText(*text, dir, path);
	if (!r.error.empty()) { r.error += " (" + std::string(path) + ")"; }
	return r;
}

/// @brief world.json のパスかを判定する。drawModel の振り分けに使う。
[[nodiscard]] inline bool isOutdoorWorldPath(std::string_view path) noexcept
{
	return detail::endsWith(path, ".world.json");
}

} // namespace mitiru::terrain
