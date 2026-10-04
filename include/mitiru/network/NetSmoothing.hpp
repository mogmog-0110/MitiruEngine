#pragma once

/// @file NetSmoothing.hpp
/// @brief オンライン予測を修正した後、描画位置と向きを修正前から修正後へ数フレームかけて戻す。
///
/// 描画だけに使い、GameMemory には書き込まないため、巻き戻し、リプレイ、checksum は変わらない。
/// 描画位置は、現在位置に「重み ×（修正前の位置 − 修正後の位置）」を組ごとに足して求める。
/// 重みは修正したフレームの 1 から、臨界減衰 (1 + t/τ)·e^(−t/τ) で 0 へ下がる。
/// 修正したフレームは修正前の位置に描かれ、戻る間も速さが急に変わらない。
/// 差が snapDistance を超える組は足さず、瞬間移動、復活、場面の切り替えをすぐに反映する。
/// 状態は持たず、毎フレーム NetCorrectionsView の組から作り直す。
/// static と GameMemory への書き込みは使わない。
/// カメラにも同じ関数を使い、目と注視点を状態から求める式を渡す。
/// キャラだけを戻すと、追従カメラが修正したフレームに急に移動する。

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <sgc/math/Quaternion.hpp>
#include <sgc/math/Vec3.hpp>

#include <mitiru/network/NetCorrections.hpp>

namespace mitiru::network
{

struct SmoothingParams
{
	float timeConstantSec = 0.05f;   ///< 臨界減衰の時定数。4 倍の時間で 9 割戻る。0 は戻さずに飛ぶ
	float snapDistance    = 3.0f;    ///< 修正量がこれより大きい組は戻さずに飛ぶ (m)
	float snapAngleDeg    = 120.0f;  ///< 向きで同じ
	float frameSec        = 1.0f / 60.0f;
};

/// @brief 修正から ageFrames フレーム後の重み。修正したフレームは 1、時定数の 6 倍または kMaxNetCorrections で 0 になる。
[[nodiscard]] inline float correctionWeight(int ageFrames, const SmoothingParams& p) noexcept
{
	const float tau = p.frameSec > 0.0f ? p.timeConstantSec / p.frameSec : 0.0f;
	if (tau <= 0.0f || ageFrames < 0) return 0.0f;
	const int window = (std::min)(kMaxNetCorrections, static_cast<int>(std::ceil(6.0f * tau)));
	if (ageFrames >= window) return 0.0f;
	const auto shape = [tau](float a) { return (1.0f + a / tau) * std::exp(-a / tau); };
	const float tail = shape(static_cast<float>(window));   // 窓の端で 0 に着くように全体をずらす
	return (shape(static_cast<float>(ageFrames)) - tail) / (1.0f - tail);
}

/// @brief 各組のうち、keep(修正前, 修正後) が true のものだけ poseOf の差を重み付きで足す。T は GameMemory の型。
/// @details keep は同じ物を指すかを返す。別の物の差を足すと、その位置へ近づく。
template<class T, class PoseOf, class Keep>
[[nodiscard]] sgc::Vec3f correctionOffsetIf(const NetCorrectionsView& v, PoseOf&& poseOf, Keep&& keep, const SmoothingParams& p = {})
{
	sgc::Vec3f sum{};
	if (v.memorySize != sizeof(T)) return sum;
	const float snap2 = p.snapDistance * p.snapDistance;
	for (std::uint32_t i = 0; i < v.count; ++i)
	{
		const float w = correctionWeight(v.ageFrames[i], p);
		if (w <= 0.0f) break;   // 組は新しい順なので、ここから先も 0
		const T& before = *static_cast<const T*>(v.before[i]);
		const T& after = *static_cast<const T*>(v.after[i]);
		if (!keep(before, after)) continue;
		const sgc::Vec3f e = sgc::Vec3f(poseOf(before)) - sgc::Vec3f(poseOf(after));
		if (e.lengthSquared() > snap2) continue;
		sum += e * w;
	}
	return sum;
}

template<class T, class PoseOf>
[[nodiscard]] sgc::Vec3f correctionOffset(const NetCorrectionsView& v, PoseOf&& poseOf, const SmoothingParams& p = {})
{
	return correctionOffsetIf<T>(v, poseOf, [](const T&, const T&) { return true; }, p);
}

/// @brief 描画位置。now は描画中の状態 (draw の *this)。
template<class T, class PoseOf>
[[nodiscard]] sgc::Vec3f smoothPosition(const NetCorrectionsView& v, const T& now, PoseOf&& poseOf, const SmoothingParams& p = {})
{
	return sgc::Vec3f(poseOf(now)) + correctionOffset<T>(v, poseOf, p);
}

[[nodiscard]] inline float wrapAngleRad(float a) noexcept
{
	constexpr float kPi = 3.14159265358979323846f;
	a = std::fmod(a + kPi, 2.0f * kPi);
	if (a < 0.0f) a += 2.0f * kPi;
	return a - kPi;
}

/// @brief 描画する 1 軸の角度をラジアンで返す。一周をまたぐ差は短い向きへ回る。
template<class T, class AngleOf>
[[nodiscard]] float smoothAngleRad(const NetCorrectionsView& v, const T& now, AngleOf&& angleOf, const SmoothingParams& p = {})
{
	if (v.memorySize != sizeof(T)) return angleOf(now);
	float sum = 0.0f;
	const float snap = p.snapAngleDeg * 0.01745329251994329577f;
	for (std::uint32_t i = 0; i < v.count; ++i)
	{
		const float w = correctionWeight(v.ageFrames[i], p);
		if (w <= 0.0f) break;
		const float e = wrapAngleRad(angleOf(*static_cast<const T*>(v.before[i])) - angleOf(*static_cast<const T*>(v.after[i])));
		if (std::fabs(e) > snap) continue;
		sum += e * w;
	}
	return wrapAngleRad(angleOf(now) + sum);
}

/// @brief 描画する quaternion。修正前 = 差 × 修正後となる差を、重みの分だけ現在の向きの前から掛ける。
template<class T, class RotOf>
[[nodiscard]] sgc::Quaternionf smoothRotation(const NetCorrectionsView& v, const T& now, RotOf&& rotOf, const SmoothingParams& p = {})
{
	sgc::Quaternionf q = rotOf(now);
	if (v.memorySize != sizeof(T)) return q;
	const float snap = p.snapAngleDeg * 0.01745329251994329577f;
	for (std::uint32_t i = 0; i < v.count; ++i)
	{
		const float w = correctionWeight(v.ageFrames[i], p);
		if (w <= 0.0f) break;
		const sgc::Quaternionf before = rotOf(*static_cast<const T*>(v.before[i]));
		const sgc::Quaternionf after = rotOf(*static_cast<const T*>(v.after[i]));
		const sgc::Quaternionf delta = (before * after.conjugate()).normalized();
		const float angle = 2.0f * std::acos((std::min)(std::fabs(delta.w), 1.0f));
		if (angle > snap) continue;
		q = sgc::Quaternionf::slerp(sgc::Quaternionf::identity(), delta, w) * q;
	}
	return q.normalized();
}

} // namespace mitiru::network
