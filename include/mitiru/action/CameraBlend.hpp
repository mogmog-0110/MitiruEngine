#pragma once

/// @file CameraBlend.hpp
/// @brief 固定 tick の間に描画する三人称カメラの視点を補間する。
/// @details 描画だけで使う。GameMemory には入れず、update で書く prev / cur の組から draw が作る。

#include <cmath>

#include <mitiru/action/CameraRig.hpp>

namespace mitiru::action
{

struct CameraBlendLimits
{
	float maxTargetShift = 3.0f;
	float maxTurnDeg     = 60.0f;
};

namespace camera_blend_detail
{

[[nodiscard]] inline bool isFinite(const CameraView& view) noexcept
{
	return action::isFinite(view.eye) && action::isFinite(view.target) && std::isfinite(view.fovDeg) &&
	       action::isFinite(view.up) && std::isfinite(view.rollDeg);
}

[[nodiscard]] inline Vec3 slerpDirection(const Vec3& from, const Vec3& to, float t) noexcept
{
	const float cosine = clampf(dot(from, to), -1.0f, 1.0f);
	if (cosine > 0.9995f) { return normalizeOr(lerp(from, to, t), to); }

	if (cosine < -0.9995f)
	{
		const Vec3 basis = std::fabs(from.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
		const Vec3 side  = normalizeOr(cross(from, basis), Vec3{0.0f, 0.0f, 1.0f});
		const float angle = kPi * t;
		return from * std::cos(angle) + side * std::sin(angle);
	}

	const float angle = std::acos(cosine);
	const float scale = 1.0f / std::sin(angle);
	return from * (std::sin((1.0f - t) * angle) * scale) + to * (std::sin(t * angle) * scale);
}

} // namespace camera_blend_detail

[[nodiscard]] inline CameraView blendCameraView(const CameraView& prev, const CameraView& cur, float alpha,
                                                const CameraBlendLimits& limits = {}) noexcept
{
	if (alpha >= 1.0f || !camera_blend_detail::isFinite(prev) || !camera_blend_detail::isFinite(cur))
	{
		return cur;
	}
	alpha = saturate(std::isfinite(alpha) ? alpha : 1.0f);

	const Vec3  prevOffset = prev.eye - prev.target;
	const Vec3  curOffset  = cur.eye - cur.target;
	const float prevLength = length(prevOffset);
	const float curLength  = length(curOffset);
	if (!(prevLength > 1e-6f) || !(curLength > 1e-6f)) { return cur; }

	const Vec3 targetDelta = cur.target - prev.target;
	if (length(targetDelta) > limits.maxTargetShift) { return cur; }

	const Vec3  prevDirection = prevOffset * (1.0f / prevLength);
	const Vec3  curDirection  = curOffset * (1.0f / curLength);
	const float turnDeg       = std::acos(clampf(dot(prevDirection, curDirection), -1.0f, 1.0f)) / kDegToRad;
	if (turnDeg > limits.maxTurnDeg) { return cur; }

	const Vec3 direction = camera_blend_detail::slerpDirection(prevDirection, curDirection, alpha);
	CameraView result    = cur;
	result.eye           = cur.target + direction * lerpf(prevLength, curLength, alpha);
	result.fovDeg        = lerpf(prev.fovDeg, cur.fovDeg, alpha);
	result.up            = normalizeOr(lerp(prev.up, cur.up, alpha), cur.up);
	result.rollDeg       = lerpf(prev.rollDeg, cur.rollDeg, alpha);
	return result;
}

} // namespace mitiru::action
