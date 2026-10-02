#pragma once

/// @file LevelExtras.hpp
/// @brief glTF ノードの extras (Blender のカスタムプロパティ) に書くレベルの約束
/// @details `mitiru_type` を付けたノードは配置や当たり判定の印で、描かない。drawModel の取り込みと
///          level::LevelData の読み込みが同じ判定を使う。

#include <string>

#include <nlohmann/json.hpp>

namespace mitiru::level
{

inline constexpr const char* kTypeKey = "mitiru_type";
inline constexpr const char* kSizeKey = "mitiru_size";
inline constexpr const char* kCollideKey = "mitiru_collide";
inline constexpr const char* kNavKey = "mitiru_nav";

/// @brief extras の JSON 文字列 (null 可) をオブジェクトにする。オブジェクトでなければ空
[[nodiscard]] inline nlohmann::json parseExtras(const char* extrasJson)
{
	if (extrasJson == nullptr) { return nlohmann::json::object(); }
	auto j = nlohmann::json::parse(extrasJson, nullptr, false);
	return j.is_object() ? j : nlohmann::json::object();
}

[[nodiscard]] inline std::string extrasString(const nlohmann::json& extras, const char* key)
{
	const auto it = extras.find(key);
	return (it != extras.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

[[nodiscard]] inline bool extrasFlag(const nlohmann::json& extras, const char* key)
{
	const auto it = extras.find(key);
	if (it == extras.end()) { return false; }
	if (it->is_boolean()) { return it->get<bool>(); }
	return it->is_number() && it->get<double>() != 0.0;
}

/// @brief このノードのメッシュを描くか (`mitiru_type` が無ければ描く)
[[nodiscard]] inline bool isRenderedNode(const char* extrasJson)
{
	return extrasString(parseExtras(extrasJson), kTypeKey).empty();
}

}  // namespace mitiru::level
