#pragma once

/// @file UIToggle.hpp
/// @brief Toggle (checkbox / switch) widget for boolean values.

#include <mitiru/ui/UINode.hpp>

#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Visual style for the toggle widget.
enum class ToggleStyle : std::uint8_t
{
	Checkbox,  ///< Square checkbox with checkmark.
	Switch     ///< Sliding switch (pill shape).
};

/// @brief Configuration for creating a UIToggle.
struct UIToggleConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string label;
	bool checked = false;
	bool enabled = true;
	ToggleStyle style = ToggleStyle::Checkbox;
	float width = 0.0f;   ///< 0 = auto-size from label.
	float height = 24.0f;
};

/// @brief Toggle widget that wraps a UINode with click-to-toggle logic.
///
/// The toggle manages a boolean checked state and notifies via callback.
/// It creates a container node with a toggle indicator child and a label child.
///
/// @code
///   UIToggleConfig cfg;
///   cfg.id = 30;
///   cfg.label = "Show FPS";
///   cfg.checked = true;
///   UIToggle toggle(cfg);
///
///   toggle.setOnChanged([](bool checked) {
///       /* handle state change */
///   });
///   toggle.onClick();  // flips checked state
/// @endcode
class UIToggle
{
	std::shared_ptr<UINode> m_node;
	bool m_checked;
	bool m_enabled;
	ToggleStyle m_style;
	std::function<void(bool)> m_onChanged;

public:
	/// @brief Construct a toggle from configuration.
	/// @param config Toggle configuration.
	explicit UIToggle(const UIToggleConfig& config)
		: m_checked(config.checked)
		, m_enabled(config.enabled)
		, m_style(config.style)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Toggle;
		data.text = config.label;
		data.value = m_checked ? 1.0f : 0.0f;
		data.bounds = sgc::Rectf(0.0f, 0.0f,
			config.width > 0.0f ? config.width : 200.0f,
			config.height);
		data.properties["widget_type"] = "toggle";
		data.properties["toggle_style"] = (m_style == ToggleStyle::Checkbox) ? "checkbox" : "switch";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Check if currently toggled on.
	[[nodiscard]] bool isChecked() const noexcept { return m_checked; }

	/// @brief Check if the toggle is enabled.
	[[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }

	/// @brief Get the label text.
	[[nodiscard]] const std::string& label() const noexcept { return m_node->text(); }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the state-changed callback.
	/// @param callback Function invoked with the new checked state.
	void setOnChanged(std::function<void(bool)> callback) { m_onChanged = std::move(callback); }

	/// @brief Set the checked state programmatically.
	/// @param checked New checked state.
	void setChecked(bool checked)
	{
		if (m_checked != checked)
		{
			m_checked = checked;
			syncNodeState();
			if (m_onChanged) { m_onChanged(m_checked); }
		}
	}

	/// @brief Set the label text.
	/// @param label New label.
	void setLabel(const std::string& label)
	{
		m_node->setText(label);
	}

	/// @brief Set enabled state.
	/// @param enabled True to enable interaction.
	void setEnabled(bool enabled)
	{
		m_enabled = enabled;
		syncNodeState();
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Called when the toggle is clicked.
	void onClick()
	{
		if (!m_enabled) { return; }
		m_checked = !m_checked;
		syncNodeState();
		if (m_onChanged) { m_onChanged(m_checked); }
	}

private:
	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setValue(m_checked ? 1.0f : 0.0f);
		m_node->setProperty("checked", m_checked ? "true" : "false");
		m_node->setProperty("enabled", m_enabled ? "true" : "false");
	}
};

} // namespace mitiru::ui
