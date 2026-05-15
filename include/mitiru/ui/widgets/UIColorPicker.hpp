#pragma once

/// @file UIColorPicker.hpp
/// @brief Color selection widget with HSV model, hue bar, alpha bar, and preset swatches.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief RGBA color value (0.0 - 1.0 per channel).
struct UIColorRGBA
{
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

/// @brief HSV color value (H: 0-360, S: 0-1, V: 0-1).
struct UIColorHSV
{
	float h = 0.0f;   ///< Hue in degrees (0-360).
	float s = 1.0f;   ///< Saturation (0-1).
	float v = 1.0f;   ///< Value/brightness (0-1).
};

/// @brief Preset color entry.
struct UIColorPreset
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 1.0f;
};

/// @brief Which component of the color picker is being interacted with.
enum class ColorPickerInteraction : std::uint8_t
{
	None,
	SaturationValue,
	HueBar,
	AlphaBar,
	Eyedropper
};

/// @brief Configuration for creating a UIColorPicker.
struct UIColorPickerConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float width = 260.0f;                  ///< Total widget width.
	float height = 300.0f;                 ///< Total widget height.
	bool showAlpha = true;                 ///< Show alpha channel bar.
	bool showHex = true;                   ///< Show hex color input.
	bool showRGB = true;                   ///< Show RGB sliders.
	bool showHSV = false;                  ///< Show HSV sliders.
	std::vector<UIColorPreset> presetColors;  ///< Preset color swatches.
	float hueBarWidth = 20.0f;             ///< Width of the vertical hue bar.
	float alphaBarHeight = 16.0f;          ///< Height of the horizontal alpha bar.
	float previewSize = 40.0f;             ///< Size of the color preview square.
	std::string backgroundImageKey;        ///< Background image key.
	std::string hueBarImageKey;            ///< Hue bar gradient image key.
	float alphaCheckerSize = 8.0f;         ///< Size of alpha checker pattern cells.
	float fontSize = 12.0f;                ///< Font size for labels and hex input.
	float labelWidth = 20.0f;              ///< Width of "R:", "G:", "B:" labels.
};

/// @brief Color picker widget with HSV model, hue bar, SV box, alpha bar, and presets.
///
/// Provides a saturation-value rectangular box plus a vertical hue bar. Optionally
/// shows an alpha bar, hex input, RGB sliders, and preset color swatches.
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
	/// @brief Construct a color picker from configuration.
	/// @param config Color picker configuration.
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current color as RGBA.
	[[nodiscard]] UIColorRGBA getColor() const noexcept
	{
		return hsvToRgb(m_hsv, m_alpha);
	}

	/// @brief Get the current color as HSV.
	[[nodiscard]] UIColorHSV getHSV() const noexcept { return m_hsv; }

	/// @brief Get the current alpha value.
	[[nodiscard]] float getAlpha() const noexcept { return m_alpha; }

	/// @brief Get the current hex string (e.g. "#FF8040" or "#FF8040CC").
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

	/// @brief Check if eyedropper mode is active.
	[[nodiscard]] bool isEyedropperActive() const noexcept { return m_eyedropperMode; }

	// -- Configuration -------------------------------------------------------

	/// @brief Set the color-changed callback.
	/// @param callback Function invoked with the new RGBA color.
	void setOnColorChanged(std::function<void(const UIColorRGBA&)> callback)
	{
		m_onColorChanged = std::move(callback);
	}

	// -- Color Setters -------------------------------------------------------

	/// @brief Set the color from RGBA values.
	/// @param r Red (0-1).
	/// @param g Green (0-1).
	/// @param b Blue (0-1).
	/// @param a Alpha (0-1).
	void setColor(float r, float g, float b, float a = 1.0f)
	{
		m_hsv = rgbToHsv(r, g, b);
		m_alpha = std::clamp(a, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief Set the color from HSV values.
	/// @param h Hue (0-360).
	/// @param s Saturation (0-1).
	/// @param v Value (0-1).
	void setHSV(float h, float s, float v)
	{
		m_hsv.h = std::fmod(std::max(h, 0.0f), 360.0f);
		m_hsv.s = std::clamp(s, 0.0f, 1.0f);
		m_hsv.v = std::clamp(v, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief Set the alpha value.
	/// @param a Alpha (0-1).
	void setAlpha(float a)
	{
		m_alpha = std::clamp(a, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief Select a preset color by index.
	/// @param index Preset index.
	void selectPreset(std::size_t index)
	{
		if (index >= m_presets.size()) { return; }
		const auto& p = m_presets[index];
		setColor(p.r, p.g, p.b, p.a);
	}

	// -- Interaction ---------------------------------------------------------

	/// @brief Begin interaction with a specific component.
	/// @param component Which component is being interacted with.
	void onInteractionBegin(ColorPickerInteraction component)
	{
		m_interaction = component;
	}

	/// @brief Update the saturation-value box with normalized coordinates.
	/// @param normX Normalized X (0-1, maps to saturation).
	/// @param normY Normalized Y (0-1, maps to value, 0 = top = bright).
	void onSaturationValueUpdate(float normX, float normY)
	{
		if (m_interaction != ColorPickerInteraction::SaturationValue) { return; }
		m_hsv.s = std::clamp(normX, 0.0f, 1.0f);
		m_hsv.v = std::clamp(1.0f - normY, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief Update the hue bar with a normalized position.
	/// @param normY Normalized Y (0-1, maps to hue 0-360).
	void onHueBarUpdate(float normY)
	{
		if (m_interaction != ColorPickerInteraction::HueBar) { return; }
		m_hsv.h = std::clamp(normY, 0.0f, 1.0f) * 360.0f;
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief Update the alpha bar with a normalized position.
	/// @param normX Normalized X (0-1, maps to alpha).
	void onAlphaBarUpdate(float normX)
	{
		if (m_interaction != ColorPickerInteraction::AlphaBar) { return; }
		m_alpha = std::clamp(normX, 0.0f, 1.0f);
		syncNodeState();
		notifyColorChanged();
	}

	/// @brief End the current interaction.
	void onInteractionEnd()
	{
		m_interaction = ColorPickerInteraction::None;
	}

	/// @brief Toggle eyedropper mode.
	void toggleEyedropper()
	{
		m_eyedropperMode = !m_eyedropperMode;
		m_node->setProperty("eyedropper_active", m_eyedropperMode ? "true" : "false");
	}

	/// @brief Set color from eyedropper pick result.
	/// @param r Red (0-1).
	/// @param g Green (0-1).
	/// @param b Blue (0-1).
	void onEyedropperPick(float r, float g, float b)
	{
		m_eyedropperMode = false;
		m_node->setProperty("eyedropper_active", "false");
		setColor(r, g, b, m_alpha);
	}

private:
	/// @brief Notify color change through callback.
	void notifyColorChanged()
	{
		if (m_onColorChanged)
		{
			m_onColorChanged(getColor());
		}
	}

	/// @brief Synchronize state to the UINode.
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

	/// @brief Convert HSV to RGBA.
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

	/// @brief Convert RGB to HSV.
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
