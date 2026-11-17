#pragma once

/// @file FilmPreset.hpp
/// @brief `look_preset: "dusk"` を読み、film の JSON に look・sky・environment・hdri・lights の既定値として混ぜる。
/// @details film に書かれた値を優先する。look と hdri は項目ごとに補い (hdri は film が null なら使わない)、sky と environment は film に無いときだけ補う。
/// lights は film に同名のものが無ければ先頭に足す。sun は最後の定義が使われるため、film の sun が優先される。
/// 名前が `.json` で終わる場合は、film からの相対パスまたは絶対パスとして探す。それ以外は film のフォルダの
/// `looks/`、1 つ上と 2 つ上の `looks/`、host の assets の `film_player/looks/` の順に探す。

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/asset/AssetPack.hpp>

namespace mitiru::film
{

namespace preset_detail
{

using Json = nlohmann::json;

inline bool readFile(const std::filesystem::path& p, std::string& out)
{
	std::error_code ec;
	if (!std::filesystem::is_regular_file(p, ec)) { return false; }
	std::ifstream in(p, std::ios::binary);
	if (!in) { return false; }
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

inline bool nameIsPlain(const std::string& name)
{
	if (name.empty()) { return false; }
	for (char c : name)
	{
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!ok) { return false; }
	}
	return true;
}

/// @brief 候補を順に試し、最初に読めた内容を text に入れる。試した場所はエラーメッセージ用に tried に入れる。
inline bool findPreset(const std::string& name, std::string_view baseDir, std::string& text, std::string& tried)
{
	std::vector<std::filesystem::path> candidates;
	const std::filesystem::path base{std::string(baseDir)};
	if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0)
	{
		candidates.push_back(std::filesystem::path(name).is_absolute() ? std::filesystem::path(name) : base / name);
	}
	else
	{
		const std::string file = name + ".json";
		candidates = {base / "looks" / file, base / ".." / "looks" / file, base / ".." / ".." / "looks" / file};
	}
	for (const auto& c : candidates)
	{
		const std::filesystem::path n = c.lexically_normal();
		tried += (tried.empty() ? "" : ", ") + n.generic_string();
		if (readFile(n, text)) { return true; }
	}
	if (nameIsPlain(name))
	{
		const std::string asset = "film_player/looks/" + name + ".json";
		tried += ", " + asset;
		if (const auto bytes = vfs::readAsset(asset))
		{
			text.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
			return true;
		}
	}
	return false;
}

inline bool hasLightNamed(const Json& lights, const std::string& name)
{
	for (const Json& l : lights)
	{
		if (l.is_object() && l.contains("name") && l["name"] == name) { return true; }
	}
	return false;
}

}  // namespace preset_detail

/// @brief root に look_preset があればプリセットを適用する。エラーは error に入れ、false を返す。
[[nodiscard]] inline bool applyLookPreset(nlohmann::json& root, std::string_view baseDir, std::string& error)
{
	using namespace preset_detail;
	if (!root.contains("look_preset")) { return true; }
	if (!root["look_preset"].is_string()) { error = "look_preset: 文字列 (\"dusk\" か、.json のパス) でない"; return false; }
	const std::string name = root["look_preset"].get<std::string>();
	std::string text, tried;
	if (!findPreset(name, baseDir, text, tried)) { error = "look_preset: " + name + " を読めない。探した場所: " + tried; return false; }
	const Json preset = Json::parse(text, nullptr, false);
	if (preset.is_discarded() || !preset.is_object()) { error = "look_preset " + name + ": JSON として読めない"; return false; }
	for (auto it = preset.begin(); it != preset.end(); ++it)
	{
		const std::string& k = it.key();
		if (k != "look" && k != "sky" && k != "environment" && k != "hdri" && k != "lights" && k != "description")
		{
			error = "look_preset " + name + "." + k + ": 知らない項目。使えるのは look, sky, environment, hdri, lights, description";
			return false;
		}
	}
	if (preset.contains("look") && preset["look"].is_object())
	{
		Json merged = preset["look"];
		if (root.contains("look") && root["look"].is_object())
		{
			for (auto it = root["look"].begin(); it != root["look"].end(); ++it) { merged[it.key()] = it.value(); }
		}
		root["look"] = merged;
	}
	for (const char* key : {"sky", "environment"})
	{
		if (preset.contains(key) && !root.contains(key)) { root[key] = preset[key]; }
	}
	// hdri は look と同じく、フィールドごとに混ぜる (film の値が優先)。film が null なら雛形の hdri を使わない
	if (preset.contains("hdri") && preset["hdri"].is_object() && !(root.contains("hdri") && root["hdri"].is_null()))
	{
		Json merged = preset["hdri"];
		if (root.contains("hdri") && root["hdri"].is_object())
		{
			for (auto it = root["hdri"].begin(); it != root["hdri"].end(); ++it) { merged[it.key()] = it.value(); }
		}
		root["hdri"] = merged;
	}
	if (preset.contains("lights") && preset["lights"].is_array())
	{
		Json merged = Json::array();
		const Json own = root.contains("lights") && root["lights"].is_array() ? root["lights"] : Json::array();
		for (const Json& l : preset["lights"])
		{
			const bool named = l.is_object() && l.contains("name") && l["name"].is_string();
			if (!named || !hasLightNamed(own, l["name"].get<std::string>())) { merged.push_back(l); }
		}
		for (const Json& l : own) { merged.push_back(l); }
		root["lights"] = merged;
	}
	return true;
}

}  // namespace mitiru::film
