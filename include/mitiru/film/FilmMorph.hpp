#pragma once

/// @file FilmMorph.hpp
/// @brief 小道具のモーフ (口・まばたき・眉・表情) の重みを、時刻だけから求める純関数。書式と理由は docs/FILM_MORPH.md、ADR 0157。
/// @details 結果は ExpressionPose (名前の鍵と 0..1 の重み)。スロットの重みへの変換は animation::evaluateMorphWeights が行う
///          (表情の定義 `<model>.expressions.json` があれば VRM の規則、無ければ同じ名前のモーフを直接動かす)。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <mitiru/animation/AnimNameKey.hpp>
#include <mitiru/animation/MorphExpressionTypes.hpp>
#include <mitiru/animation/VisemeTrack.hpp>
#include <mitiru/film/FilmEditShots.hpp>
#include <mitiru/film/FilmEval.hpp>
#include <mitiru/film/FilmTypes.hpp>

namespace mitiru::film
{

namespace morph_detail
{

/// @brief 同名の重みは加算し、上限を 1 とする。項目が満杯なら false を返す。
inline bool addWeight(animation::ExpressionPose& pose, std::uint32_t key, float w) noexcept
{
	if (!(w > 0.0f)) { return true; }
	const std::uint32_t count = std::min<std::uint32_t>(pose.count, animation::kExpressionMaxEntries);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (pose.entries[i].key == key) { pose.entries[i].weight = std::min(1.0f, pose.entries[i].weight + w); return true; }
	}
	if (count >= static_cast<std::uint32_t>(animation::kExpressionMaxEntries)) { return false; }
	pose.entries[count] = {key, std::min(1.0f, w)};
	pose.count = count + 1;
	return true;
}

}  // namespace morph_detail

/// @brief キーの軌跡には小道具と同じワールド時刻、口の表には声の cue と同じ映像時刻を使う。time_remap で声は伸びない。
[[nodiscard]] inline animation::ExpressionPose evaluateMorphPose(const Prop& p, float tWorld, float tFilm) noexcept
{
	using animation::animNameKey;
	animation::ExpressionPose pose{};
	for (const MorphChannel& c : p.morphs)
	{
		if (c.visemes.empty())
		{
			(void)morph_detail::addWeight(pose, animNameKey(c.name), sampleTrack(c.weight, tWorld, 0.0f) * c.gain);
			continue;
		}
		float w[animation::kVisemeCount];
		animation::sampleViseme(c.track, tFilm - c.start, w);
		if (c.name.empty())
		{
			for (int i = 0; i < animation::kVisemeCount; ++i) { (void)morph_detail::addWeight(pose, animNameKey(animation::kVisemeNames[i]), w[i] * c.gain); }
			continue;
		}
		float open = 0.0f;
		for (int i = 0; i < animation::kVisemeCount; ++i) { open += w[i]; }
		(void)morph_detail::addWeight(pose, animNameKey(c.name), std::min(1.0f, open) * c.gain);
	}
	return pose;
}

/// @brief 小道具の morphs が動かす名前の種類を数える。ExpressionPose の上限 16 を超えると重みが落ちるため、読み込みで断る。
[[nodiscard]] inline int morphNameCount(const Prop& p)
{
	animation::ExpressionPose pose{};
	int distinct = 0;
	const auto add = [&](std::string_view name) {
		const std::uint32_t key = animation::animNameKey(name);
		for (std::uint32_t i = 0; i < pose.count; ++i) { if (pose.entries[i].key == key) { return; } }
		if (pose.count < static_cast<std::uint32_t>(animation::kExpressionMaxEntries)) { pose.entries[pose.count++] = {key, 1.0f}; }
		++distinct;
	};
	for (const MorphChannel& c : p.morphs)
	{
		if (!c.visemes.empty() && c.name.empty()) { for (const char* v : animation::kVisemeNames) { add(v); } }
		else { add(c.name); }
	}
	return distinct;
}

/// @brief 口の表の JSON を、host の asset、指定パス、映像のフォルダ、その親 4 つ上までの `<親>/`、`<親>/assets/` の順に探す。
/// 最後の 2 つは、host の外で動く `mitiru_film check` や patch がリポジトリの `films/<名前>/` から `assets/` を見つけるために使う。
[[nodiscard]] inline std::optional<std::string> readMorphAsset(const std::string& path, const std::string& baseDir)
{
	const auto slurp = [](const std::filesystem::path& p) -> std::optional<std::string> {
		std::error_code ec;
		if (!std::filesystem::is_regular_file(p, ec)) { return std::nullopt; }
		std::ifstream in(p, std::ios::binary);
		if (!in) { return std::nullopt; }
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	};
	if (auto hosted = readFileDefault(path)) { return hosted; }
	if (const auto direct = slurp(path)) { return direct; }
	std::filesystem::path dir = baseDir.empty() ? std::filesystem::path{"."} : std::filesystem::path{baseDir};
	for (int up = 0; up < 5; ++up)
	{
		if (const auto a = slurp(dir / path)) { return a; }
		if (const auto b = slurp(dir / "assets" / path)) { return b; }
		if (!dir.has_parent_path() || dir.parent_path() == dir) { break; }
		dir = dir.parent_path();
	}
	return std::nullopt;
}

}  // namespace mitiru::film
