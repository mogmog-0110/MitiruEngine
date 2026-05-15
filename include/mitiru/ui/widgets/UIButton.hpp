#pragma once

/// @file UIButton.hpp
/// @brief Clickable button widget with normal/hover/pressed/disabled states and toggle mode.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Visual/interaction state of a button.
enum class ButtonState : std::uint8_t
{
	Normal,
	Hovered,
	Pressed,
	Disabled
};

/// @brief Configuration for creating a UIButton.
struct UIButtonConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string text;
	float width = 120.0f;
	float height = 32.0f;
	bool enabled = true;
	bool toggleable = false;
};

/// @brief Button widget that wraps a UINode with press/release/click logic.
///
/// The button configures a UINode with role Button and manages interaction
/// state transitions. Rendering is handled externally by UIRenderer.
///
/// @code
///   UIButtonConfig cfg;
///   cfg.id = 10;
///   cfg.text = "Start";
///   cfg.toggleable = true;
///   UIButton btn(cfg);
///
///   btn.setOnClick([] { /* handle click */ });
///   btn.onPointerEnter();
///   btn.onPointerDown();
///   btn.onPointerUp();  // triggers onClick
/// @endcode
class UIButton
{
	std::shared_ptr<UINode> m_node;
	ButtonState m_state = ButtonState::Normal;
	bool m_enabled = true;
	bool m_toggleable = false;
	bool m_toggled = false;
	bool m_pointerInside = false;
	std::function<void()> m_onClick;

public:
	/// @brief Construct a button from configuration.
	/// @param config Button configuration.
	explicit UIButton(const UIButtonConfig& config)
		: m_enabled(config.enabled)
		, m_toggleable(config.toggleable)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Button;
		data.text = config.text;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.height);
		data.properties["widget_type"] = "button";
		data.properties["toggleable"] = m_toggleable ? "true" : "false";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current button state.
	[[nodiscard]] ButtonState state() const noexcept { return m_state; }

	/// @brief Check if the button is enabled.
	[[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

	/// @brief Check if the button is currently toggled on (only meaningful if toggleable).
	[[nodiscard]] bool isToggled() const noexcept { return m_toggled; }

	/// @brief Get the button text.
	[[nodiscard]] const std::string& text() const noexcept { return m_node->text(); }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the click callback.
	/// @param callback Function invoked on click.
	void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

	/// @brief Set whether the button is enabled.
	/// @param enabled True to enable.
	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
		if (!m_enabled)
		{
			m_state = ButtonState::Disabled;
		}
		else if (m_state == ButtonState::Disabled)
		{
			m_state = ButtonState::Normal;
		}
		syncNodeState();
	}

	/// @brief Set the button display text.
	/// @param text New text.
	void setText(const std::string& text)
	{
		m_node->setText(text);
	}

	// ── Interaction (called by event system) ─────────────────

	/// @brief Called when the pointer enters the button area.
	void onPointerEnter()
	{
		m_pointerInside = true;
		if (!m_enabled) { return; }
		if (m_state != ButtonState::Pressed)
		{
			m_state = ButtonState::Hovered;
			syncNodeState();
		}
	}

	/// @brief Called when the pointer leaves the button area.
	void onPointerLeave()
	{
		m_pointerInside = false;
		if (!m_enabled) { return; }
		if (m_state != ButtonState::Pressed || !m_toggleable || !m_toggled)
		{
			m_state = ButtonState::Normal;
			syncNodeState();
		}
	}

	/// @brief Called when the pointer is pressed down on the button.
	void onPointerDown()
	{
		if (!m_enabled) { return; }
		m_state = ButtonState::Pressed;
		syncNodeState();
	}

	/// @brief Called when the pointer is released.
	void onPointerUp()
	{
		if (!m_enabled) { return; }
		if (m_state == ButtonState::Pressed && m_pointerInside)
		{
			if (m_toggleable)
			{
				m_toggled = !m_toggled;
			}

			if (m_onClick) { m_onClick(); }
		}

		if (m_pointerInside)
		{
			m_state = (m_toggleable && m_toggled) ? ButtonState::Pressed : ButtonState::Hovered;
		}
		else
		{
			m_state = ButtonState::Normal;
		}
		syncNodeState();
	}

private:
	/// @brief Synchronize interaction state to the UINode properties.
	void syncNodeState()
	{
		const char* stateStr = "normal";
		switch (m_state)
		{
		case ButtonState::Hovered:  stateStr = "hovered";  break;
		case ButtonState::Pressed:  stateStr = "pressed";  break;
		case ButtonState::Disabled: stateStr = "disabled"; break;
		default: break;
		}
		m_node->setProperty("state", stateStr);
		m_node->setProperty("toggled", m_toggled ? "true" : "false");
		m_node->setProperty("enabled", m_enabled ? "true" : "false");
	}
};

} // namespace mitiru::ui
