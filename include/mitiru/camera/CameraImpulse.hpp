#pragma once

/// @file CameraImpulse.hpp
/// @brief 被弾・爆発などの衝撃でカメラを揺らす。揺れは「いつ・どの強さ・どの seed」の純関数で、乱数を使わない。
/// @details GameMemory に置くのは CameraImpulseSet (発生の時刻と強さと seed だけ) で、揺れの量は毎フレーム時刻から求める。
///          利用者のアクセシビリティ設定の倍率 (Screen::cameraShakeScale()) は PC ごとに違う値なので、update では読まず、
///          draw で applyCameraShake の scale に渡す (update で読んで GameMemory に書くと、PC ごとに進みが食い違う)。
///          減衰は強さの 2 乗 (Vlambeer の trauma 方式。effects/ScreenShake と同じ考え方で、こちらは時刻の関数)。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <mitiru/action/CameraRig.hpp>
#include <mitiru/camera/CameraPriority.hpp>

namespace mitiru::camera
{

using action::CameraView;
using action::Vec3;

struct CameraImpulse
{
	Vec3          dir{};            ///< 衝撃の向き (視点から見て x = 右、y = 上、z = 奥)。0 なら方向なしの揺れ
	float         startSec = 0.0f;  ///< 発生した時刻 (GameMemory の秒)
	float         duration = 0.0f;  ///< 消えるまでの秒。0 で空き
	float         strength = 0.0f;  ///< 0..1
	std::uint32_t seed = 0;
};

template <int N = 4>
struct CameraImpulseSet
{
	static_assert(N > 0 && N <= 255);
	CameraImpulse items[N]{};
	std::uint8_t  next = 0;   ///< 次に書く場所。満杯なら一番古いものを上書きする
	std::uint8_t  _pad[3] = {};

	void add(float nowSec, float strength, const Vec3& dir = {}, float duration = 0.5f, std::uint32_t seed = 0) noexcept
	{
		CameraImpulse& slot = items[next];
		slot = CameraImpulse{dir, nowSec, duration > 0.0f ? duration : 0.0f, action::saturate(strength), seed};
		next = static_cast<std::uint8_t>((next + 1) % N);
	}
};

/// @brief 揺れの大きさ。ゲームごとに決める定数 (GameMemory には置かない)
struct CameraShakeShape
{
	float maxOffset = 0.12f;      ///< 強さ 1 のときの位置のずれ (m)
	float maxAngleDeg = 1.5f;     ///< 視線の向きのずれ (度)
	float maxRollDeg = 2.0f;      ///< 傾きのずれ (度)
	float frequency = 24.0f;      ///< 1 秒に乱数の目を替える回数
};

struct CameraShake
{
	Vec3  offset{};   ///< 視点から見た位置のずれ (x = 右、y = 上、z = 奥)
	float yawDeg = 0.0f;
	float pitchDeg = 0.0f;
	float rollDeg = 0.0f;
};

namespace impulse_detail
{

[[nodiscard]] constexpr float hashSigned(std::uint32_t seed, std::uint32_t channel, std::uint32_t index) noexcept
{
	std::uint32_t h = seed * 0x9E3779B1u ^ (channel + 1u) * 0x85EBCA6Bu ^ (index + 1u) * 0xC2B2AE35u;
	h ^= h >> 16; h *= 0x45D9F3Bu; h ^= h >> 16; h *= 0x45D9F3Bu; h ^= h >> 16;
	return static_cast<float>(h & 0xFFFFu) / 32767.5f - 1.0f;
}

/// @brief 目の間をなめらかにつないだ値の雑音 [-1, 1]。x は 目の番号 + 小数
[[nodiscard]] inline float noise(std::uint32_t seed, std::uint32_t channel, float x) noexcept
{
	const float        fl = std::floor(x);
	const std::uint32_t i = static_cast<std::uint32_t>(static_cast<std::int32_t>(fl));
	const float        u = smoothstep01(x - fl);
	return hashSigned(seed, channel, i) * (1.0f - u) + hashSigned(seed, channel, i + 1u) * u;
}

[[nodiscard]] inline Vec3 rotateAbout(const Vec3& v, const Vec3& axis, float rad) noexcept
{
	const float c = std::cos(rad), s = std::sin(rad);
	return v * c + action::cross(axis, v) * s + axis * (action::dot(axis, v) * (1.0f - c));
}

}  // namespace impulse_detail

/// @brief 時刻 nowSec の揺れ。範囲外 (発生前・消えた後) のものは足さない
template <int N>
[[nodiscard]] CameraShake sampleCameraShake(const CameraImpulseSet<N>& set, float nowSec, const CameraShakeShape& shape = {}) noexcept
{
	CameraShake out;
	for (const CameraImpulse& im : set.items)
	{
		const float age = nowSec - im.startSec;
		if (im.duration <= 0.0f || age < 0.0f || age >= im.duration) { continue; }
		const float left = 1.0f - age / im.duration;
		const float amp = im.strength * left * left;
		const float x = age * shape.frequency;
		auto n = [&](std::uint32_t ch) { return impulse_detail::noise(im.seed, ch, x); };
		const float pos = amp * shape.maxOffset;
		out.offset = out.offset + im.dir * (pos * n(0)) + Vec3{n(1), n(2), 0.0f} * (pos * 0.5f);
		out.yawDeg += amp * shape.maxAngleDeg * n(3);
		out.pitchDeg += amp * shape.maxAngleDeg * n(4);
		out.rollDeg += amp * shape.maxRollDeg * n(5);
	}
	return out;
}

/// @brief view に揺れを足す。scale は Screen::cameraShakeScale()。0 以下なら view をそのまま返す
[[nodiscard]] inline CameraView applyCameraShake(const CameraView& view, const CameraShake& shake, float scale) noexcept
{
	if (!(scale > 0.0f)) { return view; }
	namespace id = impulse_detail;
	const Vec3 f = action::normalizeOr(view.target - view.eye, Vec3{0.0f, 0.0f, 1.0f});
	const Vec3 r = action::normalizeOr(action::cross(f, view.up), action::anyPerpendicular(f));
	const Vec3 u = action::cross(r, f);
	const float k = action::kDegToRad * scale;
	const Vec3 move = r * shake.offset.x + u * shake.offset.y + f * shake.offset.z;
	const Vec3 r1 = id::rotateAbout(r, u, shake.yawDeg * k);   // pitch は yaw で回った後の右を軸にする
	const Vec3 f1 = id::rotateAbout(id::rotateAbout(f, u, shake.yawDeg * k), r1, shake.pitchDeg * k);
	const Vec3 u1 = id::rotateAbout(action::cross(r1, f1), f1, shake.rollDeg * k);
	CameraView out = view;
	out.eye = view.eye + move * scale;
	out.target = out.eye + f1 * action::maxf(action::length(view.target - view.eye), 1e-3f);
	out.up = u1;
	out.rollDeg = action::cameraRollDeg(out.eye, out.target, out.up);
	return out;
}

}  // namespace mitiru::camera
