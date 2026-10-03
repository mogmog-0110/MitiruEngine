#pragma once

/// @file AnimSidecar.hpp
/// @brief モデルの隣に置く `<名前>.anim.json` を AnimAssetOptions へ読む。
/// @details glTF には時刻付きの印 (Blender のマーカー) が入らないので、イベント・マスク・ソケット・
///          ルート骨と、別の骨格から写すクリップ (retarget) はこの JSON に書く。書式は docs/ANIMATION_RUNTIME.md。

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <mitiru/animation/AnimAssetBuild.hpp>

namespace mitiru::animation
{

/// @brief "fox.glb" → "fox.anim.json" (拡張子を置き換える。拡張子が無ければ足す)。
[[nodiscard]] inline std::string animSidecarPath(std::string_view modelPath)
{
	const auto slash = modelPath.find_last_of("/\\");
	const auto dot = modelPath.find_last_of('.');
	const bool hasExt = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
	return std::string(hasExt ? modelPath.substr(0, dot) : modelPath) + ".anim.json";
}

namespace detail
{

[[nodiscard]] inline std::vector<std::string> jsonStrings(const nlohmann::json& j, const char* key)
{
	std::vector<std::string> out;
	if (!j.contains(key) || !j[key].is_array()) { return out; }
	for (const auto& v : j[key])
	{
		if (v.is_string()) { out.push_back(v.get<std::string>()); }
	}
	return out;
}

template <class V, std::size_t N>
[[nodiscard]] inline V jsonVec(const nlohmann::json& j, const char* key, V fallback)
{
	if (!j.contains(key) || !j[key].is_array() || j[key].size() != N) { return fallback; }
	float v[N] = {};
	for (std::size_t i = 0; i < N; ++i) { v[i] = j[key][i].is_number() ? j[key][i].get<float>() : 0.0f; }
	if constexpr (N == 3) { return V{v[0], v[1], v[2]}; }
	else { return V{v[0], v[1], v[2], v[3]}; }
}

inline void readSidecarEvents(const nlohmann::json& root, AnimAssetOptions& out)
{
	if (!root.contains("events") || !root["events"].is_object()) { return; }
	for (const auto& [clip, list] : root["events"].items())
	{
		if (!list.is_array()) { continue; }
		for (const auto& e : list)
		{
			if (!e.is_object() || !e.contains("name") || !e["name"].is_string()) { continue; }
			out.events.push_back({clip, e["name"].get<std::string>(), e.value("time", 0.0f)});
		}
	}
}

inline void readSidecarMasks(const nlohmann::json& root, AnimAssetOptions& out)
{
	if (!root.contains("masks") || !root["masks"].is_object()) { return; }
	for (const auto& [name, m] : root["masks"].items())
	{
		if (!m.is_object()) { continue; }
		out.masks.push_back({name, jsonStrings(m, "include"), jsonStrings(m, "exclude"), m.value("weight", 1.0f)});
	}
}

inline void readSidecarSockets(const nlohmann::json& root, AnimAssetOptions& out)
{
	if (!root.contains("sockets") || !root["sockets"].is_object()) { return; }
	for (const auto& [name, s] : root["sockets"].items())
	{
		if (!s.is_object() || !s.contains("bone") || !s["bone"].is_string()) { continue; }
		out.sockets.push_back({name, s["bone"].get<std::string>(),
		                       jsonVec<sgc::Vec3f, 3>(s, "offset", {0, 0, 0}),
		                       quatNormalize(jsonVec<sgc::Vec4f, 4>(s, "rotation", {0, 0, 0, 1}))});
	}
}

inline void readSidecarRetargets(const nlohmann::json& root, AnimAssetOptions& out)
{
	if (!root.contains("retarget") || !root["retarget"].is_array()) { return; }
	for (const auto& r : root["retarget"])
	{
		if (!r.is_object() || !r.contains("source") || !r["source"].is_string() || !r.contains("map")) { continue; }
		AnimRetargetDef def;
		def.source = r["source"].get<std::string>();
		if (r["map"].is_string()) { def.mapPath = r["map"].get<std::string>(); }
		else { def.mapJson = r["map"].dump(); }
		def.clips = jsonStrings(r, "clips");
		if (r.contains("prefix") && r["prefix"].is_string()) { def.prefix = r["prefix"].get<std::string>(); }
		out.retargets.push_back(std::move(def));
	}
}

} // namespace detail

/// @brief sidecar JSON の文字列を読む。JSON のオブジェクトとして読めなければ nullopt (error に理由)。
[[nodiscard]] inline std::optional<AnimAssetOptions> parseAnimSidecar(std::string_view text,
                                                                      std::string* error = nullptr)
{
	const auto root = nlohmann::json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
	if (root.is_discarded() || !root.is_object())
	{
		if (error != nullptr) { *error = "anim.json を JSON のオブジェクトとして読めない"; }
		return std::nullopt;
	}
	AnimAssetOptions out;
	if (root.contains("rootMotion") && root["rootMotion"].is_string())
	{
		out.rootMotionBone = root["rootMotion"].get<std::string>();
	}
	detail::readSidecarEvents(root, out);
	detail::readSidecarMasks(root, out);
	detail::readSidecarSockets(root, out);
	detail::readSidecarRetargets(root, out);
	return out;
}

} // namespace mitiru::animation
