#pragma once

/// @file FilmSampling.hpp
/// @brief オフライン描画の時間・画面・レンズの標本。1 コマを N 回描いて平均するための、乱数を使わず添字だけで決まる配置。

#include <cmath>
#include <cstdint>

namespace mitiru::film
{

struct SamplingPlan
{
	int   subframes = 1;      ///< 1 コマあたりの標本数 N (1 = 蓄積なし)
	float shutter = 0.5f;     ///< シャッターが開く長さ (コマの間隔に対する割合。0.5 = 180 度)
	float fps = 60.0f;        ///< 出力のコマ数 / 秒
};

/// @brief Halton 列の index 番目を返す。index は 0 始まり、base は素数。同じ添字からは同じ値を返す。
[[nodiscard]] inline float halton(std::uint32_t index, std::uint32_t base) noexcept
{
	float f = 1.0f, r = 0.0f;
	for (std::uint32_t i = index + 1; i > 0; i /= base)
	{
		f /= static_cast<float>(base);
		r += f * static_cast<float>(i % base);
	}
	return r;
}

/// @brief 2 つの一様な値を、同心円写像で単位円板上の点に変換する。ボケの形は円になる。
inline void concentricDisk(float u, float v, float& x, float& y) noexcept
{
	const float a = 2.0f * u - 1.0f, b = 2.0f * v - 1.0f;
	if (a == 0.0f && b == 0.0f) { x = 0.0f; y = 0.0f; return; }
	const float pi4 = 0.78539816f;
	float r, phi;
	if (std::fabs(a) > std::fabs(b)) { r = a; phi = pi4 * (b / a); }
	else { r = b; phi = 1.5707963f - pi4 * (a / b); }
	x = r * std::cos(phi);
	y = r * std::sin(phi);
}

struct Dir3 { float x, y, z; };

/// @brief 光の進む向き d (長さは保つ) を、d を軸にした半径 radiusDeg の円錐の中の (diskX, diskY) (単位円板上) へ振る。
///        radiusDeg が 0 以下なら d のまま。太陽の円盤の中の点を選ぶと、標本の平均が距離で広がる半影になる (ADR 0150)
[[nodiscard]] inline Dir3 swingDirection(Dir3 d, float radiusDeg, float diskX, float diskY) noexcept
{
	if (!(radiusDeg > 0.0f)) { return d; }
	const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if (!(len > 1e-6f)) { return d; }
	const Dir3 n{d.x / len, d.y / len, d.z / len};
	const Dir3 h = std::fabs(n.y) < 0.99f ? Dir3{0.0f, 1.0f, 0.0f} : Dir3{1.0f, 0.0f, 0.0f};
	Dir3 u{n.y * h.z - n.z * h.y, n.z * h.x - n.x * h.z, n.x * h.y - n.y * h.x};
	const float ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
	u = {u.x / ul, u.y / ul, u.z / ul};
	const Dir3 v{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};
	const float r = std::tan(radiusDeg * 0.017453292f);
	const Dir3 m{n.x + (u.x * diskX + v.x * diskY) * r, n.y + (u.y * diskX + v.y * diskY) * r, n.z + (u.z * diskX + v.z * diskY) * r};
	const float ml = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
	return {m.x / ml * len, m.y / ml * len, m.z / ml * len};
}

/// @brief ホスト側の k 番目の描画が、出力の何コマ目の何番目の標本かを表す。k は 0 始まり。
struct SampleIndex
{
	std::uint32_t outputFrame = 0;
	std::uint32_t sample = 0;
};

[[nodiscard]] inline SampleIndex sampleIndexOf(std::uint32_t hostFrame, const SamplingPlan& plan) noexcept
{
	const std::uint32_t n = plan.subframes > 0 ? static_cast<std::uint32_t>(plan.subframes) : 1u;
	return {hostFrame / n, hostFrame % n};
}

/// @brief 標本の時刻を秒で返す。コマの時刻 F / fps を中心に、シャッターが開く間を N 等分した各区間の中央を使う。N = 1 ではコマの時刻を使う。
[[nodiscard]] inline float sampleTime(const SampleIndex& s, const SamplingPlan& plan) noexcept
{
	const float n = static_cast<float>(plan.subframes > 0 ? plan.subframes : 1);
	const float offset = plan.subframes > 1 ? plan.shutter * ((static_cast<float>(s.sample) + 0.5f) / n - 0.5f) : 0.0f;
	return (static_cast<float>(s.outputFrame) + offset) / plan.fps;
}

/// @brief 標本ごとの画面とレンズのずれ。jitter は画素単位で ±0.5、aperture と sun は単位円板上の点 (sun は太陽の円盤の中の位置)。
struct SampleOffsets
{
	float jitterX = 0.0f, jitterY = 0.0f;
	float apertureX = 0.0f, apertureY = 0.0f;
	float sunX = 0.0f, sunY = 0.0f;
};

[[nodiscard]] inline SampleOffsets offsetsOf(const SampleIndex& s, const SamplingPlan& plan) noexcept
{
	SampleOffsets o;
	if (plan.subframes <= 1) { return o; }
	o.jitterX = halton(s.sample, 2) - 0.5f;
	o.jitterY = halton(s.sample, 3) - 0.5f;
	concentricDisk(halton(s.sample, 5), halton(s.sample, 7), o.apertureX, o.apertureY);
	// 太陽の円盤は Halton ではなく標本の数 N を使う Fibonacci 円板にする。Halton(11, 13) は N が 11 未満の間 2 つの値の比が一定で、
	// 点が 1 本の直線に並び、半影が円でなく弧になる
	const float n = static_cast<float>(plan.subframes);
	const float radius = std::sqrt((static_cast<float>(s.sample) + 0.5f) / n);
	const float angle = static_cast<float>(s.sample) * 2.3999632f;
	o.sunX = radius * std::cos(angle);
	o.sunY = radius * std::sin(angle);
	return o;
}

}  // namespace mitiru::film
