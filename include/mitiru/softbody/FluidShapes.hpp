#pragma once

/// @file FluidShapes.hpp
/// @brief 流れる体の器と面に使う符号付き距離 (外が正) と、その部品の数学。
/// @details 累乗は log2 と exp2 の多項式で自前に計算する。std::pow より速く、同じ実行ファイルなら
///          どの PC でも同じビットになる (CRT の実装に依らない)。

#include <cmath>
#include <cstdint>
#include <cstring>

#include <sgc/math/Vec3.hpp>

namespace mitiru::softbody
{

using Vec3 = sgc::Vec3f;

/// @brief 3 本の列ベクトルで表す向き。ローカルからワールドへは c0 * x + c1 * y + c2 * z
struct Basis
{
	Vec3 c0{1.0f, 0.0f, 0.0f};
	Vec3 c1{0.0f, 1.0f, 0.0f};
	Vec3 c2{0.0f, 0.0f, 1.0f};
};

[[nodiscard]] inline Vec3 toLocal(const Basis& b, const Vec3& v) noexcept { return {v.dot(b.c0), v.dot(b.c1), v.dot(b.c2)}; }
[[nodiscard]] inline Vec3 toWorld(const Basis& b, const Vec3& l) noexcept { return b.c0 * l.x + b.c1 * l.y + b.c2 * l.z; }

[[nodiscard]] inline float length(const Vec3& v) noexcept { return std::sqrt(v.dot(v)); }

/// @brief 長さがほぼ 0 のベクトルはそのまま返す
[[nodiscard]] inline Vec3 normalizeOr(const Vec3& v) noexcept
{
	const float n = length(v);
	return n > 1e-9f ? v / n : v;
}

/// @brief axis (単位でなくてよい) の周りに rad 回す (Rodrigues)
[[nodiscard]] inline Vec3 rotateAround(const Vec3& v, const Vec3& axis, float rad) noexcept
{
	const Vec3 k = normalizeOr(axis);
	const float c = std::cos(rad), s = std::sin(rad);
	return v * c + k.cross(v) * s + k * (k.dot(v) * (1.0f - c));
}

[[nodiscard]] inline Basis rotateBasis(const Basis& b, const Vec3& axis, float rad) noexcept
{
	return {rotateAround(b.c0, axis, rad), rotateAround(b.c1, axis, rad), rotateAround(b.c2, axis, rad)};
}

[[nodiscard]] inline float clamp01(float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

/// @brief 2 を底とする対数。仮数を [√½, √2) に収めた級数で、相対誤差は 1e-7 ほど (x > 0)
[[nodiscard]] inline float fastLog2(float x) noexcept
{
	std::uint32_t bits = 0;
	std::memcpy(&bits, &x, 4);
	int e = static_cast<int>((bits >> 23) & 0xFFu) - 127;
	bits = (bits & 0x007FFFFFu) | 0x3F800000u;
	float m = 0.0f;
	std::memcpy(&m, &bits, 4);
	if (m > 1.41421356f) { m *= 0.5f; ++e; }
	const float t = (m - 1.0f) / (m + 1.0f);
	const float t2 = t * t;
	const float series = t * (2.0f + t2 * (2.0f / 3.0f + t2 * (2.0f / 5.0f + t2 * (2.0f / 7.0f))));
	return static_cast<float>(e) + series * 1.44269504f;
}

/// @brief 2 の累乗。端数を [-½, ½] にした 6 次の級数で、相対誤差は 1e-7 ほど
[[nodiscard]] inline float fastExp2(float y) noexcept
{
	if (y < -126.0f) { return 0.0f; }
	if (y > 127.0f) { y = 127.0f; }
	int ni = static_cast<int>(y + 0.5f);
	if (static_cast<float>(ni) > y + 0.5f) { --ni; }
	const float n = static_cast<float>(ni);
	const float x = (y - n) * 0.693147181f;
	const float p = 1.0f + x * (1.0f + x * (0.5f + x * (1.0f / 6.0f + x * (1.0f / 24.0f + x * (1.0f / 120.0f + x * (1.0f / 720.0f))))));
	const std::uint32_t bits = static_cast<std::uint32_t>(ni + 127) << 23;
	float scale = 0.0f;
	std::memcpy(&scale, &bits, 4);
	return p * scale;
}

/// @brief x の e 乗 (x ≥ 0)
[[nodiscard]] inline float fastPow(float x, float e) noexcept { return x < 1e-30f ? 0.0f : fastExp2(e * fastLog2(x)); }

/// @brief 滑らかな最小 (多項式の smooth min)。k は混ぜる幅 (m)
[[nodiscard]] inline float smoothMin(float a, float b, float k) noexcept
{
	const float h = clamp01(0.5f + 0.5f * (b - a) / k);
	return b * (1.0f - h) + a * h - k * h * (1.0f - h);
}

/// @brief 超楕円体の距離もどき。各軸の |p / half| を e 乗して足し、1 / e 乗から 1 を引いて一番短い半径を掛ける。e = 2 で楕円体
[[nodiscard]] inline float sdSuperEllipsoid(const Vec3& p, const Vec3& half, float e) noexcept
{
	const float s = fastPow(std::fabs(p.x) / half.x, e) + fastPow(std::fabs(p.y) / half.y, e) + fastPow(std::fabs(p.z) / half.z, e);
	const float minHalf = std::fmin(half.x, std::fmin(half.y, half.z));
	return (fastPow(s, 1.0f / e) - 1.0f) * minHalf;
}

/// @brief 半径が a の ra から b の rb へ変わる筒
[[nodiscard]] inline float sdTaperedCapsule(const Vec3& p, const Vec3& a, const Vec3& b, float ra, float rb) noexcept
{
	const Vec3 pa = p - a, ba = b - a;
	const float bb = ba.dot(ba);
	const float h = clamp01(pa.dot(ba) / (bb > 1e-9f ? bb : 1e-9f));
	return length(pa - ba * h) - (ra + (rb - ra) * h);
}

/// @brief 楕円体の距離の近似 (Quilez)。k0 (k0 - 1) / k1
[[nodiscard]] inline float sdEllipsoid(const Vec3& p, const Vec3& r) noexcept
{
	const float k0 = length(Vec3{p.x / r.x, p.y / r.y, p.z / r.z});
	const float k1 = length(Vec3{p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z)});
	return k0 * (k0 - 1.0f) / std::fmax(k1, 1e-6f);
}

/// @brief 体から押し出す形 (仮足・手足)。器に足すと粒が入り、面に足すと描く網に出る
struct FluidShape
{
	enum class Kind : std::uint8_t { Capsule, Ellipsoid, SuperEllipsoid };
	/// World はワールド、Body は体 (傾き・上下に付く)、Ground は足元 (傾かない) の座標系
	enum class Space : std::uint8_t { World, Body, Ground };

	Kind kind = Kind::Capsule;
	Space space = Space::World;
	std::uint8_t pad[2]{};
	Vec3 a{};                       ///< 筒の根元、または楕円体の中心
	Vec3 b{};                       ///< 筒の先
	float ra = 0.1f, rb = 0.1f;     ///< 筒の半径
	Basis axes{};                   ///< 楕円体の向き (space の座標系で)
	Vec3 half{0.1f, 0.1f, 0.1f};
	float exponent = 2.0f;          ///< SuperEllipsoid の指数
	float fillet = 0.06f;           ///< 器へ混ぜる幅 (m)。面では FluidParams::limbFillet を使う
};

[[nodiscard]] inline float fluidShapeDistance(const FluidShape& w, const Vec3& p) noexcept
{
	if (w.kind == FluidShape::Kind::Capsule) { return sdTaperedCapsule(p, w.a, w.b, w.ra, w.rb); }
	const Vec3 l = toLocal(w.axes, p - w.a);
	if (w.kind == FluidShape::Kind::Ellipsoid) { return sdEllipsoid(l, w.half); }
	return sdSuperEllipsoid(l, w.half, w.exponent);
}

/// @brief 形を囲む箱 (ワールドの形だけ)
inline void fluidShapeBounds(const FluidShape& w, Vec3& lo, Vec3& hi) noexcept
{
	if (w.kind == FluidShape::Kind::Capsule)
	{
		const float r = std::fmax(w.ra, w.rb);
		lo = Vec3{std::fmin(w.a.x, w.b.x), std::fmin(w.a.y, w.b.y), std::fmin(w.a.z, w.b.z)} - Vec3{r, r, r};
		hi = Vec3{std::fmax(w.a.x, w.b.x), std::fmax(w.a.y, w.b.y), std::fmax(w.a.z, w.b.z)} + Vec3{r, r, r};
		return;
	}
	const float r = std::fmax(w.half.x, std::fmax(w.half.y, w.half.z));
	lo = w.a - Vec3{r, r, r};
	hi = w.a + Vec3{r, r, r};
}

} // namespace mitiru::softbody