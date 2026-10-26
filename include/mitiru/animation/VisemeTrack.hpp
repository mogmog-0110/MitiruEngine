#pragma once

/// @file VisemeTrack.hpp
/// @brief 前もって焼いた口の形の時刻表 (`<名前>.visemes.json`)。時刻から aa ih ou ee oh の重みを引く純関数。
/// @details 実行時に音を解析しない。音のスレッドの都合で値が変わると、リプレイで口が合わなくなるため。
///          時刻はゲームが GameMemory に持つ秒 (音を鳴らし始めてからの秒) で、同じ時刻からは同じ重みが出る。
///          表は tools/bake_visemes.py が WAV から作る。書式: {"keys": [{"t": 0.0, "aa": 0.0, "ih": 0.0, "ou": 0.0, "ee": 0.0, "oh": 0.0}, ...]}
///          書かない母音は 0。キーの間は線形に補間し、最初のキーより前と最後のキーより後は口を閉じる (全部 0)。

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/animation/MorphExpressionTypes.hpp>

namespace mitiru::animation
{

inline constexpr int kVisemeCount = 5;
inline constexpr const char* kVisemeNames[kVisemeCount] = {"aa", "ih", "ou", "ee", "oh"};

struct VisemeTrack
{
	std::vector<float> times;                              ///< 昇順の秒
	std::vector<std::array<float, kVisemeCount>> weights;  ///< times と同数

	[[nodiscard]] float durationSec() const noexcept { return times.empty() ? 0.0f : times.back(); }
};

/// @brief 時刻 t の重みを out[kVisemeCount] へ書く
inline void sampleViseme(const VisemeTrack& track, float t, float* out) noexcept
{
	for (int i = 0; i < kVisemeCount; ++i) { out[i] = 0.0f; }
	if (track.times.empty() || track.weights.size() != track.times.size() || !(t >= track.times.front()) || t > track.times.back())
	{
		return;
	}
	const auto it = std::upper_bound(track.times.begin(), track.times.end(), t);
	const auto next = static_cast<std::size_t>(it - track.times.begin());
	if (next >= track.times.size())
	{
		for (int i = 0; i < kVisemeCount; ++i) { out[i] = track.weights.back()[static_cast<std::size_t>(i)]; }
		return;
	}
	const float t0 = track.times[next - 1];
	const float span = track.times[next] - t0;
	const float u = span > 1e-8f ? (t - t0) / span : 0.0f;
	for (std::size_t i = 0; i < kVisemeCount; ++i)
	{
		const float a = track.weights[next - 1][i];
		out[i] = a + (track.weights[next][i] - a) * u;
	}
}

/// @brief 時刻 t の口の形を、ExpressionPose の aa ih ou ee oh へ置く (0 の母音は項目を消す)
inline void applyViseme(ExpressionPose& pose, const VisemeTrack& track, float t) noexcept
{
	float w[kVisemeCount];
	sampleViseme(track, t, w);
	for (int i = 0; i < kVisemeCount; ++i) { (void)setExpression(pose, kVisemeNames[i], w[i]); }
}

/// @brief JSON の文字列から表を読む。形が違えば nullopt (error に理由)。キーは時刻の昇順に並べ直す
[[nodiscard]] inline std::optional<VisemeTrack> parseVisemeTrack(std::string_view text, std::string* error = nullptr)
{
	std::string syntax;
	const auto root = data::parseJsonText(text, "visemes.json", syntax);
	const auto fail = [&](const std::string& why) -> std::optional<VisemeTrack> {
		if (error != nullptr) { *error = why; }
		return std::nullopt;
	};
	if (root.is_discarded() || !root.is_object()) { return fail(syntax.empty() ? "一番外側が { } のオブジェクトではありません" : syntax); }
	if (!root.contains("keys") || !root["keys"].is_array()) { return fail("\"keys\" の配列がありません"); }
	struct Key
	{
		float t;
		std::array<float, kVisemeCount> w;
	};
	std::vector<Key> keys;
	for (const auto& k : root["keys"])
	{
		if (!k.is_object() || !k.contains("t") || !k["t"].is_number()) { return fail("keys の要素には数の \"t\" (秒) が要ります"); }
		Key key{k["t"].get<float>(), {}};
		for (std::size_t i = 0; i < kVisemeCount; ++i)
		{
			const float w = (k.contains(kVisemeNames[i]) && k[kVisemeNames[i]].is_number()) ? k[kVisemeNames[i]].get<float>() : 0.0f;
			key.w[i] = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
		}
		keys.push_back(key);
	}
	std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
	VisemeTrack track;
	for (const Key& k : keys)
	{
		track.times.push_back(k.t);
		track.weights.push_back(k.w);
	}
	return track;
}

} // namespace mitiru::animation
