#pragma once

/// @file ProgressBar.hpp
/// @brief プログレスバー評価ユーティリティ
///
/// プログレスバーの進行率と塗りつぶし矩形を算出する。
/// 入力操作は関与せず、純粋な表示計算のみ行う。
///
/// @code
/// using namespace sgc::ui;
/// auto result = evaluateProgressBar(barBounds, hp, 0.0f, maxHp);
/// // result.ratio で進行率、result.filledRect で塗りつぶし矩形を取得
/// @endcode

#include "sgc/math/Rect.hpp"

namespace sgc::ui
{

/// @brief プログレスバー評価結果
struct ProgressBarResult
{
	float ratio{0.0f};        ///< 進行率 [0, 1]
	float filledWidth{0.0f};  ///< 塗りつぶし幅（ピクセル）
	Rectf filledRect{};       ///< 塗りつぶし部分の矩形
};

/// @brief プログレスバーの進行率と塗りつぶし矩形を算出する
///
/// 値を [minValue, maxValue] の範囲で正規化し、
/// バー矩形に対する塗りつぶし部分を計算する。
/// 範囲外の値は [0, 1] にクランプされる。
///
/// @param bounds プログレスバー全体の矩形
/// @param value 現在の値
/// @param minValue 最小値
/// @param maxValue 最大値
/// @return 進行率・塗りつぶし幅・塗りつぶし矩形
[[nodiscard]] constexpr ProgressBarResult evaluateProgressBar(
	const Rectf& bounds, float value,
	float minValue, float maxValue) noexcept
{
	const float range = maxValue - minValue;
	float ratio = (range > 0.0f) ? ((value - minValue) / range) : 0.0f;
	if (ratio < 0.0f)
	{
		ratio = 0.0f;
	}
	if (ratio > 1.0f)
	{
		ratio = 1.0f;
	}

	const float filledW = bounds.width() * ratio;
	const Rectf filled{bounds.x(), bounds.y(), filledW, bounds.height()};

	ProgressBarResult result;
	result.ratio = ratio;
	result.filledWidth = filledW;
	result.filledRect = filled;
	return result;
}

} // namespace sgc::ui
