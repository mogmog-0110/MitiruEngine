#pragma once

#include <cstdint>

namespace mitiru::platform
{

struct FitRect
{
	int x;
	int y;
	int w;
	int h;
	bool letterboxed;

	[[nodiscard]] constexpr bool operator==(const FitRect&) const noexcept = default;
};

[[nodiscard]] constexpr FitRect fitRectToAspect(
	int monX, int monY, int monW, int monH, int aspectMilli) noexcept
{
	const int safeW = monW > 0 ? monW : 1;
	const int safeH = monH > 0 ? monH : 1;
	if (aspectMilli <= 0)
	{
		return {monX, monY, safeW, safeH, false};
	}

	constexpr std::int64_t kAspectScale = 1000;
	constexpr std::int64_t kToleranceDenominator = 200; // ±0.5%
	const std::int64_t monitorScaled =
		static_cast<std::int64_t>(safeW) * kAspectScale;
	const std::int64_t targetScaled =
		static_cast<std::int64_t>(safeH) * aspectMilli;
	const std::int64_t difference =
		monitorScaled > targetScaled
			? monitorScaled - targetScaled
			: targetScaled - monitorScaled;
	if (difference * kToleranceDenominator <= targetScaled)
	{
		return {monX, monY, safeW, safeH, false};
	}

	if (monitorScaled > targetScaled)
	{
		const std::int64_t rounded =
			(static_cast<std::int64_t>(safeH) * aspectMilli + kAspectScale / 2)
			/ kAspectScale;
		const int w = rounded < 1 ? 1 : (rounded > safeW ? safeW : static_cast<int>(rounded));
		return {monX + (safeW - w) / 2, monY, w, safeH, true};
	}

	const std::int64_t rounded =
		(static_cast<std::int64_t>(safeW) * kAspectScale + aspectMilli / 2)
		/ aspectMilli;
	const int h = rounded < 1 ? 1 : (rounded > safeH ? safeH : static_cast<int>(rounded));
	return {monX, monY + (safeH - h) / 2, safeW, h, true};
}

} // namespace mitiru::platform
