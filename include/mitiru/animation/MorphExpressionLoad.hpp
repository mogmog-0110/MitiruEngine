#pragma once

/// @file MorphExpressionLoad.hpp
/// @brief モデルの隣に置く `<名前>.expressions.json` を MorphExpressionSet へ読む。
/// @details 書式は VRM 1.0 の `VRMC_vrm.expressions` と同じ (preset と custom)。モーフのバインド (morphTargetBinds) と
///          isBinary と override だけを読み、materialColorBinds / textureTransformBinds は無視する。
///          ルートに preset / custom を直接書いても、"expressions" の下に書いても、glTF の拡張の
///          `extensions.VRMC_vrm.expressions` をそのまま貼っても読める。
///          custom の表情が口・まばたき・視線のどれかとして打ち消される側に回るときは "procedural": "mouth" | "blink" | "lookAt" と書く。

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/animation/MorphExpressionTypes.hpp>

namespace mitiru::animation
{

/// @brief "KAO.glb" → "KAO.expressions.json" (拡張子を置き換える。拡張子が無ければ足す)
[[nodiscard]] inline std::string morphExpressionSidecarPath(std::string_view modelPath)
{
	const auto slash = modelPath.find_last_of("/\\");
	const auto dot = modelPath.find_last_of('.');
	const bool hasExt = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
	return std::string(hasExt ? modelPath.substr(0, dot) : modelPath) + ".expressions.json";
}

namespace detail
{

/// 型の違う値で nlohmann が例外を投げないよう、数・真偽は型を確かめてから読む (読み込み中のモデルを止めない)
[[nodiscard]] inline float jsonNumber(const nlohmann::json& o, const char* key, float fallback)
{
	return (o.contains(key) && o[key].is_number()) ? o[key].get<float>() : fallback;
}

[[nodiscard]] inline bool jsonFlag(const nlohmann::json& o, const char* key, bool fallback)
{
	return (o.contains(key) && o[key].is_boolean()) ? o[key].get<bool>() : fallback;
}

[[nodiscard]] inline MorphCategory presetCategory(std::string_view name) noexcept
{
	for (const char* n : {"aa", "ih", "ou", "ee", "oh"}) { if (name == n) { return MorphCategory::Mouth; } }
	for (const char* n : {"blink", "blinkLeft", "blinkRight"}) { if (name == n) { return MorphCategory::Blink; } }
	for (const char* n : {"lookUp", "lookDown", "lookLeft", "lookRight"}) { if (name == n) { return MorphCategory::LookAt; } }
	return MorphCategory::None;
}

[[nodiscard]] inline MorphOverride parseOverride(const nlohmann::json& e, const char* key, std::vector<std::string>& warnings,
                                                 const std::string& name)
{
	if (!e.contains(key)) { return MorphOverride::None; }
	const auto& v = e[key];
	const std::string text = v.is_string() ? v.get<std::string>() : std::string{};
	if (text == "none" || text.empty()) { return MorphOverride::None; }
	if (text == "block") { return MorphOverride::Block; }
	if (text == "blend") { return MorphOverride::Blend; }
	warnings.push_back("expression " + name + ": " + key + " は none / block / blend のどれか: " + text);
	return MorphOverride::None;
}

inline void readExpression(const std::string& name, const nlohmann::json& e, bool preset, MorphExpressionSet& out,
                           std::vector<std::string>& warnings)
{
	if (!e.is_object()) { warnings.push_back("expression " + name + ": オブジェクトではない"); return; }
	if (out.find(name) >= 0) { warnings.push_back("expression " + name + ": 名前が重複している"); return; }
	MorphExpression x;
	x.name = name;
	x.key = animNameKey(name);
	x.isBinary = jsonFlag(e, "isBinary", false);
	x.category = preset ? presetCategory(name) : MorphCategory::None;
	if (!preset && e.contains("procedural") && e["procedural"].is_string())
	{
		const std::string p = e["procedural"].get<std::string>();
		x.category = p == "mouth" ? MorphCategory::Mouth : p == "blink" ? MorphCategory::Blink
		           : p == "lookAt" ? MorphCategory::LookAt : MorphCategory::None;
	}
	x.overrideMouth = parseOverride(e, "overrideMouth", warnings, name);
	x.overrideBlink = parseOverride(e, "overrideBlink", warnings, name);
	x.overrideLookAt = parseOverride(e, "overrideLookAt", warnings, name);
	if (e.contains("morphTargetBinds") && e["morphTargetBinds"].is_array())
	{
		for (const auto& b : e["morphTargetBinds"])
		{
			if (!b.is_object() || !b.contains("node") || !b["node"].is_number_integer() || !b.contains("index") ||
			    !b["index"].is_number_integer())
			{
				warnings.push_back("expression " + name + ": morphTargetBinds の node と index は整数");
				continue;
			}
			x.binds.push_back({b["node"].get<int>(), b["index"].get<int>(), jsonNumber(b, "weight", 1.0f)});
		}
	}
	out.expressions.push_back(std::move(x));
}

[[nodiscard]] inline const nlohmann::json* findExpressionsRoot(const nlohmann::json& root)
{
	if (root.contains("expressions") && root["expressions"].is_object()) { return &root["expressions"]; }
	if (root.contains("extensions") && root["extensions"].is_object() && root["extensions"].contains("VRMC_vrm") &&
	    root["extensions"]["VRMC_vrm"].is_object() && root["extensions"]["VRMC_vrm"].contains("expressions"))
	{
		return &root["extensions"]["VRMC_vrm"]["expressions"];
	}
	return &root;
}

} // namespace detail

/// @brief 表情の JSON を読む。JSON として読めなければ nullopt (error に理由)。表の中の誤りは warnings に積んで、その表情だけ捨てる
[[nodiscard]] inline std::optional<MorphExpressionSet> parseMorphExpressions(std::string_view text, std::string* error = nullptr,
                                                                             std::vector<std::string>* warnings = nullptr)
{
	std::string syntaxError;
	const auto root = data::parseJsonText(text, "expressions.json", syntaxError);
	if (root.is_discarded() || !root.is_object())
	{
		if (error != nullptr) { *error = syntaxError.empty() ? "JSON の書き方が間違っているか、一番外側が { } のオブジェクトではありません" : syntaxError; }
		return std::nullopt;
	}
	std::vector<std::string> localWarnings;
	auto& warn = warnings != nullptr ? *warnings : localWarnings;
	MorphExpressionSet set;
	const nlohmann::json& body = *detail::findExpressionsRoot(root);
	for (const char* group : {"preset", "custom"})
	{
		if (!body.contains(group) || !body[group].is_object()) { continue; }
		for (const auto& [name, e] : body[group].items()) { detail::readExpression(name, e, group[0] == 'p', set, warn); }
	}
	constexpr std::size_t kMax = static_cast<std::size_t>(kExpressionMaxEntries) * 4;
	if (set.expressions.size() > kMax)
	{
		warn.push_back("表情が多すぎる (先頭の " + std::to_string(kMax) + " 個だけ使う)");
		set.expressions.resize(kMax);
	}
	return set;
}

} // namespace mitiru::animation
