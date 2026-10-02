#pragma once

/// @file AnimBlendSpace.hpp
/// @brief ブレンドスペース (1D / 2D) の重みと、重みの組をレイヤの列へ変える関数。
/// @details 重みは足して 1。pushBlend は重み w_k を「それまでの和に対する割合」にした Override の
///          列を作るので、平行移動とスケールは重み付き平均と一致し、回転は slerp を順に重ねた値になる。
///          2D は gradient band 補間で、加減乗除だけで決まる。

#include <algorithm>
#include <cstdint>

#include <sgc/math/Vec2.hpp>

#include <mitiru/animation/AnimPose.hpp>

namespace mitiru::animation
{

inline constexpr int kMaxBlendSamples = 8;

struct BlendWeight
{
	int index = -1;
	float weight = 0.0f;
};

/// @brief 1D: 昇順の thresholds 上で x を挟む 2 点の重み。戻り値は個数 (1 か 2、n<=0 なら 0)。
inline int blendSpace1D(const float* thresholds, int n, float x, BlendWeight out[2]) noexcept
{
	if (thresholds == nullptr || n <= 0) { return 0; }
	if (n == 1 || x <= thresholds[0])
	{
		out[0] = {0, 1.0f};
		return 1;
	}
	if (x >= thresholds[n - 1])
	{
		out[0] = {n - 1, 1.0f};
		return 1;
	}
	int i = 0;
	while (i + 1 < n && x > thresholds[i + 1]) { ++i; }
	const float span = thresholds[i + 1] - thresholds[i];
	const float u = span > 0.0f ? (x - thresholds[i]) / span : 0.0f;
	out[0] = {i, 1.0f - u};
	out[1] = {i + 1, u};
	return 2;
}

/// @brief 2D: points (n <= kMaxBlendSamples) の gradient band 補間。重みが 0 の点は出さない。
/// @return 書いた個数。点に重なれば その点だけ。
inline int blendSpace2D(const sgc::Vec2f* points, int n, sgc::Vec2f p, BlendWeight* out) noexcept
{
	if (points == nullptr || out == nullptr || n <= 0) { return 0; }
	n = std::min(n, kMaxBlendSamples);
	float raw[kMaxBlendSamples] = {};
	float total = 0.0f;
	for (int i = 0; i < n; ++i)
	{
		float w = 1.0f;
		for (int j = 0; j < n; ++j)
		{
			if (i == j) { continue; }
			const sgc::Vec2f ij{points[j].x - points[i].x, points[j].y - points[i].y};
			const float len2 = ij.x * ij.x + ij.y * ij.y;
			if (len2 <= 0.0f) { continue; }
			const float h = 1.0f - ((p.x - points[i].x) * ij.x + (p.y - points[i].y) * ij.y) / len2;
			w = std::min(w, h);
		}
		raw[i] = std::max(w, 0.0f);
		total += raw[i];
	}
	int count = 0;
	for (int i = 0; i < n && total > 0.0f; ++i)
	{
		if (raw[i] > 0.0f) { out[count++] = {i, raw[i] / total}; }
	}
	return count;
}

/// @brief 重みの組を Override レイヤの列として params へ足す。clips[k] / times[k] は weights[k].index で引く。
/// @return 全部入れば true。
inline bool pushBlend(AnimPoseParams& params, const BlendWeight* weights, int n, const std::int16_t* clips,
                      const float* times, std::int16_t mask = -1, std::uint8_t flags = kAnimLayerLoop) noexcept
{
	float sum = 0.0f;
	bool ok = true;
	for (int k = 0; k < n; ++k)
	{
		const float w = weights[k].weight;
		if (!(w > 0.0f)) { continue; }
		sum += w;
		AnimLayer layer;
		layer.clip = clips[weights[k].index];
		layer.mask = mask;
		layer.flags = flags;
		layer.timeSec = times[weights[k].index];
		layer.weight = w / sum;
		ok = pushLayer(params, layer) && ok;
	}
	return ok;
}

/// @brief 位相 (0..1) をクリップの時刻へ変える。速さの違うクリップを足並みそろえて混ぜるときに使う。
[[nodiscard]] inline float phaseToTime(const AnimAsset& asset, int clip, float phase01) noexcept
{
	const AnimClip* c = asset.clip(clip);
	return c != nullptr ? phase01 * c->durationSec : 0.0f;
}

} // namespace mitiru::animation
