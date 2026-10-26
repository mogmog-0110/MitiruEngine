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

/// @brief 大きさ (winW, winH) のウィンドウを、作業領域 (areaX, areaY, areaW, areaH) の中央に置く矩形。領域より大きければ領域に収まるまで縮める。
///        モニタを移すときの置き場所で、ウィンドウの大きさを保つ (縮めるのは入らない時だけ)
[[nodiscard]] constexpr FitRect centerWindowIn(int areaX, int areaY, int areaW, int areaH, int winW, int winH) noexcept
{
	const int safeAreaW = areaW > 0 ? areaW : 1;
	const int safeAreaH = areaH > 0 ? areaH : 1;
	const int w = winW < 1 ? 1 : (winW > safeAreaW ? safeAreaW : winW);
	const int h = winH < 1 ? 1 : (winH > safeAreaH ? safeAreaH : winH);
	return {areaX + (safeAreaW - w) / 2, areaY + (safeAreaH - h) / 2, w, h, false};
}

} // namespace mitiru::platform
