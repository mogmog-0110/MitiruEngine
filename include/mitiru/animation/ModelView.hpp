#pragma once

/// @file ModelView.hpp
/// @brief glb を並べて見る apps/mitiru_view の設定と配置計算。host が設定を JSON にして環境変数で渡し、DLL が読む。

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <sgc/math/Vec3.hpp>

#include <mitiru/animation/ModelCheck.hpp>

namespace mitiru::animation
{

inline constexpr const char* kModelViewEnv = "MITIRU_VIEW";

struct ModelViewItem
{
	std::string path;
	int clip = -1;                  ///< AnimAsset のクリップの番号。-1 はレスト姿勢のまま
	float durationSec = 0.0f;
	sgc::Vec3f boundsMin{0.0f, 0.0f, 0.0f};
	sgc::Vec3f boundsMax{0.0f, 0.0f, 0.0f};
	std::string clipName;
};

struct ModelViewConfig
{
	std::vector<ModelViewItem> models;
	bool toon = false;
};

struct ModelViewLayout
{
	std::vector<float> offsetX;
	std::vector<float> offsetZ;     ///< モデルごとの z のずらし量 (包含箱の奥行の中心を z = 0 に置く)
	sgc::Vec3f eye{0.0f, 1.0f, 5.0f};
	sgc::Vec3f target{0.0f, 0.8f, 0.0f};
	float groundTop = 0.0f;
	float groundHalfX = 4.0f;
	float groundHalfZ = 3.0f;
	float groundThickness = 0.1f;
};

[[nodiscard]] inline ModelViewConfig makeModelViewConfig(const std::vector<ModelCheck>& checks,
	std::string_view clipName, bool toon)
{
	ModelViewConfig config;
	config.toon = toon;
	for (const auto& c : checks)
	{
		const int clip = clipName.empty() ? (c.clips.empty() ? -1 : 0) : c.findClip(clipName);
		const float duration = clip >= 0 ? c.clips[static_cast<std::size_t>(clip)].durationSec : 0.0f;
		const std::string name = clip >= 0 ? c.clips[static_cast<std::size_t>(clip)].name : std::string();
		config.models.push_back({c.path, clip, duration, c.boundsMin, c.boundsMax, name});
	}
	return config;
}

/// @brief 高さが 10 倍以上違う glb を並べると、小さい方がほとんど見えなくなるため、cm と m の混在などを先に知らせる
[[nodiscard]] inline std::string scaleMismatchNote(const std::vector<ModelCheck>& checks)
{
	float lowest = 0.0f, highest = 0.0f;
	for (const auto& c : checks)
	{
		const float h = c.ok() ? c.boundsMax.y - c.boundsMin.y : 0.0f;
		if (h <= 0.0f) { continue; }
		lowest = lowest > 0.0f ? std::min(lowest, h) : h;
		highest = std::max(highest, h);
	}
	if (lowest <= 0.0f || highest < lowest * 10.0f) { return {}; }
	char line[200];
	std::snprintf(line, sizeof line, "警告: モデルの高さが 10 倍以上違う (%.3f と %.3f)。小さい方は並べた画面で見えなくなる\n",
	              static_cast<double>(lowest), static_cast<double>(highest));
	return line;
}

[[nodiscard]] inline std::string toJson(const ModelViewConfig& config)
{
	nlohmann::json models = nlohmann::json::array();
	for (const auto& m : config.models)
	{
		models.push_back({{"path", m.path}, {"clip", m.clip}, {"clipName", m.clipName}, {"duration", m.durationSec},
		                  {"min", {m.boundsMin.x, m.boundsMin.y, m.boundsMin.z}},
		                  {"max", {m.boundsMax.x, m.boundsMax.y, m.boundsMax.z}}});
	}
	return nlohmann::json{{"toon", config.toon}, {"models", models}}.dump(-1, ' ', true);
}

[[nodiscard]] inline std::optional<ModelViewConfig> parseModelViewConfig(std::string_view text)
{
	const auto root = nlohmann::json::parse(text, nullptr, false);
	if (!root.is_object() || !root.contains("models") || !root["models"].is_array()) { return std::nullopt; }
	ModelViewConfig config;
	config.toon = root.value("toon", false);
	for (const auto& j : root["models"])
	{
		const bool hasKeys = j.is_object() && j.contains("path") && j.contains("min") && j.contains("max");
		if (!hasKeys || !j["path"].is_string()) { return std::nullopt; }
		ModelViewItem m;
		m.path = j["path"].get<std::string>();
		m.clip = j.value("clip", -1);
		m.clipName = j.value("clipName", std::string());
		m.durationSec = j.value("duration", 0.0f);
		const auto lo = j["min"].get<std::vector<float>>();
		const auto hi = j["max"].get<std::vector<float>>();
		if (lo.size() != 3 || hi.size() != 3) { return std::nullopt; }
		m.boundsMin = {lo[0], lo[1], lo[2]};
		m.boundsMax = {hi[0], hi[1], hi[2]};
		config.models.push_back(std::move(m));
	}
	return config;
}

/// @brief 横一列に並べ、全体が画面に収まる距離にカメラを置く。glTF の +z 側から見る
[[nodiscard]] inline ModelViewLayout layoutModelView(const ModelViewConfig& config, float fovDeg, float aspect)
{
	ModelViewLayout out;
	if (config.models.empty()) { return out; }
	float widest = 0.0f;
	for (const auto& m : config.models) { widest = std::max(widest, m.boundsMax.x - m.boundsMin.x); }
	const float gap = std::max(widest * 0.25f, 1e-6f);
	float cursor = 0.0f;
	float lowest = config.models.front().boundsMin.y, highest = config.models.front().boundsMax.y;
	float deepest = 0.0f;
	for (const auto& m : config.models)
	{
		const float width = m.boundsMax.x - m.boundsMin.x;
		out.offsetX.push_back(cursor + width * 0.5f - (m.boundsMin.x + m.boundsMax.x) * 0.5f);
		out.offsetZ.push_back(-(m.boundsMin.z + m.boundsMax.z) * 0.5f);
		cursor += width + gap;
		lowest = std::min(lowest, m.boundsMin.y);
		highest = std::max(highest, m.boundsMax.y);
		deepest = std::max(deepest, m.boundsMax.z - m.boundsMin.z);
	}
	const float totalWidth = std::max(cursor - gap, 1e-6f);
	const float height = std::max(highest - lowest, 1e-6f);
	for (auto& x : out.offsetX) { x -= totalWidth * 0.5f; }

	const float halfTan = std::tan(fovDeg * 0.5f * 3.14159265f / 180.0f);
	const float fitHeight = (height * 0.5f) / halfTan;
	const float fitWidth = (totalWidth * 0.5f) / (halfTan * std::max(aspect, 0.1f));
	const float distance = std::max(fitHeight, fitWidth) * 1.3f + deepest * 0.5f;
	const float centerY = (lowest + highest) * 0.5f;
	out.target = {0.0f, centerY, 0.0f};
	out.eye = {0.0f, centerY + height * 0.12f, distance};
	out.groundTop = lowest;
	out.groundHalfX = std::max(totalWidth, height) * 1.2f;
	out.groundHalfZ = std::max(deepest, height) * 1.2f;
	out.groundThickness = height * 0.03f;
	return out;
}

} // namespace mitiru::animation
