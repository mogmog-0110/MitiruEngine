#pragma once

/// @file UIRadioGroup.hpp
/// @brief Mutually exclusive selection group widget with keyboard navigation.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Orientation for radio group layout.
enum class RadioOrientation : std::uint8_t
{
	Vertical,
	Horizontal
};

/// @brief Data for a single radio option.
struct UIRadioOption
{
	std::string label;                ///< Display label.
	std::string value;                ///< Programmatic value string.
	bool enabled = true;              ///< Whether this option is selectable.
	std::string iconImageKey;         ///< Optional icon image key.
};

/// @brief Configuration for creating a UIRadioGroup.
struct UIRadioGroupConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIRadioOption> options;
	int selectedIndex = 0;
	RadioOrientation orientation = RadioOrientation::Vertical;
	float spacing = 8.0f;                  ///< Space between options.
	float radioSize = 16.0f;               ///< Radio indicator size.
	float labelGap = 6.0f;                 ///< Gap between indicator and label.
	std::string radioImageKey;             ///< Unchecked radio image.
	std::string radioCheckedImageKey;      ///< Checked radio image.
	std::string radioDisabledImageKey;     ///< Disabled radio image.
	float fontSize = 14.0f;                ///< Label font size.
};

/// @brief Radio group widget for mutually exclusive selection.
///
/// Manages a set of radio options where exactly one may be selected at a time.
/// Keyboard navigation with up/down to move focus and space to confirm.
///
/// @code
///   UIRadioGroupConfig cfg;
///   cfg.id = 100;
///   cfg.options = {{"Low", "low"}, {"Medium", "med"}, {"High", "high"}};
///   cfg.selectedIndex = 1;
///   UIRadioGroup group(cfg);
///
///   group.setOnSelectionChanged([](int idx, const std::string& val) {
///       // handle selection
///   });
///   group.select(2);
/// @endcode
class UIRadioGroup
{
	std::shared_ptr<UINode> m_node;
	std::vector<UIRadioOption> m_options;
	int m_selectedIndex;
	int m_focusedIndex = 0;
	RadioOrientation m_orientation;
	float m_spacing;
	float m_radioSize;
	float m_labelGap;
	float m_fontSize;
	std::string m_radioImageKey;
	std::string m_radioCheckedImageKey;
	std::string m_radioDisabledImageKey;
	std::function<void(int, const std::string&)> m_onSelectionChanged;

public:
	/// @brief Construct a radio group from configuration.
	/// @param config Radio group configuration.
	explicit UIRadioGroup(const UIRadioGroupConfig& config)
		: m_options(config.options)
		, m_selectedIndex(config.selectedIndex)
		, m_orientation(config.orientation)
		, m_spacing(config.spacing)
		, m_radioSize(config.radioSize)
		, m_labelGap(config.labelGap)
		, m_fontSize(config.fontSize)
		, m_radioImageKey(config.radioImageKey)
		, m_radioCheckedImageKey(config.radioCheckedImageKey)
		, m_radioDisabledImageKey(config.radioDisabledImageKey)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.properties["widget_type"] = "radio_group";
		data.properties["orientation"] = (m_orientation == RadioOrientation::Vertical) ? "vertical" : "horizontal";
		data.properties["spacing"] = std::to_string(m_spacing);
		data.properties["radio_size"] = std::to_string(m_radioSize);
		data.properties["label_gap"] = std::to_string(m_labelGap);
		data.properties["font_size"] = std::to_string(m_fontSize);
		data.properties["radio_image"] = m_radioImageKey;
		data.properties["radio_checked_image"] = m_radioCheckedImageKey;
		data.properties["radio_disabled_image"] = m_radioDisabledImageKey;

		m_node = std::make_shared<UINode>(std::move(data));

		// Create child nodes for each option.
		for (std::size_t i = 0; i < m_options.size(); ++i)
		{
			UINodeData optData;
			optData.id = config.id + static_cast<UINodeId>(i) + 1;
			optData.name = config.name + "_option_" + std::to_string(i);
			optData.role = UIRole::MenuItem;
			optData.text = m_options[i].label;
			optData.properties["value"] = m_options[i].value;
			optData.properties["enabled"] = m_options[i].enabled ? "true" : "false";
			optData.properties["icon_image"] = m_options[i].iconImageKey;
			m_node->addChild(std::make_shared<UINode>(std::move(optData)));
		}

		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			m_focusedIndex = m_selectedIndex;
		}
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the currently selected index.
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief Get the currently selected value string.
	[[nodiscard]] const std::string& selectedValue() const
	{
		static const std::string empty;
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			return m_options[static_cast<std::size_t>(m_selectedIndex)].value;
		}
		return empty;
	}

	/// @brief Get the focused index (keyboard navigation).
	[[nodiscard]] int focusedIndex() const noexcept { return m_focusedIndex; }

	/// @brief Get the option count.
	[[nodiscard]] std::size_t optionCount() const noexcept { return m_options.size(); }

	/// @brief Get the options list.
	[[nodiscard]] const std::vector<UIRadioOption>& options() const noexcept { return m_options; }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the selection-changed callback.
	/// @param callback Function invoked with (index, value) when selection changes.
	void setOnSelectionChanged(std::function<void(int, const std::string&)> callback)
	{
		m_onSelectionChanged = std::move(callback);
	}

	/// @brief Replace the options list.
	/// @param options New options.
	void setOptions(std::vector<UIRadioOption> options)
	{
		m_options = std::move(options);
		if (m_selectedIndex >= static_cast<int>(m_options.size()))
		{
			m_selectedIndex = m_options.empty() ? -1 : 0;
		}
		m_focusedIndex = std::clamp(m_focusedIndex, 0, std::max(0, static_cast<int>(m_options.size()) - 1));
		syncNodeState();
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Select an option by index.
	/// @param index Option index to select.
	void select(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_options.size())) { return; }
		if (!m_options[static_cast<std::size_t>(index)].enabled) { return; }
		if (m_selectedIndex == index) { return; }

		m_selectedIndex = index;
		m_focusedIndex = index;
		syncNodeState();
		if (m_onSelectionChanged)
		{
			m_onSelectionChanged(m_selectedIndex, m_options[static_cast<std::size_t>(m_selectedIndex)].value);
		}
	}

	/// @brief Move focus to the previous option (Up/Left key).
	void focusPrevious()
	{
		if (m_options.empty()) { return; }
		const int count = static_cast<int>(m_options.size());
		int next = (m_focusedIndex - 1 + count) % count;

		// Skip disabled options (at most one full loop).
		for (int i = 0; i < count; ++i)
		{
			if (m_options[static_cast<std::size_t>(next)].enabled) { break; }
			next = (next - 1 + count) % count;
		}
		m_focusedIndex = next;
		syncNodeState();
	}

	/// @brief Move focus to the next option (Down/Right key).
	void focusNext()
	{
		if (m_options.empty()) { return; }
		const int count = static_cast<int>(m_options.size());
		int next = (m_focusedIndex + 1) % count;

		// Skip disabled options (at most one full loop).
		for (int i = 0; i < count; ++i)
		{
			if (m_options[static_cast<std::size_t>(next)].enabled) { break; }
			next = (next + 1) % count;
		}
		m_focusedIndex = next;
		syncNodeState();
	}

	/// @brief Confirm the currently focused option (Space key).
	void confirmFocused()
	{
		select(m_focusedIndex);
	}

private:
	/// @brief Synchronize selection state to the UINode tree.
	void syncNodeState()
	{
		m_node->setProperty("selected", std::to_string(m_selectedIndex));
		m_node->setProperty("focused", std::to_string(m_focusedIndex));
		m_node->setProperty("option_count", std::to_string(m_options.size()));

		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			m_node->setProperty("selected_value", m_options[static_cast<std::size_t>(m_selectedIndex)].value);
		}

		// Mark checked/unchecked on child nodes.
		const auto& children = m_node->children();
		for (std::size_t i = 0; i < children.size() && i < m_options.size(); ++i)
		{
			const bool checked = (static_cast<int>(i) == m_selectedIndex);
			const bool focused = (static_cast<int>(i) == m_focusedIndex);
			children[i]->setProperty("checked", checked ? "true" : "false");
			children[i]->setProperty("focused", focused ? "true" : "false");
			children[i]->setProperty("enabled", m_options[i].enabled ? "true" : "false");
		}
	}
};

} // namespace mitiru::ui
