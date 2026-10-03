#pragma once

/// @file ProbeMath.hpp
/// @brief 光のプローブが使う数学。2 次の球面調和 (SH L2) と八面体の方向の符号化
/// @details 焼く側 (CPU) と描く側 (HLSL、DX12LightingProbeShaders.hpp) が同じ式を使う。係数は余弦の畳み込みを
///          済ませた放射照度の形で持つので、法線 n の放射照度は sum_k c_k Y_k(n) の 1 回の内積で出る
///          (Ramamoorthi と Hanrahan の 2001 年の論文の式)。

#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru::render::gi
{

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr int kShCoefficients = 9;

struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] constexpr Vec3 operator*(Vec3 a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
[[nodiscard]] constexpr Vec3 mul(Vec3 a, Vec3 b) noexcept { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) noexcept
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline float length(Vec3 v) noexcept { return std::sqrt(dot(v, v)); }
[[nodiscard]] inline Vec3 normalize(Vec3 v) noexcept
{
	const float l = length(v);
	return l > 1e-20f ? v * (1.0f / l) : Vec3{0.0f, 1.0f, 0.0f};
}

/// @brief SH L2 の基底 9 個。並びは (l, m) = (0,0) (1,-1) (1,0) (1,1) (2,-2) (2,-1) (2,0) (2,1) (2,2)
[[nodiscard]] inline std::array<float, kShCoefficients> shBasis(Vec3 n) noexcept
{
	return {0.282095f,
	        0.488603f * n.y,
	        0.488603f * n.z,
	        0.488603f * n.x,
	        1.092548f * n.x * n.y,
	        1.092548f * n.y * n.z,
	        0.315392f * (3.0f * n.z * n.z - 1.0f),
	        1.092548f * n.x * n.z,
	        0.546274f * (n.x * n.x - n.y * n.y)};
}

/// @brief 帯ごとの余弦の畳み込み (A0 = pi, A1 = 2pi/3, A2 = pi/4)。放射輝度の係数に掛けると放射照度の係数になる
[[nodiscard]] constexpr float shCosineBand(int k) noexcept
{
	return k == 0 ? kPi : (k < 4 ? 2.0f * kPi / 3.0f : kPi / 4.0f);
}

/// @brief RGB の SH L2。c[k] は係数 k の (r, g, b)
struct ShL2Rgb
{
	std::array<Vec3, kShCoefficients> c{};

	/// @brief 球面上の 1 標本を足す。weight は立体角 (一様な N 標本なら 4pi / N)
	void addRadiance(Vec3 dir, Vec3 radiance, float weight) noexcept
	{
		const auto y = shBasis(dir);
		for (int k = 0; k < kShCoefficients; ++k) { c[k] = c[k] + radiance * (y[k] * weight); }
	}

	/// @brief 放射輝度の係数を、余弦を畳み込んだ放射照度の係数にする
	[[nodiscard]] ShL2Rgb convolvedToIrradiance() const noexcept
	{
		ShL2Rgb out;
		for (int k = 0; k < kShCoefficients; ++k) { out.c[k] = c[k] * shCosineBand(k); }
		return out;
	}

	/// @brief 放射照度の係数から、法線 n の放射照度。2 次で切った負の振れは 0 に止める
	[[nodiscard]] Vec3 irradiance(Vec3 n) const noexcept
	{
		const auto y = shBasis(n);
		Vec3 e{};
		for (int k = 0; k < kShCoefficients; ++k) { e = e + c[k] * y[k]; }
		return {e.x > 0.0f ? e.x : 0.0f, e.y > 0.0f ? e.y : 0.0f, e.z > 0.0f ? e.z : 0.0f};
	}
};

/// @brief 方向を八面体の [-1, 1]^2 へ写す (y が上の半球が内側の菱形)
[[nodiscard]] inline std::array<float, 2> octEncode(Vec3 n) noexcept
{
	const float s = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
	float u = n.x / s;
	float v = n.z / s;
	if (n.y < 0.0f)
	{
		const float pu = (1.0f - std::fabs(v)) * (u >= 0.0f ? 1.0f : -1.0f);
		const float pv = (1.0f - std::fabs(u)) * (v >= 0.0f ? 1.0f : -1.0f);
		u = pu;
		v = pv;
	}
	return {u, v};
}

/// @brief octEncode の逆
[[nodiscard]] inline Vec3 octDecode(float u, float v) noexcept
{
	Vec3 n{u, 1.0f - std::fabs(u) - std::fabs(v), v};
	if (n.y < 0.0f)
	{
		const float pu = (1.0f - std::fabs(v)) * (u >= 0.0f ? 1.0f : -1.0f);
		const float pv = (1.0f - std::fabs(u)) * (v >= 0.0f ? 1.0f : -1.0f);
		n.x = pu;
		n.z = pv;
	}
	return normalize(n);
}

/// @brief 球面フィボナッチの i 番目 (count 個で球をほぼ一様に覆う)
[[nodiscard]] inline Vec3 sphericalFibonacci(std::uint32_t i, std::uint32_t count) noexcept
{
	constexpr float kGolden = 1.61803398874989484820f;
	const float phi = 2.0f * kPi * std::fmod(static_cast<float>(i) / kGolden, 1.0f);
	const float cosTheta = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(count);
	const float sinTheta = std::sqrt(std::fmax(0.0f, 1.0f - cosTheta * cosTheta));
	return {std::cos(phi) * sinTheta, cosTheta, std::sin(phi) * sinTheta};
}

/// @brief 整数のハッシュ (lowbias32)。乱数の状態を持たずに、番号から決まる値を作る
[[nodiscard]] constexpr std::uint32_t hash32(std::uint32_t x) noexcept
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

[[nodiscard]] constexpr float hash01(std::uint32_t x) noexcept
{
	return static_cast<float>(hash32(x) >> 8) * (1.0f / 16777216.0f);
}

} // namespace mitiru::render::gi
