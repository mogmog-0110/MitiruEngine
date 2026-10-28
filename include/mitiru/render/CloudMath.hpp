#pragma once

/// @file CloudMath.hpp
/// @brief 雲の層 (R5) の被覆率を求める CPU 実装。DX12AtmosphereShaders.hpp の applyCloudLayer と shaders/dawn/sky.slang の applyCloudLayer と同じ手順で d * fade を求め、試験画像の画素と比較する。
/// @details 色は大気の LUT に依存するため扱わない。戻り値が 0 なら空の色が残り、1 に近いほど雲の色に置き換わる。

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru::render::cloud
{

[[nodiscard]] inline float smoothstep(float a, float b, float x) noexcept
{
	const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] inline float hash(std::int32_t x, std::int32_t y) noexcept
{
	std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
	h = (h ^ (h >> 13u)) * 1274126177u;
	return static_cast<float>(h ^ (h >> 16u)) * (1.0f / 4294967296.0f);
}

[[nodiscard]] inline float noise(float px, float py) noexcept
{
	const float ix = std::floor(px), iy = std::floor(py);
	const float fx = px - ix, fy = py - iy;
	const float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
	const auto xi = static_cast<std::int32_t>(ix), yi = static_cast<std::int32_t>(iy);
	const float a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
	const float top = a + (b - a) * ux, bottom = c + (d - c) * ux;
	return top + (bottom - top) * uy;
}

[[nodiscard]] inline float fbm(float px, float py) noexcept
{
	float v = 0.0f, a = 0.5f;
	for (int k = 0; k < 4; ++k)
	{
		v += a * noise(px, py);
		px = px * 2.03f + 17.0f;
		py = py * 2.03f + 17.0f;
		a *= 0.5f;
	}
	return v / 0.9375f;
}

/// 視線が雲で覆われる割合 0 から 1。dirX、dirY、dirZ は y を上とする単位ベクトルで、戻り値は色を混ぜる前の d * fade。
/// bands は SkyLook::toonBands で、2 以上なら量を段階化する。cover が 0 以下、または地平線の近くより下なら 0。
[[nodiscard]] inline float coverage(float dirX, float dirY, float dirZ, float cover, float timeSec, int bands) noexcept
{
	const float fade = smoothstep(0.02f, 0.22f, dirY);
	if (cover <= 0.0f || fade <= 0.0f) { return 0.0f; }
	const float inv = 1.6f / (dirY + 0.15f);
	const float u = dirX * inv + timeSec * 0.012f;
	const float v = dirZ * inv + timeSec * 0.005f;
	const float n = fbm(u, v);
	const float thr = 1.0f - cover;
	float d = smoothstep(thr - 0.02f, thr + 0.28f, n);
	if (bands >= 2) { d = std::round(d * static_cast<float>(bands)) / static_cast<float>(bands); }
	return d * fade * 0.96f;
}

}  // namespace mitiru::render::cloud
