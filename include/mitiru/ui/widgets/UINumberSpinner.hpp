#pragma once

/// @file UINumberSpinner.hpp
/// @brief Numeric input widget with increment/decrement buttons and auto-repeat.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace mitiru::ui {

/// @brief Configuration for creating a UINumberSpinner.
struct UINumberSpinnerConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float value = 0.0f;                    ///< Initial value.
	float min = 0.0f;                      ///< Minimum value.
	float max = 100.0f;                    ///< Maximum value.
	float step = 1.0f;                     ///< Increment/decrement step.
	int decimalPlaces = 0;                 ///< Decimal places for display.
	float width = 160.0f;                  ///< Total widget width.
	float height = 32.0f;                  ///< Total widget height.
	float buttonWidth = 32.0f;             ///< Width of +/- buttons.
	float inputWidth = 96.0f;              ///< Width of the number display area.
	std::string incrementImageKey;         ///< Image for increment button.
	std::string decrementImageKey;         ///< Image for decrement button.
	std::string inputBackgroundImageKey;   ///< Image for number input background.
	float fontSize = 14.0f;                ///< Font size for the number display.
	float holdRepeatDelay = 0.5f;          ///< Seconds before auto-repeat starts.
	float holdRepeatRate = 0.1f;           ///< Seconds between auto-repeat ticks.
};

/// @brief Numeric spinner widget with +/- buttons and hold-to-repeat.
///
/// Provides increment/decrement buttons, direct text input, and auto-repeat
/// when a button is held down. Values are clamped to [min, max] and snapped
/// to the configured step.
///
/// @code
///   UINumberSpinnerConfig cfg;
///   cfg.id = 110;
///   cfg.value = 50.0f;
///   cfg.min = 0.0f;
///   cfg.max = 100.0f;
///   cfg.step = 5.0f;
///   UINumberSpinner spinner(cfg);
///
///   spinner.setOnValueChanged([](float v) { /* use v */ });
///   spinner.increment();
///   spinner.update(0.016f);
/// @endcode
class UINumberSpinner
{
	std::shared_ptr<UINode> m_node;
	float m_value;
	float m_min;
	float m_max;
	float m_step;
	int m_decimalPlaces;
	float m_holdRepeatDelay;
	float m_holdRepeatRate;
	bool m_editing = false;
	std::string m_editBuffer;

	// Hold-to-repeat state.
	enum class HoldDirection : std::uint8_t { None, Increment, Decrement };
	HoldDirection m_holdDirection = HoldDirection::None;
	float m_holdTimer = 0.0f;
	bool m_holdRepeating = false;

	std::function<void(float)> m_onValueChanged;

public:
	/// @brief Construct a number spinner from configuration.
	/// @param config Spinner configuration.
	explicit UINumberSpinner(const UINumberSpinnerConfig& config)
		: m_value(std::clamp(config.value, config.min, config.max))
		, m_min(config.min)
		, m_max(config.max)
		, m_step(config.step)
		, m_decimalPlaces(config.decimalPlaces)
		, m_holdRepeatDelay(config.holdRepeatDelay)
		, m_holdRepeatRate(config.holdRepeatRate)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.value = m_value;
		data.maxValue = m_max;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "number_spinner";
		data.properties["min"] = std::to_string(m_min);
		data.properties["max"] = std::to_string(m_max);
		data.properties["step"] = std::to_string(m_step);
		data.properties["decimal_places"] = std::to_string(m_decimalPlaces);
		data.properties["button_width"] = std::to_string(config.buttonWidth);
		data.properties["input_width"] = std::to_string(config.inputWidth);
		data.properties["increment_image"] = config.incrementImageKey;
		data.properties["decrement_image"] = config.decrementImageKey;
		data.properties["input_bg_image"] = config.inputBackgroundImageKey;
		data.properties["font_size"] = std::to_string(config.fontSize);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current value.
	[[nodiscard]] float getValue() const noexcept { return m_value; }

	/// @brief Get the minimum value.
	[[nodiscard]] float getMin() const noexcept { return m_min; }

	/// @brief Get the maximum value.
	[[nodiscard]] float getMax() const noexcept { return m_max; }

	/// @brief Check if currently in text editing mode.
	[[nodiscard]] bool isEditing() const noexcept { return m_editing; }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the value-changed callback.
	/// @param callback Function invoked when value changes.
	void setOnValueChanged(std::function<void(float)> callback) { m_onValueChanged = std::move(callback); }

	/// @brief Set the value programmatically.
	/// @param val New value (clamped and snapped).
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
		m_node->setProperty("max", std::to_string(m_max));
		setValue(m_value); // re-clamp
	}

	/// @brief Set the step size.
	/// @param step New step value.
	void setStep(float step)
	{
		m_step = step;
		m_node->setProperty("step", std::to_string(m_step));
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Increment the value by one step.
	void increment()
	{
		setValue(m_value + m_step);
	}

	/// @brief Decrement the value by one step.
	void decrement()
	{
		setValue(m_value - m_step);
	}

	/// @brief Begin holding the increment button.
	void beginHoldIncrement()
	{
		m_holdDirection = HoldDirection::Increment;
		m_holdTimer = 0.0f;
		m_holdRepeating = false;
		increment();
	}

	/// @brief Begin holding the decrement button.
	void beginHoldDecrement()
	{
		m_holdDirection = HoldDirection::Decrement;
		m_holdTimer = 0.0f;
		m_holdRepeating = false;
		decrement();
	}

	/// @brief Release the held button.
	void endHold()
	{
		m_holdDirection = HoldDirection::None;
		m_holdTimer = 0.0f;
		m_holdRepeating = false;
	}

	/// @brief Update auto-repeat logic. Call each frame.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		if (m_holdDirection == HoldDirection::None) { return; }

		m_holdTimer += dt;

		if (!m_holdRepeating)
		{
			if (m_holdTimer >= m_holdRepeatDelay)
			{
				m_holdRepeating = true;
				m_holdTimer -= m_holdRepeatDelay;
			}
		}

		if (m_holdRepeating)
		{
			while (m_holdTimer >= m_holdRepeatRate)
			{
				m_holdTimer -= m_holdRepeatRate;
				if (m_holdDirection == HoldDirection::Increment)
				{
					increment();
				}
				else
				{
					decrement();
				}
			}
		}
	}

	// ── Text Editing ─────────────────────────────────────────

	/// @brief Begin direct text editing of the number field.
	void beginEditing()
	{
		m_editing = true;
		m_editBuffer = formatValue(m_value);
		m_node->setProperty("editing", "true");
		m_node->setProperty("edit_buffer", m_editBuffer);
	}

	/// @brief Append a character to the edit buffer.
	/// @param ch Character to append (digits, '.', '-').
	void editAppendChar(char ch)
	{
		if (!m_editing) { return; }

		// Allow digits, decimal point, minus sign.
		const bool isDigit = (ch >= '0' && ch <= '9');
		const bool isDot = (ch == '.' && m_decimalPlaces > 0 && m_editBuffer.find('.') == std::string::npos);
		const bool isMinus = (ch == '-' && m_editBuffer.empty() && m_min < 0.0f);

		if (isDigit || isDot || isMinus)
		{
			m_editBuffer += ch;
			m_node->setProperty("edit_buffer", m_editBuffer);
		}
	}

	/// @brief Remove the last character from the edit buffer.
	void editBackspace()
	{
		if (!m_editing || m_editBuffer.empty()) { return; }
		m_editBuffer.pop_back();
		m_node->setProperty("edit_buffer", m_editBuffer);
	}

	/// @brief Confirm the text edit and apply the value.
	void confirmEditing()
	{
		if (!m_editing) { return; }
		m_editing = false;
		m_node->setProperty("editing", "false");

		if (!m_editBuffer.empty())
		{
			try
			{
				const float parsed = std::stof(m_editBuffer);
				setValue(parsed);
			}
			catch (...)
			{
				// Invalid input; keep current value.
			}
		}
		syncNodeState();
	}

	/// @brief Cancel text editing without applying.
	void cancelEditing()
	{
		m_editing = false;
		m_node->setProperty("editing", "false");
		syncNodeState();
	}

private:
	/// @brief Snap a value to the nearest step.
	[[nodiscard]] float snapToStep(float val) const noexcept
	{
		if (m_step <= 0.0f) { return val; }
		return m_min + std::round((val - m_min) / m_step) * m_step;
	}

	/// @brief Format a value with the configured decimal places.
	[[nodiscard]] std::string formatValue(float val) const
	{
		std::ostringstream oss;
		oss << std::fixed << std::setprecision(m_decimalPlaces) << val;
		return oss.str();
	}

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setValue(m_value);
		m_node->setProperty("display_value", formatValue(m_value));
		m_node->setText(formatValue(m_value));
	}
};

} // namespace mitiru::ui
