#pragma once

/// @file MusicManifest.hpp
/// @brief 曲の区間、層、切り替え規則、スティンガーを表すデータ
/// @details assets/audio/music.json は MusicManifestJson.hpp がこの形に読み込む。名前は読み込み時に番号へ変換し、MusicDirector は番号だけを見る。

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::audio::music
{

/// @brief 切り替えや層の変化を、どの区切りまで待つか
enum class MusicSync : std::uint8_t
{
	Immediate,  ///< 待たない
	Beat,       ///< 次の拍
	Bar,        ///< 次の小節
	Marker,     ///< 区間に書いた次のマーカー (無ければ Exit)
	Exit,       ///< 区間の終わり。今の区間は止めず、後ろに残る響きをそのまま鳴らす
};

/// @brief 区間で鳴らす 1 本の音源。layer が -1 なら強さにかかわらず鳴る
struct MusicStem
{
	std::string file;
	int         layer = -1;
};

/// @brief 小節数で長さを決める曲の 1 区間。音源がこの長さを超えた分は、響きとして重なる
struct MusicSegment
{
	std::string             id;
	double                  bpm = 120.0;
	int                     beatsPerBar = 4;
	int                     bars = 4;
	bool                    loop = true;
	int                     next = -1;      ///< loop=false で終わった後に続く区間 (-1 = そこで止まる)
	std::vector<int>        markerBeats;    ///< MusicSync::Marker で抜けてよい位置 (区間の頭からの拍、昇順)
	std::vector<MusicStem>  stems;
};

/// @brief 強さに応じて音量が変わる層。curve は強さの昇順に並ぶ (強さ, 音量 0..1) の折れ線
struct MusicLayer
{
	std::string                       id;
	std::vector<std::array<float, 2>> curve;
	float                             fadeSec = 1.0f;
	MusicSync                         sync = MusicSync::Immediate;
};

/// @brief from から to へ移るときの待ち方。from または to が -1 なら、どの区間にも当てはまる
struct MusicTransition
{
	int       from = -1;
	int       to = -1;
	MusicSync sync = MusicSync::Bar;
	float     fadeOutSec = 0.0f;
	float     fadeInSec = 0.0f;
	int       via = -1;   ///< 間に 1 回だけ挟む区間 (-1 = 挟まない)
};

/// @brief 曲の上に重ねる短いフレーズ
struct MusicStinger
{
	std::string id;
	std::string file;
	MusicSync   sync = MusicSync::Beat;
	float       gain = 1.0f;
};

struct MusicManifest
{
	std::vector<MusicLayer>      layers;
	std::vector<MusicSegment>    segments;
	std::vector<MusicTransition> transitions;
	std::vector<MusicStinger>    stingers;

	[[nodiscard]] int segmentIndex(std::string_view id) const noexcept { return find(segments, id); }
	[[nodiscard]] int stingerIndex(std::string_view id) const noexcept { return find(stingers, id); }
	[[nodiscard]] int layerIndex(std::string_view id) const noexcept { return find(layers, id); }

	/// @brief from から to への規則。両方が一致する規則を最優先し、無ければ次の小節でフェードせずに切り替える
	[[nodiscard]] MusicTransition rule(int from, int to) const noexcept
	{
		const MusicTransition* best = nullptr;
		int bestScore = -1;
		for (const auto& t : transitions)
		{
			if ((t.from != -1 && t.from != from) || (t.to != -1 && t.to != to)) { continue; }
			const int score = (t.to != -1 ? 2 : 0) + (t.from != -1 ? 1 : 0);
			if (score > bestScore) { best = &t; bestScore = score; }
		}
		if (best != nullptr) { return *best; }
		MusicTransition fallback;
		fallback.from = from;
		fallback.to = to;
		return fallback;
	}

	/// @brief intensity で決まる層の音量。折れ線の範囲外では端の値を使う
	[[nodiscard]] float layerGain(int layer, float intensity) const noexcept
	{
		if (layer < 0 || layer >= static_cast<int>(layers.size())) { return 1.0f; }
		const auto& c = layers[static_cast<std::size_t>(layer)].curve;
		if (c.empty()) { return 1.0f; }
		if (intensity <= c.front()[0]) { return c.front()[1]; }
		for (std::size_t i = 1; i < c.size(); ++i)
		{
			if (intensity > c[i][0]) { continue; }
			const float span = c[i][0] - c[i - 1][0];
			const float u = (span > 0.0f) ? (intensity - c[i - 1][0]) / span : 1.0f;
			return c[i - 1][1] + (c[i][1] - c[i - 1][1]) * u;
		}
		return c.back()[1];
	}

private:
	template <class T>
	[[nodiscard]] static int find(const std::vector<T>& items, std::string_view id) noexcept
	{
		const auto it = std::find_if(items.begin(), items.end(), [&](const T& x) { return x.id == id; });
		return (it == items.end()) ? -1 : static_cast<int>(it - items.begin());
	}
};

}  // namespace mitiru::audio::music
