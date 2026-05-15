#pragma once

/// @file UIDropdown.hpp
/// @brief Dropdown selection widget with scrollable option list.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Configuration for creating a UIDropdown.
struct UIDropdownConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<std::string> options;
	int selectedIndex = -1;     ///< -1 = no selection.
	float width = 180.0f;
	float itemHeight = 24.0f;
	int maxVisibleItems = 6;    ///< Max items shown before scrolling.
};

/// @brief Dropdown selection widget that wraps a UINode with open/close and selection logic.
///
/// Manages an option list, open/close state, scroll offset for overflow,
/// and keyboard navigation. The UINode tree contains the header and a
/// list container child with item children (only visible items are populated).
///
/// @code
///   UIDropdownConfig cfg;
///   cfg.id = 50;
///   cfg.options = {"Low", "Medium", "High", "Ultra"};
///   cfg.selectedIndex = 1;
///   UIDropdown dropdown(cfg);
///
///   dropdown.setOnSelectionChanged([](int idx) { /* use idx */ });
///   dropdown.toggle();
///   dropdown.selectItem(2);
/// @endcode
class UIDropdown
{
	std::shared_ptr<UINode> m_node;
	std::vector<std::string> m_options;
	int m_selectedIndex;
	int m_highlightedIndex = -1;
	float m_width;
	float m_itemHeight;
	int m_maxVisibleItems;
	int m_scrollOffset = 0;
	bool m_open = false;
	std::function<void(int)> m_onSelectionChanged;

public:
	/// @brief Construct a dropdown from configuration.
	/// @param config Dropdown configuration.
	explicit UIDropdown(const UIDropdownConfig& config)
		: m_options(config.options)
		, m_selectedIndex(config.selectedIndex)
		, m_width(config.width)
		, m_itemHeight(config.itemHeight)
		, m_maxVisibleItems(config.maxVisibleItems)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Dropdown;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.itemHeight);
		data.properties["widget_type"] = "dropdown";
		data.properties["item_count"] = std::to_string(m_options.size());
		data.properties["max_visible"] = std::to_string(m_maxVisibleItems);

		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			data.text = m_options[static_cast<std::size_t>(m_selectedIndex)];
		}

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Check if the dropdown list is open.
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief Get the currently selected index (-1 if none).
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief Get the text of the currently selected option.
	[[nodiscard]] std::string selectedText() const
	{
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			return m_options[static_cast<std::size_t>(m_selectedIndex)];
		}
		return {};
	}

	/// @brief Get the option list.
	[[nodiscard]] const std::vector<std::string>& options() const noexcept { return m_options; }

	/// @brief Get the current scroll offset.
	[[nodiscard]] int scrollOffset() const noexcept { return m_scrollOffset; }

	/// @brief Get the highlighted item index.
	[[nodiscard]] int highlightedIndex() const noexcept { return m_highlightedIndex; }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the selection-changed callback.
	void setOnSelectionChanged(std::function<void(int)> callback) { m_onSelectionChanged = std::move(callback); }

	/// @brief Replace the option list.
	/// @param options New option strings.
	void setOptions(std::vector<std::string> options)
	{
		m_options = std::move(options);
		m_scrollOffset = 0;
		m_node->setProperty("item_count", std::to_string(m_options.size()));

		if (m_selectedIndex >= static_cast<int>(m_options.size()))
		{
			m_selectedIndex = m_options.empty() ? -1 : 0;
		}
		syncNodeState();
	}

	/// @brief Set the selected index programmatically.
	/// @param index Index to select (-1 to clear).
	void setSelectedIndex(int index)
	{
		if (index < -1 || index >= static_cast<int>(m_options.size())) { return; }
		if (m_selectedIndex != index)
		{
			m_selectedIndex = index;
			syncNodeState();
			if (m_onSelectionChanged) { m_onSelectionChanged(m_selectedIndex); }
		}
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Toggle the dropdown open/closed.
	void toggle()
	{
		m_open = !m_open;
		if (m_open)
		{
			m_highlightedIndex = m_selectedIndex;
			ensureHighlightedVisible();
		}
		syncNodeState();
	}

	/// @brief Open the dropdown list.
	void open()
	{
		if (!m_open)
		{
			m_open = true;
			m_highlightedIndex = m_selectedIndex;
			ensureHighlightedVisible();
			syncNodeState();
		}
	}

	/// @brief Close the dropdown list.
	void close()
	{
		if (m_open)
		{
			m_open = false;
			syncNodeState();
		}
	}

	/// @brief Select an item by index and close the dropdown.
	/// @param index Item index to select.
	void selectItem(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_options.size())) { return; }
		m_selectedIndex = index;
		m_open = false;
		syncNodeState();
		if (m_onSelectionChanged) { m_onSelectionChanged(m_selectedIndex); }
	}

	/// @brief Highlight an item (e.g., on mouse hover).
	/// @param index Item index to highlight.
	void highlightItem(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_options.size())) { return; }
		m_highlightedIndex = index;
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
	}

	/// @brief Move highlight up (keyboard navigation).
	void highlightPrevious()
	{
		if (m_options.empty()) { return; }
		m_highlightedIndex = (m_highlightedIndex <= 0)
			? static_cast<int>(m_options.size()) - 1
			: m_highlightedIndex - 1;
		ensureHighlightedVisible();
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
	}

	/// @brief Move highlight down (keyboard navigation).
	void highlightNext()
	{
		if (m_options.empty()) { return; }
		m_highlightedIndex = (m_highlightedIndex >= static_cast<int>(m_options.size()) - 1)
			? 0
			: m_highlightedIndex + 1;
		ensureHighlightedVisible();
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
	}

	/// @brief Confirm the currently highlighted item.
	void confirmHighlighted()
	{
		if (m_highlightedIndex >= 0 && m_highlightedIndex < static_cast<int>(m_options.size()))
		{
			selectItem(m_highlightedIndex);
		}
	}

	/// @brief Scroll the visible list by a delta.
	/// @param delta Positive = scroll down, negative = scroll up.
	void scroll(int delta)
	{
		const int maxScroll = std::max(0, static_cast<int>(m_options.size()) - m_maxVisibleItems);
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxScroll);
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

private:
	/// @brief Ensure the highlighted item is within the visible scroll range.
	void ensureHighlightedVisible()
	{
		if (m_highlightedIndex < m_scrollOffset)
		{
			m_scrollOffset = m_highlightedIndex;
		}
		else if (m_highlightedIndex >= m_scrollOffset + m_maxVisibleItems)
		{
			m_scrollOffset = m_highlightedIndex - m_maxVisibleItems + 1;
		}
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			m_node->setText(m_options[static_cast<std::size_t>(m_selectedIndex)]);
		}
		else
		{
			m_node->setText("");
		}
		m_node->setProperty("open", m_open ? "true" : "false");
		m_node->setProperty("selected", std::to_string(m_selectedIndex));
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}
};

} // namespace mitiru::ui
