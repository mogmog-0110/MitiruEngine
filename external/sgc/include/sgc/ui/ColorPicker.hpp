#pragma once

/// @file ColorPicker.hpp
/// @brief カラーピッカー評価ユーティリティ
///
/// HSVカラーピッカーの状態を評価する。
/// SV領域、Hueスライダー、Alphaスライダーのドラッグ操作を判定する。
///
/// @code
/// using namespace sgc::ui;
/// ColorPickerConfig config{bounds, hue, sat, val, alpha, true, false, false, false};
/// auto result = evaluateColorPicker(config, mousePos, mouseDown, mousePressed, mouseReleased);
/// if (result.changed) { hue = result.hue; sat = result.saturation; val = result.value; }
/// @endcode

#include <cmath>

#include "sgc/types/Color.hpp"
#include "sgc/ui/WidgetState.hpp"

namespace sgc::ui
{

/// @brief Hueスライダーの幅
inline constexpr float HUE_BAR_WIDTH = 20.0f;

/// @brief Alphaスライダーの幅
inline constexpr float ALPHA_BAR_WIDTH = 20.0f;

/// @brief バー間のマージン
inline constexpr float BAR_MARGIN = 8.0f;

/// @brief プレビュー領域の高さ
inline constexpr float PREVIEW_HEIGHT = 30.0f;

/// @brief カラーピッカー設定
struct ColorPickerConfig
{
	Rectf bounds;             ///< カラーピッカー全体のバウンズ
	float hue{0.0f};          ///< 色相 [0, 360]
	float saturation{1.0f};   ///< 彩度 [0, 1]
	float value{1.0f};        ///< 明度 [0, 1]
	float alpha{1.0f};        ///< アルファ [0, 1]
	bool showAlpha{true};     ///< アルファスライダー表示
	bool isDraggingSV{false}; ///< SV領域ドラッグ中
	bool isDraggingHue{false};   ///< Hueスライダードラッグ中
	bool isDraggingAlpha{false}; ///< Alphaスライダードラッグ中
};

/// @brief カラーピッカー評価結果
struct ColorPickerResult
{
	WidgetState state{WidgetState::Normal};  ///< ウィジェットの視覚状態
	float hue{0.0f};                  ///< 色相
	float saturation{1.0f};           ///< 彩度
	float value{1.0f};                ///< 明度
	float alpha{1.0f};                ///< アルファ
	bool isDraggingSV{false};         ///< SV領域ドラッグ中
	bool isDraggingHue{false};        ///< Hueスライダードラッグ中
	bool isDraggingAlpha{false};      ///< Alphaスライダードラッグ中
	bool changed{false};              ///< 値が変化したか
	Rectf svAreaBounds;               ///< SV選択エリア
	Rectf hueBarBounds;               ///< Hueスライダーバウンズ
	Rectf alphaBarBounds;             ///< Alphaスライダーバウンズ
	Rectf previewBounds;              ///< プレビュー色表示エリア
};

/// @brief 値を指定範囲にクランプする
/// @param val 値
/// @param lo 最小値
/// @param hi 最大値
/// @return クランプされた値
[[nodiscard]] constexpr float clampFloat(float val, float lo, float hi) noexcept
{
	if (val < lo) return lo;
	if (val > hi) return hi;
	return val;
}

/// @brief カラーピッカーの状態を評価する
///
/// SV領域、Hueスライダー、Alphaスライダーのドラッグ操作を判定し、
/// HSV値を更新する。
///
/// @param config カラーピッカーの設定
/// @param mousePos マウス座標
/// @param mouseDown マウスボタンが押下中か
/// @param mousePressed マウスボタンがこのフレームで押されたか
/// @param mouseReleased マウスボタンがこのフレームで離されたか
/// @return カラーピッカーの評価結果
[[nodiscard]] constexpr ColorPickerResult evaluateColorPicker(
	const ColorPickerConfig& config, const Vec2f& mousePos,
	bool mouseDown, bool mousePressed,
	bool mouseReleased) noexcept
{
	// レイアウト計算
	const float totalBarWidth = HUE_BAR_WIDTH + BAR_MARGIN +
		(config.showAlpha ? (ALPHA_BAR_WIDTH + BAR_MARGIN) : 0.0f);
	const float svWidth = config.bounds.width() - totalBarWidth;
	const float svHeight = config.bounds.height() - PREVIEW_HEIGHT - BAR_MARGIN;

	const Rectf svArea{
		config.bounds.position,
		{svWidth, svHeight}
	};
	const Rectf hueBar{
		{config.bounds.x() + svWidth + BAR_MARGIN, config.bounds.y()},
		{HUE_BAR_WIDTH, svHeight}
	};
	const Rectf alphaBar{
		{hueBar.x() + HUE_BAR_WIDTH + BAR_MARGIN, config.bounds.y()},
		{ALPHA_BAR_WIDTH, svHeight}
	};
	const Rectf previewArea{
		{config.bounds.x(), config.bounds.y() + svHeight + BAR_MARGIN},
		{config.bounds.width(), PREVIEW_HEIGHT}
	};

	float h = config.hue;
	float s = config.saturation;
	float v = config.value;
	float a = config.alpha;
	bool draggingSV = config.isDraggingSV;
	bool draggingHue = config.isDraggingHue;
	bool draggingAlpha = config.isDraggingAlpha;

	// ドラッグ開始
	if (mousePressed)
	{
		if (isMouseOver(mousePos, svArea))
		{
			draggingSV = true;
		}
		else if (isMouseOver(mousePos, hueBar))
		{
			draggingHue = true;
		}
		else if (config.showAlpha && isMouseOver(mousePos, alphaBar))
		{
			draggingAlpha = true;
		}
	}

	// ドラッグ解放
	if (mouseReleased)
	{
		draggingSV = false;
		draggingHue = false;
		draggingAlpha = false;
	}

	// ドラッグ中の値更新
	if (draggingSV && mouseDown)
	{
		const float relX = mousePos.x - svArea.x();
		const float relY = mousePos.y - svArea.y();
		s = clampFloat(relX / svArea.width(), 0.0f, 1.0f);
		v = clampFloat(1.0f - relY / svArea.height(), 0.0f, 1.0f);
	}
	if (draggingHue && mouseDown)
	{
		const float relY = mousePos.y - hueBar.y();
		h = clampFloat(relY / hueBar.height() * 360.0f, 0.0f, 360.0f);
	}
	if (draggingAlpha && mouseDown)
	{
		const float relY = mousePos.y - alphaBar.y();
		a = clampFloat(1.0f - relY / alphaBar.height(), 0.0f, 1.0f);
	}

	const bool changed = (h != config.hue || s != config.saturation ||
	                       v != config.value || a != config.alpha);

	const bool hovered = isMouseOver(mousePos, config.bounds);
	const bool pressed = mouseDown && (draggingSV || draggingHue || draggingAlpha || hovered);
	const auto state = resolveWidgetState(true, hovered, pressed);

	return {state, h, s, v, a, draggingSV, draggingHue, draggingAlpha,
	        changed, svArea, hueBar, alphaBar, previewArea};
}

/// @brief HSV色をRGB色に変換する
///
/// @param h 色相 [0, 360]
/// @param s 彩度 [0, 1]
/// @param v 明度 [0, 1]
/// @param a アルファ [0, 1]
/// @return RGB色
[[nodiscard]] constexpr Colorf hsvToRgb(float h, float s, float v, float a = 1.0f) noexcept
{
	if (s <= 0.0f)
	{
		return Colorf{v, v, v, a};
	}

	float hh = h;
	if (hh >= 360.0f) hh = 0.0f;
	hh /= 60.0f;

	const int32_t i = static_cast<int32_t>(hh);
	const float ff = hh - static_cast<float>(i);
	const float p = v * (1.0f - s);
	const float q = v * (1.0f - s * ff);
	const float t = v * (1.0f - s * (1.0f - ff));

	switch (i)
	{
	case 0:  return Colorf{v, t, p, a};
	case 1:  return Colorf{q, v, p, a};
	case 2:  return Colorf{p, v, t, a};
	case 3:  return Colorf{p, q, v, a};
	case 4:  return Colorf{t, p, v, a};
	default: return Colorf{v, p, q, a};
	}
}

/// @brief RGB色をHSV色に変換する
///
/// @param color RGB色
/// @param[out] h 色相 [0, 360]
/// @param[out] s 彩度 [0, 1]
/// @param[out] v 明度 [0, 1]
constexpr void rgbToHsv(const Colorf& color, float& h, float& s, float& v) noexcept
{
	const float maxC = (color.r > color.g)
		? ((color.r > color.b) ? color.r : color.b)
		: ((color.g > color.b) ? color.g : color.b);
	const float minC = (color.r < color.g)
		? ((color.r < color.b) ? color.r : color.b)
		: ((color.g < color.b) ? color.g : color.b);
	const float delta = maxC - minC;

	v = maxC;
	s = (maxC > 0.0f) ? (delta / maxC) : 0.0f;

	if (delta <= 0.0f)
	{
		h = 0.0f;
		return;
	}

	if (maxC == color.r)
	{
		h = 60.0f * ((color.g - color.b) / delta);
	}
	else if (maxC == color.g)
	{
		h = 60.0f * (2.0f + (color.b - color.r) / delta);
	}
	else
	{
		h = 60.0f * (4.0f + (color.r - color.g) / delta);
	}

	if (h < 0.0f)
	{
		h += 360.0f;
	}
}

} // namespace sgc::ui
