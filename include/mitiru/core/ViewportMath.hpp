#pragma once

/// @file ViewportMath.hpp
/// @brief ウィンドウと logical の大きさから、2D viewport の矩形と初期ウィンドウサイズを求める関数。
/// @details window と device に依存しない。Engine::onWindowResize と Engine::initialize から呼ばれる。

#include <algorithm>
#include <cmath>
#include <limits>

namespace mitiru::detail
{
struct KeepViewportRect
{
	float width;
	float height;
	float offsetX;
	float offsetY;
};

// ResizeMode::Keep 用: aspect 比を保った内接矩形 (letterbox/pillarbox) を計算する。
// Engine::onWindowResize から切り出し済み（window/device なしで単体テストできるようにするため）。
[[nodiscard]] inline KeepViewportRect computeKeepViewportRect(
	int windowW, int windowH, int logicalW, int logicalH) noexcept
{
	KeepViewportRect rect{
		static_cast<float>(windowW), static_cast<float>(windowH), 0.0f, 0.0f};
	if (windowW <= 0 || windowH <= 0 || logicalW <= 0 || logicalH <= 0)
	{
		return rect;
	}
	const float logicalAspect = static_cast<float>(logicalW) / static_cast<float>(logicalH);
	const float windowAspect  = static_cast<float>(windowW) / static_cast<float>(windowH);
	if (windowAspect > logicalAspect)
	{
		// window の方が横長 => 高さ基準で内接、左右に pillarbox
		rect.height  = static_cast<float>(windowH);
		rect.width   = rect.height * logicalAspect;
		rect.offsetX = 0.5f * (static_cast<float>(windowW) - rect.width);
	}
	else
	{
		// window の方が縦長 (または同じ) => 幅基準で内接、上下に letterbox
		rect.width   = static_cast<float>(windowW);
		rect.height  = rect.width / logicalAspect;
		rect.offsetY = 0.5f * (static_cast<float>(windowH) - rect.height);
	}
	return rect;
}

// ResizeMode::Integer 用。logical の整数倍の矩形を中央に置き、余りは背景色のまま残す。
// 余白を整数ピクセルにして、拡大したドットの境界がにじむ 0.5 ピクセルのずれを防ぐ。
[[nodiscard]] inline KeepViewportRect computeIntegerViewportRect(
	int windowW, int windowH, int logicalW, int logicalH) noexcept
{
	if (windowW <= 0 || windowH <= 0 || logicalW <= 0 || logicalH <= 0)
	{
		return computeKeepViewportRect(windowW, windowH, logicalW, logicalH);
	}
	const int scale = (std::min)(windowW / logicalW, windowH / logicalH);
	if (scale < 1)
	{
		// ウィンドウが logical より小さいと整数倍では表示が切れるため、Keep と同じ内接矩形にする。
		return computeKeepViewportRect(windowW, windowH, logicalW, logicalH);
	}
	const int width = logicalW * scale;
	const int height = logicalH * scale;
	return KeepViewportRect{
		static_cast<float>(width), static_cast<float>(height),
		static_cast<float>((windowW - width) / 2), static_cast<float>((windowH - height) / 2)};
}

struct FitWindowSize
{
	int width;
	int height;
	int scale;
};

// EngineConfig::windowFitScreenRatio 用。base の整数倍から、画面の ratio 倍に収まる最大サイズを求める。最低は 1 倍。
// Pyxel の display_scale の既定と同じ式で、切り捨てて求める。
[[nodiscard]] inline FitWindowSize computeFitWindowSize(
	int baseW, int baseH, int screenW, int screenH, float ratio) noexcept
{
	FitWindowSize fit{baseW, baseH, 1};
	if (baseW <= 0 || baseH <= 0 || screenW <= 0 || screenH <= 0 || !(ratio > 0.0f))
	{
		return fit;
	}
	const float byWidth = static_cast<float>(screenW) * ratio / static_cast<float>(baseW);
	const float byHeight = static_cast<float>(screenH) * ratio / static_cast<float>(baseH);
	// 4096 倍までにして、float から int への変換と、base との掛け算が溢れないようにする
	const float limit = (std::min)(4096.0f,
		static_cast<float>((std::numeric_limits<int>::max)() / 2 / (std::max)(baseW, baseH)));
	const float scale = (std::min)((std::min)(byWidth, byHeight), limit);
	if (!std::isfinite(scale) || scale < 2.0f)
	{
		return fit;
	}
	fit.scale = static_cast<int>(scale);
	fit.width = baseW * fit.scale;
	fit.height = baseH * fit.scale;
	return fit;
}
}  // namespace mitiru::detail
