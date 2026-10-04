#pragma once

/// @file OutdoorRegion.hpp
/// @brief 広い屋外を区画 (1 区画 = 1 つの world.json) に分けた `*.region.json`。目からの距離で読む区画を決める
/// @details host の描画とゲーム DLL の当たり判定は、同じ表と同じ updateRegionResidency で同じ区画を選べる。
///          区画の矩形は表に書くので、区画の world.json を読まずに距離が出る。読み始めは loadRadius、手放すのは
///          unloadRadius で、その間は今の状態を保つ (境目を行き来しても読み直しを繰り返さない)。
///          書式は docs/STREAMING.md。区画は tools/split_world.py で 1 枚の world.json から作れる。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/terrain/detail/OutdoorWorldParse.hpp>

namespace mitiru::terrain
{

struct RegionCell
{
	std::string world;   ///< 区画の world.json (解決したパス)
	float minX = 0.0f;
	float minZ = 0.0f;
	float maxX = 0.0f;
	float maxZ = 0.0f;
};

struct OutdoorRegion
{
	std::string             path;
	std::vector<RegionCell> cells;
	float                   loadRadius = 256.0f;    ///< 区画の矩形までの距離がこれより近ければ読む (m)
	float                   unloadRadius = 320.0f;  ///< これより遠ければ手放す (m)。loadRadius 以上
};

struct OutdoorRegionLoadResult
{
	std::optional<OutdoorRegion> region;
	std::string                  error;
};

[[nodiscard]] inline bool isOutdoorRegionPath(std::string_view path) noexcept
{
	return detail::endsWith(path, ".region.json");
}

/// @brief (x, z) から区画の矩形までの水平の距離。中にいれば 0
[[nodiscard]] inline float distanceToCell(const RegionCell& c, float x, float z) noexcept
{
	const float dx = std::max({c.minX - x, 0.0f, x - c.maxX});
	const float dz = std::max({c.minZ - z, 0.0f, z - c.maxZ});
	return std::sqrt(dx * dx + dz * dz);
}

/// @brief 区画ごとに読むか (1) 手放すか (0) を決め直す。resident は前の結果で、区画と同じ数に揃える
/// @return 変わった区画の数
inline int updateRegionResidency(const OutdoorRegion& r, float x, float z, std::vector<std::uint8_t>& resident)
{
	resident.resize(r.cells.size(), 0);
	int changed = 0;
	for (std::size_t i = 0; i < r.cells.size(); ++i)
	{
		const float d = distanceToCell(r.cells[i], x, z);
		const std::uint8_t next = resident[i] != 0 ? (d <= r.unloadRadius ? 1 : 0) : (d <= r.loadRadius ? 1 : 0);
		changed += next != resident[i] ? 1 : 0;
		resident[i] = next;
	}
	return changed;
}

namespace detail
{

[[nodiscard]] inline bool parseRegionCell(const Json& j, const WorldPaths& paths, RegionCell& out, std::string& error)
{
	const auto world = j.find("world");
	const auto mn = j.find("min");
	const auto mx = j.find("max");
	const auto isPair = [](const Json::const_iterator& it, const Json& o) {
		return it != o.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number();
	};
	if (world == j.end() || !world->is_string() || !isPair(mn, j) || !isPair(mx, j))
	{
		error = "区画には world (文字列) と min / max ([x, z]) が要る";
		return false;
	}
	out.world = paths.resolve(world->get<std::string>());
	out.minX = (*mn)[0].get<float>();
	out.minZ = (*mn)[1].get<float>();
	out.maxX = (*mx)[0].get<float>();
	out.maxZ = (*mx)[1].get<float>();
	if (!(out.maxX > out.minX) || !(out.maxZ > out.minZ))
	{
		error = "区画の max は min より大きくする: " + out.world;
		return false;
	}
	return true;
}

} // namespace detail

/// @brief region.json の文字列から作る。baseDir は区画の world.json の相対パスの起点
[[nodiscard]] inline OutdoorRegionLoadResult loadOutdoorRegionFromText(std::string_view text, std::string_view baseDir,
                                                                      std::string_view sourcePath = {})
{
	OutdoorRegionLoadResult r;
	std::string syntaxError;
	const auto json = data::parseJsonText(text, sourcePath.empty() ? std::string_view("region.json") : sourcePath, syntaxError);
	const auto cells = json.is_object() ? json.find("cells") : json.end();
	if (!json.is_object() || cells == json.end() || !cells->is_array())
	{
		r.error = syntaxError.empty() ? "region.json には cells の配列が要る" : syntaxError;
		return r;
	}
	OutdoorRegion region;
	region.path = std::string(sourcePath);
	region.loadRadius = detail::num(json, "loadRadius", region.loadRadius);
	region.unloadRadius = std::max(region.loadRadius, detail::num(json, "unloadRadius", region.loadRadius * 1.25f));
	const detail::WorldPaths paths{std::string(baseDir)};
	for (const auto& c : *cells)
	{
		RegionCell cell;
		if (!detail::parseRegionCell(c, paths, cell, r.error)) { return r; }
		region.cells.push_back(std::move(cell));
	}
	r.region = std::move(region);
	return r;
}

[[nodiscard]] inline OutdoorRegionLoadResult loadOutdoorRegion(std::string_view path)
{
	const auto text = vfs::readAssetText(path);
	if (!text)
	{
		OutdoorRegionLoadResult r;
		r.error = "region.json を開けない: " + std::string(path);
		return r;
	}
	const std::size_t slash = path.find_last_of("/\\");
	const std::string_view dir = (slash == std::string_view::npos) ? std::string_view{} : path.substr(0, slash + 1);
	auto r = loadOutdoorRegionFromText(*text, dir, path);
	if (!r.error.empty()) { r.error += " (" + std::string(path) + ")"; }
	return r;
}

} // namespace mitiru::terrain
