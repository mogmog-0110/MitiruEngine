#pragma once

/// @file Light2DMath.hpp
/// @brief 2D ライトの式のリファレンス実装 (CPU) と、シェーダーへ渡す定数のパック。
/// @details 光の当たり方の正本。DX12 の lit ピクセルシェーダー、Dawn の batch2d.slang、GPU の試験はどれもこの式と突き合わせる。
///          論理座標は y が下向きで、法線マップの緑は画像の上向きを正とする (Unity / Godot と同じ)。
///          host 専用で、ゲームの DLL へ渡す型 (Light2DPod) とは別。

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <mitiru/render/Light2D.hpp>

namespace mitiru::render
{

/// 1 フレームぶんの光。座標は Screen の変換を通した後の論理座標
struct Lighting2D
{
	std::uint32_t count = 0;
	bool ambientSet = false;
	float ambient[3] = {0.0f, 0.0f, 0.0f};   ///< 色 x 強さ。ambient2D を呼ばなければ黒 (灯りの無い所は真っ暗)
	Light2DPod lights[kMaxLights2D];

	[[nodiscard]] bool active() const noexcept { return count > 0 || ambientSet; }
};

namespace light2d
{

inline constexpr float kDegToRad = 3.14159265358979f / 180.0f;
inline constexpr float kNoCone = -2.0f;   ///< cosOuter がこの値なら円錐なし (cos は -1 より下にならない)

/// シェーダーの定数配列に 1 灯が使う float4 の数
inline constexpr int kVec4PerLight = 3;

/// 1 灯ぶんの定数。float4 x 3
struct PackedLight
{
	float posHeightRadius[4];   ///< x, y, 高さ, 半径
	float colorFalloff[4];      ///< 色 x 強さ, 減衰の指数
	float coneDir[4];           ///< 円錐の向き (x, y)、cos(外側)、cos(内側)
};

[[nodiscard]] inline PackedLight pack(const Light2DPod& l) noexcept
{
	PackedLight p{};
	p.posHeightRadius[0] = l.x;
	p.posHeightRadius[1] = l.y;
	p.posHeightRadius[2] = l.height;
	p.posHeightRadius[3] = std::max(l.radius, 1e-4f);
	p.colorFalloff[0] = l.color[0] * l.intensity;
	p.colorFalloff[1] = l.color[1] * l.intensity;
	p.colorFalloff[2] = l.color[2] * l.intensity;
	p.colorFalloff[3] = std::max(l.falloff, 0.0f);
	if (l.coneDeg > 0.0f)
	{
		const float dir = l.directionDeg * kDegToRad;
		p.coneDir[0] = std::cos(dir);
		p.coneDir[1] = std::sin(dir);
		p.coneDir[2] = std::cos(l.coneDeg * kDegToRad);
		p.coneDir[3] = std::cos(l.coneDeg * 0.5f * kDegToRad);   // 縁は外側の半角から半分の角度にかけて消える
	}
	else
	{
		p.coneDir[0] = 1.0f;
		p.coneDir[1] = 0.0f;
		p.coneDir[2] = kNoCone;
		p.coneDir[3] = kNoCone;
	}
	return p;
}

[[nodiscard]] inline float smoothstep(float edge0, float edge1, float x) noexcept
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/// 法線マップの 1 画素 (0..255) を接線空間の単位ベクトルへ。x = 画像の右、y = 画像の上、z = 手前
inline void decodeNormal(const std::uint8_t rgb[3], float out[3]) noexcept
{
	for (int i = 0; i < 3; ++i) { out[i] = static_cast<float>(rgb[i]) / 255.0f * 2.0f - 1.0f; }
	const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (len < 1e-6f) { out[0] = 0.0f; out[1] = 0.0f; out[2] = 1.0f; return; }
	for (int i = 0; i < 3; ++i) { out[i] /= len; }
}

/// 接線空間の法線を、画面の論理座標 (y 下向き) の法線へ。duvdx / duvdy は UV の画面 1 画素あたりの変化。
/// 反転 (flipX) や回転でも、画像の右と上が画面のどちらを向くかが微分から決まる
inline void tangentToScreen(const float tangent[3], const float duvdx[2], const float duvdy[2], float out[3]) noexcept
{
	const float det = duvdx[0] * duvdy[1] - duvdy[0] * duvdx[1];
	const float sign = det < 0.0f ? -1.0f : 1.0f;
	float t[2] = {duvdy[1], -duvdx[1]};    // +u が画面で向く方向
	float b[2] = {-duvdy[0], duvdx[0]};    // +v が画面で向く方向
	const float tl = std::sqrt(t[0] * t[0] + t[1] * t[1]);
	const float bl = std::sqrt(b[0] * b[0] + b[1] * b[1]);
	if (tl < 1e-12f || bl < 1e-12f) { out[0] = 0.0f; out[1] = 0.0f; out[2] = 1.0f; return; }
	for (int i = 0; i < 2; ++i) { t[i] = t[i] / tl * sign; b[i] = b[i] / bl * sign; }
	out[0] = tangent[0] * t[0] + tangent[1] * -b[0];
	out[1] = tangent[0] * t[1] + tangent[1] * -b[1];
	out[2] = tangent[2];
}

/// 画面の点 (px, py) に当たる光の合計 (環境光を含む)。normal が null なら法線マップなしの面で、向きの項は 1
inline void evaluate(const Lighting2D& lighting, float px, float py, const float* normal, float out[3]) noexcept
{
	for (int i = 0; i < 3; ++i) { out[i] = lighting.ambient[i]; }
	for (std::uint32_t i = 0; i < lighting.count; ++i)
	{
		const PackedLight l = pack(lighting.lights[i]);
		const float dx = l.posHeightRadius[0] - px;
		const float dy = l.posHeightRadius[1] - py;
		const float dist = std::sqrt(dx * dx + dy * dy);
		const float radius = l.posHeightRadius[3];
		if (dist >= radius) { continue; }
		float weight = std::pow(1.0f - dist / radius, l.colorFalloff[3]);
		if (l.coneDir[2] > kNoCone + 0.5f)
		{
			float ux = l.coneDir[0];
			float uy = l.coneDir[1];
			if (dist > 1e-5f) { ux = -dx / dist; uy = -dy / dist; }
			const float cosA = l.coneDir[0] * ux + l.coneDir[1] * uy;
			weight *= smoothstep(l.coneDir[2], l.coneDir[3], cosA);
		}
		if (normal != nullptr)
		{
			float lx = dx, ly = dy, lz = l.posHeightRadius[2];
			const float len = std::max(std::sqrt(lx * lx + ly * ly + lz * lz), 1e-6f);
			lx /= len; ly /= len; lz /= len;
			weight *= std::max(normal[0] * lx + normal[1] * ly + normal[2] * lz, 0.0f);
		}
		for (int c = 0; c < 3; ++c) { out[c] += l.colorFalloff[c] * weight; }
	}
}

}  // namespace light2d

}  // namespace mitiru::render
