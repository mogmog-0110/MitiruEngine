#include "TileSchema.hpp"

#include <cstdlib>
#include <set>

namespace mitiru::tool::tilemapedit
{

namespace
{

using Json = nlohmann::json;

constexpr long long kMaxTileSize = 4096;
constexpr long long kMaxId = 1000000000;

bool fail(std::string& error, const std::string& where, const std::string& what)
{
	error = where + ": " + what;
	return false;
}

std::string stringOf(const Json& j, const char* key, const std::string& fallback = {})
{
	const auto it = j.find(key);
	return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

bool parseColor(const std::string& hex, stageedit::Rgb& out)
{
	if (hex.size() != 7 || hex[0] != '#') { return false; }
	char* end = nullptr;
	const unsigned long v = std::strtoul(hex.c_str() + 1, &end, 16);
	if (end != hex.c_str() + 7) { return false; }
	out = stageedit::Rgb{ static_cast<unsigned char>((v >> 16) & 0xFF), static_cast<unsigned char>((v >> 8) & 0xFF), static_cast<unsigned char>(v & 0xFF) };
	return true;
}

bool parseTileset(const Json& j, Tileset& out, std::string& error)
{
	if (!j.is_object()) { return fail(error, "tileset", "Must be an object"); }
	out.image = stringOf(j, "image");
	const auto whole = [&](const char* key, long long fallback, long long lo, long long hi, int& dest) {
		const auto it = j.find(key);
		const long long v = it == j.end() ? fallback : (it->is_number_integer() ? it->get<long long>() : lo - 1);
		if (v < lo || v > hi) { return fail(error, "tileset", std::string(key) + " must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi)); }
		dest = static_cast<int>(v);
		return true;
	};
	if (!whole("tileWidth", 0, 1, kMaxTileSize, out.tileWidth) || !whole("tileHeight", 0, 1, kMaxTileSize, out.tileHeight) || !whole("firstId", 1, 1, kMaxId, out.firstId)) { return false; }
	if (out.image.empty()) { return fail(error, "tileset", "Missing image (logical path of the PNG)"); }
	return true;
}

bool parseChecked(const std::string& text, TileSchema& out, std::string& error)
{
	const Json root = Json::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object()) { return fail(error, "Schema", "Not valid JSON"); }
	out = TileSchema{};
	out.title = stringOf(root, "title", "Tilemap");
	out.dir = stringOf(root, "dir");
	if (out.dir.empty()) { return fail(error, "Schema", "Missing dir (logical path of the tilemap files)"); }
	while (!out.dir.empty() && out.dir.back() == '/') { out.dir.pop_back(); }
	if (const auto it = root.find("source"); it != root.end() && it->is_object())
	{
		out.source = stageedit::SourceMap{ stringOf(*it, "logical"), stringOf(*it, "disk") };
	}
	if (const auto it = root.find("exclude"); it != root.end() && it->is_array())
	{
		for (const auto& e : *it) { if (e.is_string()) { out.exclude.push_back(e.get<std::string>()); } }
	}
	if (const auto it = root.find("tileset"); it != root.end() && !parseTileset(*it, out.tileset, error)) { return false; }
	std::set<int> seen;
	if (const auto it = root.find("tiles"); it != root.end() && it->is_array())
	{
		for (std::size_t i = 0; i < it->size(); ++i)
		{
			const Json& t = (*it)[i];
			const std::string where = "tiles[" + std::to_string(i) + "]";
			if (!t.is_object() || !t.contains("id") || !t["id"].is_number_integer()) { return fail(error, where, "id (integer) is required"); }
			TileKind kind;
			const long long id = t["id"].get<long long>();
			if (id < 1 || id > kMaxId) { return fail(error, where, "id must be a whole number from 1 to 1000000000 (0 is the empty tile)"); }
			kind.id = static_cast<int>(id);
			if (!seen.insert(kind.id).second) { return fail(error, where, "Duplicate id " + std::to_string(kind.id)); }
			kind.label = stringOf(t, "label", std::to_string(kind.id));
			if (!parseColor(stringOf(t, "color", "#808080"), kind.color)) { return fail(error, where, "color must be #RRGGBB"); }
			out.tiles.push_back(std::move(kind));
		}
	}
	if (out.tileset.image.empty() && out.tiles.empty()) { return fail(error, "Schema", "Needs tiles or a tileset"); }
	return true;
}

} // namespace

bool parseTileSchema(const std::string& text, TileSchema& out, std::string& error)
{
	try { return parseChecked(text, out, error); }
	catch (const nlohmann::json::exception& e) { return fail(error, "Schema", std::string("A value has the wrong type (") + e.what() + ")"); }
}

} // namespace mitiru::tool::tilemapedit
