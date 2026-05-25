#pragma once

/// @file UIColorPicker.hpp
/// @brief 色選択 widget。HSV モデル、hue bar、alpha bar、preset swatch を備える。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief RGBA 色値 (各 channel 0.0 - 1.0)。
struct UIColorRGBA
{
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

/// @brief HSV 色値 (H: 0-360, S: 0-1, V: 0-1)。
struct UIColorHSV
{
	float h = 0.0f;   ///< 色相 (度、0-360)。
	float s = 1.0f;   ///< 彩度 (0-1)。
	float v = 1.0f;   ///< 明度 (0-1)。
};

/// @brief preset 色エントリ。
struct UIColorPreset
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 1.0f;
};

/// @brief color picker のどの component を操作中か。
enum class ColorPickerInteraction : std::uint8_t
{
	None,
	SaturationValue,
	HueBar,
	AlphaBar,
	Eyedropper
};

/// @brief UIColorPicker 生成用の設定。
struct UIColorPickerConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float width = 260.0f;                  ///< widget 全体の幅。
	float height = 300.0f;                 ///< widget 全体の高さ。
	bool showAlpha = true;                 ///< alpha channel bar を表示する。
	bool showHex = true;                   ///< hex 色入力を表示する。
	bool showRGB = true;                   ///< RGB slider を表示する。
	bool showHSV = false;                  ///< HSV slider を表示する。
	std::vector<UIColorPreset> presetColors;  ///< preset 色の swatch。
	float hueBarWidth = 20.0f;             ///< 垂直 hue bar の幅。
	float alphaBarHeight = 16.0f;          ///< 水平 alpha bar の高さ。
	float previewSize = 40.0f;             ///< 色 preview 四角のサイズ。
	std::string backgroundImageKey;        ///< 背景 image key。
	std::string hueBarImageKey;            ///< hue bar グラデーションの image key。
	float alphaCheckerSize = 8.0f;         ///< alpha checker パターンの cell サイズ。
	float fontSize = 12.0f;                ///< label と hex 入力の font size。
	float labelWidth = 20.0f;              ///< "R:"、"G:"、"B:" label の幅。
};

/// @brief HSV モデル、hue bar、SV box、alpha bar、preset を備えた color picker widget。
///
/// saturation-value の矩形 box と垂直 hue bar を提供する。任意で
/// alpha bar、hex 入力、RGB slider、preset 色の swatch を表示する。
///
/// @code
///   UIColorPickerConfig cfg;
///   cfg.id = 300;
///   cfg.name = "color_picker";
///   cfg.showAlpha = true;
///   cfg.presetColors = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}};
///   UIColorPicker picker(cfg);
///
///   picker.setOnColorChanged([](const UIColorRGBA& c) {
///       // use color
///   });
///   picker.setColor(0.5f, 0.8f, 0.2f, 1.0f);
/// @endcode
class UIColorPicker
{
	std::shared_ptr<UINode> m_node;
	UIColorHSV m_hsv;
	float m_alpha = 1.0f;
	bool m_showAlpha;
	bool m_showHex;
	bool m_showRGB;
	bool m_showHSV;
	float m_hueBarWidth;
	float m_alphaBarHeight;
	std::vector<UIColorPreset> m_presets;
	ColorPickerInteraction m_interaction = ColorPickerInteraction::None;
	bool m_eyedropperMode = false;
	std::function<void(const UIColorRGBA&)> m_onColorChanged;

public:
	/// @brief 設定から color picker を構築する。
	/// @param config color picker 設定。
	explicit UIColorPicker(const UIColorPickerConfig& config)
		: m_showAlpha(config.showAlpha)
		, m_showHex(config.showHex)
		, m_showRGB(config.showRGB)
		, m_showHSV(config.showHSV)
		, m_hueBarWidth(config.hueBarWidth)
		, m_alphaBarHeight(config.alphaBarHeight)
		, m_presets(config.presetColors)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "color_picker";
		data.properties["show_alpha"] = config.showAlpha ? "true" : "false";
		data.properties["show_hex"] = config.showHex ? "true" : "false";
		data.properties["show_rgb"] = config.showRGB ? "true" : "false";
		data.properties["show_hsv"] = config.showHSV ? "true" : "false";
		data.properties["hue_bar_width"] = std::to_string(config.hueBarWidth);
		data.properties["alpha_bar_height"] = std::to_string(config.alphaBarHeight);
		data.properties["preview_size"] = std::to_string(config.previewSize);
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["hue_bar_image"] = config.hueBarImageKey;
		data.properties["alpha_checker_size"] = std::to_string(config.alphaCheckerSize);
		data.properties["font_size"] = std::to_string(config.fontSize);
		data.properties["label_width"] = std::to_string(config.labelWidth);
		data.properties["preset_count"] = std::to_string(config.presetColors.size());

		m_node = std::make_shared<UINode>(std::move(data));

		syncNodeState();
	}

	/// @brief 基となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の色を RGBA で取得する。
	[[nodiscard]] UIColorRGBA getColor() const noexcept
	{
		return hsvToRgb(m_hsv, m_alpha);
	}

	/// @brief 現在の色を HSV で取得する。
	[[nodiscard]] UIColorHSV getHSV() const noexcept { return m_hsv; }

	/// @brief 現在の alpha 値を取得する。
	[[nodiscard]] float getAlpha() const noexcept { return m_alpha; }

	/// @brief 現在の hex 文字列を取得する (例: "#FF8040" や "#FF8040CC")。
	[[nodiscard]] std::string getHex() const
	{
		const auto c = getColor();
		const auto toHex = [](float v) -> std::string {
			const int byte = std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255);
			const char digits[] = "0123456789ABCDEF";
			std::string result;
			result += digits[(byte >> 4) & 0xF];
			result += digits[byte & 0xF];
			return result;
		};

		std::string hex = "#" + toHex(c.r) + toHex(c.g) + toHex(c.b);
		if (m_showAlpha)
		{
			hex += toHex(c.a);
		}
		return hex;
	}

	/// @brief eyedropper モードが有効か判定する。
	[[nodiscard]] bool isEyedropperActive() const noexcept { return m_eyedropperMode; }

	// -- Configuration -------------------------------------------------------

	/// @brief 色変更時の callback を設定する。
	/// @param callback 新しい RGBA 色を引数に呼ばれる関数。
	void setOnColorChanged(std::function<void(const UIColorRGBA&)> callback)
	{
		m_onColorChanged = std::move(callback);
	}

	// -- Color Setters -------------------------------------------------------

	/// @brief RGBA 値から色を設定する。
	/// @param r 赤 (0-1)。
	/// @param g 緑 (0-1)。
	/// @param b 青 (0-1)。
	/// @param a alpha (0-1)。
	void setColor(float r, float g, float b, float a = 1.0f)
	{
		m_hsv = rgbToHsv(r, g, b);
		m_alpha = std::clamp(a, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief HSV 値から色を設定する。
	/// @param h 色相 (0-360)。
	/// @param s 彩度 (0-1)。
	/// @param v 明度 (0-1)。
	void setHSV(float h, float s, float v)
	{
		m_hsv.h = std::fmod(std::max(h, 0.0f), 360.0f);
		m_hsv.s = std::clamp(s, 0.0f, 1.0f);
		m_hsv.v = std::clamp(v, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief alpha 値を設定する。
	/// @param a alpha (0-1)。
	void setAlpha(float a)
	{
		m_alpha = std::clamp(a, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief index 指定で preset 色を選択する。
	/// @param index preset の index。
	void selectPreset(std::size_t index)
	{
		if (index >= m_presets.size()) { return; }
		const auto& p = m_presets[index];
		setColor(p.r, p.g, p.b, p.a);
	}

	// -- Interaction ---------------------------------------------------------

	/// @brief 特定 component との操作を開始する。
	/// @param component 操作対象の component。
	void onInteractionBegin(ColorPickerInteraction component)
	{
		m_interaction = component;
	}

	/// @brief 正規化座標で saturation-value box を更新する。
	/// @param normX 正規化 X (0-1、saturation に対応)。
	/// @param normY 正規化 Y (0-1、value に対応、0 = 上端 = 明るい)。
	void onSaturationValueUpdate(float normX, float normY)
	{
		if (m_interaction != ColorPickerInteraction::SaturationValue) { return; }
		m_hsv.s = std::clamp(normX, 0.0f, 1.0f);
		m_hsv.v = std::clamp(1.0f - normY, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief 正規化位置で hue bar を更新する。
	/// @param normY 正規化 Y (0-1、hue 0-360 に対応)。
	void onHueBarUpdate(float normY)
	{
		if (m_interaction != ColorPickerInteraction::HueBar) { return; }
		m_hsv.h = std::clamp(normY, 0.0f, 1.0f) * 360.0f;
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief 正規化位置で alpha bar を更新する。
	/// @param normX 正規化 X (0-1、alpha に対応)。
	void onAlphaBarUpdate(float normX)
	{
		if (m_interaction != ColorPickerInteraction::AlphaBar) { return; }
		m_alpha = std::clamp(normX, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief 現在の操作を終了する。
	void onInteractionEnd()
	{
		m_interaction = ColorPickerInteraction::None;
	}

	/// @brief eyedropper モードを切り替える。
	void toggleEyedropper()
	{
		m_eyedropperMode = !m_eyedropperMode;
		m_node->setProperty("eyedropper_active", m_eyedropperMode ? "true" : "false");
	}

	/// @brief eyedropper の pick 結果から色を設定する。
	/// @param r 赤 (0-1)。
	/// @param g 緑 (0-1)。
	/// @param b 青 (0-1)。
	void onEyedropperPick(float r, float g, float b)
	{
		m_eyedropperMode = false;
		m_node->setProperty("eyedropper_active", "false");
		setColor(r, g, b, m_alpha);
	}

private:
	/// @brief callback 経由で色変更を通知する。
	void notifyColorChanged()
	{
		if (m_onColorChanged)
		{
			m_onColorChanged(getColor());
		}
	}

	/// @brief 状態を UINode へ同期する。
	void syncNodeState()
	{
		const auto rgb = getColor();
		m_node->setProperty("color_r", std::to_string(rgb.r));
		m_node->setProperty("color_g", std::to_string(rgb.g));
		m_node->setProperty("color_b", std::to_string(rgb.b));
		m_node->setProperty("color_a", std::to_string(rgb.a));
		m_node->setProperty("hue", std::to_string(m_hsv.h));
		m_node->setProperty("saturation", std::to_string(m_hsv.s));
		m_node->setProperty("value", std::to_string(m_hsv.v));
		m_node->setProperty("alpha", std::to_string(m_alpha));
		m_node->setProperty("hex", getHex());
	}

	// -- Color Conversion Utilities ------------------------------------------

	/// @brief HSV を RGBA に変換する。
	[[nodiscard]] static UIColorRGBA hsvToRgb(const UIColorHSV& hsv, float alpha) noexcept
	{
		const float h = hsv.h;
		const float s = hsv.s;
		const float v = hsv.v;

		if (s <= 0.0f)
		{
			return {v, v, v, alpha};
		}

		const float hh = std::fmod(h, 360.0f) / 60.0f;
		const int sector = static_cast<int>(hh);
		const float frac = hh - static_cast<float>(sector);
		const float p = v * (1.0f - s);
		const float q = v * (1.0f - s * frac);
		const float t = v * (1.0f - s * (1.0f - frac));

		float r = 0.0f, g = 0.0f, b = 0.0f;
		switch (sector)
		{
		case 0:  r = v; g = t; b = p; break;
		case 1:  r = q; g = v; b = p; break;
		case 2:  r = p; g = v; b = t; break;
		case 3:  r = p; g = q; b = v; break;
		case 4:  r = t; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
		}
		return {r, g, b, alpha};
	}

	/// @brief RGB を HSV に変換する。
	[[nodiscard]] static UIColorHSV rgbToHsv(float r, float g, float b) noexcept
	{
		r = std::clamp(r, 0.0f, 1.0f);
		g = std::clamp(g, 0.0f, 1.0f);
		b = std::clamp(b, 0.0f, 1.0f);

		const float maxC = std::max({r, g, b});
		const float minC = std::min({r, g, b});
		const float delta = maxC - minC;

		UIColorHSV hsv;
		hsv.v = maxC;

		if (delta < 0.00001f)
		{
			hsv.s = 0.0f;
			hsv.h = 0.0f;
			return hsv;
		}

		hsv.s = delta / maxC;

		if (r >= maxC)
		{
			hsv.h = 60.0f * (g - b) / delta;
		}
		else if (g >= maxC)
		{
			hsv.h = 60.0f * (2.0f + (b - r) / delta);
		}
		else
		{
			hsv.h = 60.0f * (4.0f + (r - g) / delta);
		}

		if (hsv.h < 0.0f)
		{
			hsv.h += 360.0f;
		}

		return hsv;
	}
};

} // namespace mitiru::ui
