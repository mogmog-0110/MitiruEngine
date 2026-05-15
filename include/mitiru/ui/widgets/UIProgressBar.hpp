#pragma once

/// @file UIProgressBar.hpp
/// @brief Progress bar widget with animated fill, label formatting, and indeterminate mode.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Label format for progress bar display text.
enum class ProgressLabelFormat : std::uint8_t
{
	None,           ///< No label.
	ValueSlashMax,  ///< "75/100"
	Percent         ///< "75%"
};

/// @brief Configuration for creating a UIProgressBar.
struct UIProgressBarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float min = 0.0f;
	float max = 100.0f;
	float value = 0.0f;
	bool showLabel = true;
	ProgressLabelFormat labelFormat = ProgressLabelFormat::Percent;
	bool animated = true;         ///< Smooth value transitions.
	bool indeterminate = false;   ///< Bouncing bar mode (ignores value).
	float width = 200.0f;
	float height = 20.0f;
};

/// @brief Progress bar widget that wraps a UINode with animated fill and label logic.
///
/// Manages value display, smooth animation via update(), and indeterminate
/// (bouncing) mode. The UINode's value/maxValue and properties encode all
/// state needed for rendering.
///
/// @code
///   UIProgressBarConfig cfg;
///   cfg.id = 80;
///   cfg.max = 100.0f;
///   cfg.value = 30.0f;
///   cfg.labelFormat = ProgressLabelFormat::Percent;
///   UIProgressBar bar(cfg);
///
///   bar.setValue(75.0f);
///   bar.update(0.016f);  // smooth animation step
/// @endcode
class UIProgressBar
{
	std::shared_ptr<UINode> m_node;
	float m_min;
	float m_max;
	float m_targetValue;
	float m_displayValue;
	bool m_showLabel;
	ProgressLabelFormat m_labelFormat;
	bool m_animated;
	bool m_indeterminate;
	float m_indeterminatePhase = 0.0f;
	float m_animationSpeed = 5.0f;  ///< Units per second for smooth transitions.

public:
	/// @brief Construct a progress bar from configuration.
	/// @param config Progress bar configuration.
	explicit UIProgressBar(const UIProgressBarConfig& config)
		: m_min(config.min)
		, m_max(config.max)
		, m_targetValue(std::clamp(config.value, config.min, config.max))
		, m_displayValue(m_targetValue)
		, m_showLabel(config.showLabel)
		, m_labelFormat(config.labelFormat)
		, m_animated(config.animated)
		, m_indeterminate(config.indeterminate)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::ProgressBar;
		data.value = m_displayValue;
		data.maxValue = m_max;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "progress_bar";
		data.properties["min"] = std::to_string(m_min);
		data.properties["indeterminate"] = config.indeterminate ? "true" : "false";
		data.properties["animated"] = config.animated ? "true" : "false";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the target value (the value being animated toward).
	[[nodiscard]] float value() const noexcept { return m_targetValue; }

	/// @brief Get the current display value (may lag behind target during animation).
	[[nodiscard]] float displayValue() const noexcept { return m_displayValue; }

	/// @brief Get the normalized progress (0..1).
	[[nodiscard]] float normalizedValue() const noexcept
	{
		if (m_max <= m_min) { return 0.0f; }
		return (m_displayValue - m_min) / (m_max - m_min);
	}

	/// @brief Check if in indeterminate mode.
	[[nodiscard]] bool isIndeterminate() const noexcept { return m_indeterminate; }

	/// @brief Get the formatted label string.
	[[nodiscard]] std::string labelText() const
	{
		if (!m_showLabel || m_indeterminate) { return {}; }

		switch (m_labelFormat)
		{
		case ProgressLabelFormat::ValueSlashMax:
		{
			const int v = static_cast<int>(std::round(m_displayValue));
			const int mx = static_cast<int>(std::round(m_max));
			return std::to_string(v) + "/" + std::to_string(mx);
		}
		case ProgressLabelFormat::Percent:
		{
			const int pct = static_cast<int>(std::round(normalizedValue() * 100.0f));
			return std::to_string(pct) + "%";
		}
		case ProgressLabelFormat::None:
		default:
			return {};
		}
	}

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the target value.
	/// @param val New target value (clamped to [min, max]).
	void setValue(float val)
	{
		m_targetValue = std::clamp(val, m_min, m_max);
		if (!m_animated)
		{
			m_displayValue = m_targetValue;
			syncNodeState();
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
		m_targetValue = std::clamp(m_targetValue, m_min, m_max);
		m_displayValue = std::clamp(m_displayValue, m_min, m_max);
		syncNodeState();
	}

	/// @brief Set the animation speed.
	/// @param unitsPerSecond How fast the display value catches up to the target.
	void setAnimationSpeed(float unitsPerSecond) { m_animationSpeed = unitsPerSecond; }

	/// @brief Enable or disable indeterminate mode.
	/// @param indeterminate True for bouncing bar mode.
	void setIndeterminate(bool indeterminate)
	{
		m_indeterminate = indeterminate;
		m_node->setProperty("indeterminate", m_indeterminate ? "true" : "false");
		syncNodeState();
	}

	/// @brief Set the label format.
	/// @param format Label display format.
	void setLabelFormat(ProgressLabelFormat format)
	{
		m_labelFormat = format;
		syncNodeState();
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief Advance animation by one frame.
	/// @param deltaTime Elapsed time in seconds since last frame.
	void update(float deltaTime)
	{
		if (m_indeterminate)
		{
			m_indeterminatePhase += deltaTime * 2.0f;
			if (m_indeterminatePhase > 2.0f) { m_indeterminatePhase -= 2.0f; }
			m_node->setProperty("indeterminate_phase", std::to_string(m_indeterminatePhase));
			return;
		}

		if (m_animated && m_displayValue != m_targetValue)
		{
			const float diff = m_targetValue - m_displayValue;
			const float step = m_animationSpeed * deltaTime;

			if (std::abs(diff) <= step)
			{
				m_displayValue = m_targetValue;
			}
			else
			{
				m_displayValue += (diff > 0.0f ? step : -step);
			}
			syncNodeState();
		}
	}

private:
	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setValue(m_displayValue);
		m_node->setProperty("normalized", std::to_string(normalizedValue()));
		m_node->setText(labelText());
	}
};

} // namespace mitiru::ui
