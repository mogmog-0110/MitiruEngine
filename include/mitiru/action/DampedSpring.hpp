#pragma once

/// @file DampedSpring.hpp
/// @brief 周波数 (Hz) と減衰比 ζ で決めるばねと、へこみの深さから初速を求める関数 (GAME_REQUESTS #89)。
/// @details ActionMath.hpp の SpringF は臨界減衰 (ζ = 1) の SmoothDamp で、行き過ぎて戻る動き (ζ < 1) を作れない。
///          体のつぶれや手足の振りのように、着地で目標を越えて戻る動きにはこちらを使う。状態は 8 byte の POD で GameMemory に置ける。

#include <cmath>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::action
{

namespace spring_detail
{

inline constexpr double kPiD            = 3.14159265358979323846;
inline constexpr double kCriticalBand   = 1e-6;
inline constexpr int    kMaxNewtonSteps = 40;

/// @brief 変位 a・速度 v から始めた減衰振動の dt 秒後 (目標からのずれ)。解析解なので dt に依らず安定
inline void advanceExact(double w, double z, double dt, double& x, double& v) noexcept
{
	if (z < 0.0) { z = 0.0; }
	if (std::abs(z - 1.0) < kCriticalBand)
	{
		const double e = std::exp(-w * dt);
		const double c = v + w * x;
		x = e * (x + c * dt);
		v = e * (v - w * c * dt);
		return;
	}
	if (z < 1.0)
	{
		const double wd = w * std::sqrt(1.0 - z * z);
		const double e  = std::exp(-z * w * dt);
		const double c  = std::cos(wd * dt);
		const double s  = std::sin(wd * dt);
		const double nx = e * (x * c + (v + z * w * x) / wd * s);
		const double nv = e * (v * c - (z * w * v + w * w * x) / wd * s);
		x = nx;
		v = nv;
		return;
	}
	const double root = std::sqrt(z * z - 1.0);
	const double sp = w * root;
	// 遅いほうの根 z w - sp は大きい ζ で桁落ちするので、積が w² になる性質から求める
	const double em = std::exp(-(w / (z + root)) * dt);
	const double ep = std::exp(-(w * (z + root)) * dt);
	const double ch = 0.5 * (em + ep);
	const double sh = 0.5 * (em - ep) / sp;
	const double nx = ch * x + sh * (v + z * w * x);
	const double nv = ch * v - sh * (z * w * v + w * w * x);
	x = nx;
	v = nv;
}

}  // namespace spring_detail

/// @brief 目標へ向かう減衰ばね。ζ < 1 で目標を行き過ぎて戻り、ζ = 1 で行き過ぎず最短で寄り、ζ > 1 はゆっくり寄る
struct DampedSpring
{
	float value    = 0.0f;
	float velocity = 0.0f;

	constexpr void reset(float v) noexcept { value = v; velocity = 0.0f; }

	/// @brief 解析解で dt 秒進める。dt を何倍にしても、細かく刻んだ結果と同じ軌道に乗る。hz <= 0 は力のない等速、dt <= 0 は何もしない
	float step(float target, float hz, float zeta, float dt) noexcept
	{
		if (!(dt > 0.0f) || !std::isfinite(dt) || !std::isfinite(hz) || !std::isfinite(zeta) || !std::isfinite(target) || !std::isfinite(value) || !std::isfinite(velocity)) { return value; }
		const double w = hz > 0.0f ? 2.0 * spring_detail::kPiD * static_cast<double>(hz) : 0.0;
		double       x = static_cast<double>(value) - static_cast<double>(target);
		double       v = static_cast<double>(velocity);
		if (w <= 0.0)
		{
			x += v * static_cast<double>(dt);
		}
		else
		{
			spring_detail::advanceExact(w, static_cast<double>(zeta), static_cast<double>(dt), x, v);
		}
		value    = static_cast<float>(static_cast<double>(target) + x);
		velocity = static_cast<float>(v);
		return value;
	}

	/// @brief 半陰的 Euler で 1 回進める。式は OOZE DRIVE の `stepSpring` と演算の順まで同じで、同じ入力から同じ bit が出る。
	///        録画の再生を変えたくない側が使う。w * dt が大きいと発散するので、大きい dt が来うる所では step を使う
	float stepSemiImplicit(float target, float hz, float zeta, float dt) noexcept
	{
		const float w = 2.0f * kPi * hz;
		velocity += (w * w * (target - value) - 2.0f * zeta * w * velocity) * dt;
		value += velocity * dt;
		return value;
	}
};

static_assert(std::is_trivially_copyable_v<DampedSpring>);
static_assert(sizeof(DampedSpring) == 8, "GameMemory に置く POD。大きさが動くと保存の配置も動く");

/// @brief kickForLowSampled の刻み。既定は OOZE DRIVE の `kickForLow` の定数 (サンプル 300、二分法 40 回、1.5 半周期、探索 10 w)
struct KickSampling
{
	int   samples       = 300;
	int   iterations    = 40;
	float spanHalfTurns = 1.5f;
	float searchRange   = 10.0f;
};

namespace spring_detail
{

struct Trough
{
	double low;    ///< ウィンドウの中の最小の変位
	double slope;  ///< 初速を 1 動かしたときの low の変わり方 (最小の点での、単位初速への応答)
};

/// @brief 変位 a・初速 v から始めた応答の、t 秒での値と、単位初速への応答 (a = 0, v = 1 の値)
inline void responseAt(double a, double v, double w, double z, double t, double& x, double& unit) noexcept
{
	double xa = a;
	double xv = 0.0;
	double va = 0.0;
	double vv = 1.0;
	advanceExact(w, z, t, xa, va);
	advanceExact(w, z, t, xv, vv);
	x    = xa + v * xv;
	unit = xv;
}

/// @brief ウィンドウ (ζ < 1 は spanHalfTurns 半周期、ζ >= 1 は無限) の中の最小の変位。端 (t = 0、ウィンドウの終わり) と極値を調べる
inline Trough troughOf(double a, double v, double w, double z, double spanHalfTurns) noexcept
{
	Trough best{a, 0.0};
	auto   consider = [&](double t) noexcept
	{
		if (!(t > 0.0)) { return; }
		double x = 0.0;
		double u = 0.0;
		responseAt(a, v, w, z, t, x, u);
		if (x < best.low) { best = {x, u}; }
	};
	if (z < 1.0 - kCriticalBand)
	{
		const double wd = w * std::sqrt(1.0 - z * z);
		const double k  = (z * w * v + w * w * a) / wd;
		double       u0 = std::atan2(v, k);
		if (u0 < 0.0) { u0 += kPiD; }
		const double end = spanHalfTurns * kPiD;
		for (double u = u0; u <= end; u += kPiD) { consider(u / wd); }
		consider(end / wd);
		return best;
	}
	if (best.low > 0.0) { best = {0.0, 0.0}; }
	if (z <= 1.0 + kCriticalBand)
	{
		const double d = w * (v + w * a);
		if (d != 0.0) { consider(v / d); }
		return best;
	}
	const double sp = w * std::sqrt(z * z - 1.0);
	const double k  = (z * w * v + w * w * a) / sp;
	if (k != 0.0 && std::abs(v / k) < 1.0) { consider(std::atanh(v / k) / sp); }
	return best;
}

}  // namespace spring_detail

/// @brief 目標からの変位 e0 から、最初の底が low (負なら目標の下) になる初速 (負)。ばねの式から直に解くので、サンプルの刻みに依らない。
/// @details 初速は [-searchRange * w, 0] の中で探す。e0 が low より下、または初速 0 でも底が low より深いときは 0、
///          searchRange * w でも届かないときは -searchRange * w を返す。ζ >= 1 も扱う (行き過ぎずに 1 回だけ目標を越える底)。
///          stepSemiImplicit の離散化は底をわずかに浅くするので、そちらで進めた底とは dt に応じてずれる
[[nodiscard]] inline float kickForLow(float e0, float low, float hz, float zeta, float searchRange = 10.0f) noexcept
{
	if (!(hz > 0.0f) || !std::isfinite(e0) || !std::isfinite(low) || !(searchRange > 0.0f)) { return 0.0f; }
	const double w = 2.0 * spring_detail::kPiD * static_cast<double>(hz);
	const double z = zeta > 0.0f ? static_cast<double>(zeta) : 0.0;
	const double a = static_cast<double>(e0);
	const double target = static_cast<double>(low);
	constexpr double kSpan = 1.5;
	if (a <= target) { return 0.0f; }
	double hi = 0.0;
	double lo = -static_cast<double>(searchRange) * w;
	if (spring_detail::troughOf(a, hi, w, z, kSpan).low <= target) { return 0.0f; }
	if (spring_detail::troughOf(a, lo, w, z, kSpan).low > target) { return static_cast<float>(lo); }
	double v = 0.5 * (lo + hi);
	for (int i = 0; i < spring_detail::kMaxNewtonSteps; ++i)
	{
		const spring_detail::Trough t = spring_detail::troughOf(a, v, w, z, kSpan);
		const double                g = t.low - target;
		if (std::abs(g) <= 1e-12 * (1.0 + std::abs(target))) { break; }
		if (g < 0.0) { lo = v; } else { hi = v; }
		double next = t.slope > 1e-12 ? v - g / t.slope : 0.5 * (lo + hi);
		if (!(next > lo && next < hi)) { next = 0.5 * (lo + hi); }
		v = next;
	}
	return static_cast<float>(v);
}

/// @brief OOZE DRIVE の `kickForLow` と同じ手順 (ウィンドウの中を samples 点で刻んで底を探し、初速を二分法で絞る)。同じ入力から同じ bit が出る。
/// @details 録画の再生を変えたくない側が使う。新しく作るものは kickForLow のほうが速く、刻みの誤差も無い。
///          ζ >= 1 は元の式が NaN になるので、kickForLow に任せる (ここだけ元の式と違う)
[[nodiscard]] inline float kickForLowSampled(float e0, float low, float hz, float zeta,
                                             const KickSampling& s = {}) noexcept
{
	if (!(hz > 0.0f)) { return 0.0f; }
	if (!(zeta < 1.0f)) { return kickForLow(e0, low, hz, zeta, s.searchRange); }
	const float w = 2.0f * kPi * hz;
	const int   samples  = s.samples < 2 ? 2 : s.samples;
	const auto  firstLow = [&](float v0) noexcept
	{
		const float wd  = w * std::sqrt(1.0f - zeta * zeta);
		const float end = s.spanHalfTurns * kPi / wd;
		float       m   = e0;
		for (int i = 0; i < samples; ++i)
		{
			const float t = end * static_cast<float>(i) / static_cast<float>(samples - 1);
			const float e = std::exp(-zeta * w * t) * (e0 * std::cos(wd * t) + (v0 + zeta * w * e0) / wd * std::sin(wd * t));
			m = minf(m, e);
		}
		return m;
	};
	float lo = -s.searchRange * w;
	float hi = 0.0f;
	for (int i = 0; i < s.iterations; ++i)
	{
		const float mid = 0.5f * (lo + hi);
		if (firstLow(mid) < low) { lo = mid; }
		else { hi = mid; }
	}
	return 0.5f * (lo + hi);
}

}  // namespace mitiru::action
