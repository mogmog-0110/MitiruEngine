#pragma once

/// @file UISlider.hpp
/// @brief Draggable slider widget for numeric value selection.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Orientation for slider or bar widgets.
enum class Orientation : std::uint8_t
{
	Horizontal,
	Vertical
};

/// @brief Configuration for creating a UISlider.
struct UISliderConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float min = 0.0f;
	float max = 1.0f;
	float value = 0.0f;
	float step = 0.0f;             ///< 0 = continuous (no snapping).
	Orientation orientation = Orientation::Horizontal;
	float trackLength = 200.0f;
	float trackThickness = 8.0f;
	float handleSize = 16.0f;
};

/// @brief Slider widget that wraps a UINode with drag-to-change-value logic.
///
/// Handles pointer drag, keyboard increment/decrement, and step snapping.
/// The value is stored in the UINode's value/maxValue fields for renderer access.
///
/// @code
///   UISliderConfig cfg;
///   cfg.id = 20;
///   cfg.min = 0.0f;
///   cfg.max = 100.0f;
///   cfg.step = 5.0f;
///   UISlider slider(cfg);
///
///   slider.setOnValueChanged([](float v) { /* use v */ });
///   slider.onDragUpdate(0.45f);  // normalized 0..1 along track
/// @endcode
class UISlider
{
	std::shared_ptr<UINode> m_node;
	float m_min;
	float m_max;
	float m_value;
	float m_step;
	Orientation m_orientation;
	float m_handleSize;
	bool m_dragging = false;
	bool m_enabled = true;
	std::function<void(float)> m_onValueChanged;

public:
	/// @brief Construct a slider from configuration.
	/// @param config Slider configuration.
	explicit UISlider(const UISliderConfig& config)
		: m_min(config.min)
		, m_max(config.max)
		, m_value(std::clamp(config.value, config.min, config.max))
		, m_step(config.step)
		, m_orientation(config.orientation)
		, m_handleSize(config.handleSize)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Slider;
		data.value = m_value;
		data.maxValue = m_max;
		data.properties["widget_type"] = "slider";
		data.properties["min"] = std::to_string(m_min);
		data.properties["orientation"] = (m_orientation == Orientation::Horizontal) ? "horizontal" : "vertical";
		data.properties["handle_size"] = std::to_string(m_handleSize);
		data.properties["step"] = std::to_string(m_step);

		if (m_orientation == Orientation::Horizontal)
		{
			data.bounds = sgc::Rectf(0.0f, 0.0f, config.trackLength, config.trackThickness);
		}
		else
		{
			data.bounds = sgc::Rectf(0.0f, 0.0f, config.trackThickness, config.trackLength);
		}

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current value.
	[[nodiscard]] float value() const noexcept { return m_value; }

	/// @brief Get the minimum value.
	[[nodiscard]] float min() const noexcept { return m_min; }

	/// @brief Get the maximum value.
	[[nodiscard]] float max() const noexcept { return m_max; }

	/// @brief Get the normalized value (0..1).
	[[nodiscard]] float normalizedValue() const noexcept
	{
		if (m_max <= m_min) { return 0.0f; }
		return (m_value - m_min) / (m_max - m_min);
	}

	/// @brief Check if currently being dragged.
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the value-changed callback.
	/// @param callback Function invoked when value changes.
	void setOnValueChanged(std::function<void(float)> callback) { m_onValueChanged = std::move(callback); }

	/// @brief Set the value programmatically.
	/// @param val New value (clamped to [min, max] and snapped to step).
	void setValue(float val)
	{
		const float snapped = snapToStep(std::clamp(val, m_min, m_max));
		if (snapped != m_value)
		{
			m_value = snapped;
			syncNodeState();
			if (m_onValueChanged) { m_onValueChanged(m_value); }
		}
	}

	/// @brief Set the value range.
	/// @param minVal Minimum value.
	/// @param maxVal Maximum value.
	void setRange(float minVal, float maxVal)
	{
		m_min = minVal;
		m_max = maxVal;
		m_node->setProperty("min", std::to_string(m_min));
		setValue(m_value);  // re-clamp
	}

	/// @brief Set enabled state.
	/// @param enabled True to enable interaction.
	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
		m_node->setProperty("enabled", m_enabled ? "true" : "false");
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Called when a drag begins on the slider.
	void onDragBegin()
	{
		if (!m_enabled) { return; }
		m_dragging = true;
		m_node->setProperty("dragging", "true");
	}

	/// @brief Called during drag with a normalized position (0..1) along the track.
	/// @param normalizedPos Position along track, 0 = min end, 1 = max end.
	void onDragUpdate(float normalizedPos)
	{
		if (!m_enabled || !m_dragging) { return; }
		const float clamped = std::clamp(normalizedPos, 0.0f, 1.0f);
		setValue(m_min + clamped * (m_max - m_min));
	}

	/// @brief Called when the drag ends.
	void onDragEnd()
	{
		m_dragging = false;
		m_node->setProperty("dragging", "false");
	}

	/// @brief Increment the value by one step (or 1% of range if no step).
	void increment()
	{
		if (!m_enabled) { return; }
		const float delta = (m_step > 0.0f) ? m_step : (m_max - m_min) * 0.01f;
		setValue(m_value + delta);
	}

	/// @brief Decrement the value by one step (or 1% of range if no step).
	void decrement()
	{
		if (!m_enabled) { return; }
		const float delta = (m_step > 0.0f) ? m_step : (m_max - m_min) * 0.01f;
		setValue(m_value - delta);
	}

private:
	/// @brief Snap a value to the nearest step, if step > 0.
	[[nodiscard]] float snapToStep(float val) const noexcept
	{
		if (m_step <= 0.0f) { return val; }
		return m_min + std::round((val - m_min) / m_step) * m_step;
	}

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setValue(m_value);
		m_node->setProperty("normalized", std::to_string(normalizedValue()));
	}
};

} // namespace mitiru::ui
