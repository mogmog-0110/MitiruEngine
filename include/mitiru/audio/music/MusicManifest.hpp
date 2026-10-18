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

/// @brief 名前の付いた軸 1 本の曲線。axis は MusicManifest::axes の番号、points は軸の値の昇順の (値, 音量) の折れ線
struct MusicAxisCurve
{
	int                               axis = 0;
	std::vector<std::array<float, 2>> points;
};

/// @brief 強さに応じて音量が変わる層。curve は強さの昇順に並ぶ (強さ, 音量 0..1) の折れ線
/// @details axisCurves は hud.musicParam で動かす名前付きの軸の曲線で、層の音量は curve と全部の軸の曲線の値の積になる
struct MusicLayer
{
	std::string                       id;
	std::vector<std::array<float, 2>> curve;
	std::vector<MusicAxisCurve>       axisCurves;
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
	std::vector<std::string>     axes;   ///< 層の curve に書いた強さ以外の軸の名前 (MusicAxisCurve::axis の番号順)

	[[nodiscard]] int segmentIndex(std::string_view id) const noexcept { return find(segments, id); }
	[[nodiscard]] int stingerIndex(std::string_view id) const noexcept { return find(stingers, id); }
	[[nodiscard]] int layerIndex(std::string_view id) const noexcept { return find(layers, id); }
	[[nodiscard]] int axisIndex(std::string_view name) const noexcept
	{
		const auto it = std::find(axes.begin(), axes.end(), name);
		return (it == axes.end()) ? -1 : static_cast<int>(it - axes.begin());
	}

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

	/// @brief intensity で決まる層の音量。折れ線の範囲外では端の値を使う。名前付きの軸は 0 とみなす
	[[nodiscard]] float layerGain(int layer, float intensity) const noexcept { return layerGain(layer, intensity, {}); }

	/// @brief intensity と名前付きの軸の値 (axes と同じ並び、足りない分は 0) で決まる層の音量
	[[nodiscard]] float layerGain(int layer, float intensity, const std::vector<float>& axisValues) const noexcept
	{
		if (layer < 0 || layer >= static_cast<int>(layers.size())) { return 1.0f; }
		const MusicLayer& l = layers[static_cast<std::size_t>(layer)];
		float gain = l.curve.empty() ? 1.0f : evalCurve(l.curve, intensity);
		for (const MusicAxisCurve& c : l.axisCurves)
		{
			const auto a = static_cast<std::size_t>(c.axis);
			gain *= evalCurve(c.points, (a < axisValues.size()) ? axisValues[a] : 0.0f);
		}
		return gain;
	}

	/// @brief 折れ線の x での値。範囲外では端の値、空なら 1
	[[nodiscard]] static float evalCurve(const std::vector<std::array<float, 2>>& c, float x) noexcept
	{
		if (c.empty()) { return 1.0f; }
		if (x <= c.front()[0]) { return c.front()[1]; }
		for (std::size_t i = 1; i < c.size(); ++i)
		{
			if (x > c[i][0]) { continue; }
			const float span = c[i][0] - c[i - 1][0];
			const float u = (span > 0.0f) ? (x - c[i - 1][0]) / span : 1.0f;
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
