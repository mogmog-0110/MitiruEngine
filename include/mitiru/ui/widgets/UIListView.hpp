#pragma once

/// @file UIListView.hpp
/// @brief Scrollable list view widget with virtual scrolling and selection.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Data model for a single list view item.
struct UIListItem
{
	std::string text;            ///< Display text.
	std::string icon;            ///< Icon identifier (renderer-interpreted).
	std::any userData;           ///< Arbitrary user data.
};

/// @brief Configuration for creating a UIListView.
struct UIListViewConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float itemHeight = 28.0f;
	int visibleItems = 8;
	bool selectable = true;
	bool multiSelect = false;
	float width = 250.0f;
};

/// @brief List view widget with virtual scrolling and single/multi selection.
///
/// Only visible items are exposed via UINode children, enabling efficient
/// rendering of large data sets. The scroll offset determines which slice
/// of the data model is materialized.
///
/// @code
///   UIListViewConfig cfg;
///   cfg.id = 60;
///   cfg.visibleItems = 5;
///   cfg.multiSelect = true;
///   UIListView list(cfg);
///
///   list.addItem(UIListItem{"Sword", "icon_sword", {}});
///   list.addItem(UIListItem{"Shield", "icon_shield", {}});
///   list.setOnItemSelected([](int idx) { /* handle */ });
///   list.selectItem(0);
/// @endcode
class UIListView
{
	std::shared_ptr<UINode> m_node;
	std::vector<UIListItem> m_items;
	std::set<int> m_selectedIndices;
	int m_focusedIndex = -1;
	float m_itemHeight;
	int m_visibleItems;
	bool m_selectable;
	bool m_multiSelect;
	int m_scrollOffset = 0;
	std::function<void(int)> m_onItemSelected;

public:
	/// @brief Construct a list view from configuration.
	/// @param config List view configuration.
	explicit UIListView(const UIListViewConfig& config)
		: m_itemHeight(config.itemHeight)
		, m_visibleItems(config.visibleItems)
		, m_selectable(config.selectable)
		, m_multiSelect(config.multiSelect)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::ListView;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.itemHeight * config.visibleItems);
		data.properties["widget_type"] = "list_view";
		data.properties["item_height"] = std::to_string(config.itemHeight);
		data.properties["selectable"] = config.selectable ? "true" : "false";
		data.properties["multi_select"] = config.multiSelect ? "true" : "false";

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the total item count.
	[[nodiscard]] std::size_t itemCount() const noexcept { return m_items.size(); }

	/// @brief Get the items data model.
	[[nodiscard]] const std::vector<UIListItem>& items() const noexcept { return m_items; }

	/// @brief Get the set of selected indices.
	[[nodiscard]] const std::set<int>& selectedIndices() const noexcept { return m_selectedIndices; }

	/// @brief Get the single selected index (first in set, or -1).
	[[nodiscard]] int selectedIndex() const noexcept
	{
		return m_selectedIndices.empty() ? -1 : *m_selectedIndices.begin();
	}

	/// @brief Get the focused (keyboard-navigated) index.
	[[nodiscard]] int focusedIndex() const noexcept { return m_focusedIndex; }

	/// @brief Get the current scroll offset (first visible item index).
	[[nodiscard]] int scrollOffset() const noexcept { return m_scrollOffset; }

	/// @brief Get the range of currently visible item indices [start, end).
	[[nodiscard]] std::pair<int, int> visibleRange() const noexcept
	{
		const int start = m_scrollOffset;
		const int end = std::min(m_scrollOffset + m_visibleItems, static_cast<int>(m_items.size()));
		return {start, end};
	}

	// ── Data Manipulation ────────────────────────────────────

	/// @brief Add an item to the end of the list.
	/// @param item Item to add.
	void addItem(UIListItem item)
	{
		m_items.push_back(std::move(item));
		syncNodeState();
	}

	/// @brief Insert an item at a specific index.
	/// @param index Position to insert at.
	/// @param item Item to insert.
	void insertItem(std::size_t index, UIListItem item)
	{
		if (index > m_items.size()) { index = m_items.size(); }
		m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
		syncNodeState();
	}

	/// @brief Remove an item by index.
	/// @param index Item index to remove.
	void removeItem(std::size_t index)
	{
		if (index >= m_items.size()) { return; }
		m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));
		m_selectedIndices.erase(static_cast<int>(index));
		syncNodeState();
	}

	/// @brief Clear all items.
	void clearItems()
	{
		m_items.clear();
		m_selectedIndices.clear();
		m_focusedIndex = -1;
		m_scrollOffset = 0;
		syncNodeState();
	}

	/// @brief Set the complete item list.
	/// @param items New item vector.
	void setItems(std::vector<UIListItem> items)
	{
		m_items = std::move(items);
		m_selectedIndices.clear();
		m_focusedIndex = -1;
		m_scrollOffset = 0;
		syncNodeState();
	}

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the item-selected callback.
	void setOnItemSelected(std::function<void(int)> callback) { m_onItemSelected = std::move(callback); }

	// ── Selection ────────────────────────────────────────────

	/// @brief Select an item by index.
	/// @param index Item index.
	/// @param addToSelection If true and multiSelect enabled, adds to current selection.
	void selectItem(int index, bool addToSelection = false)
	{
		if (!m_selectable) { return; }
		if (index < 0 || index >= static_cast<int>(m_items.size())) { return; }

		if (!m_multiSelect || !addToSelection)
		{
			m_selectedIndices.clear();
		}
		m_selectedIndices.insert(index);
		m_focusedIndex = index;
		syncNodeState();
		if (m_onItemSelected) { m_onItemSelected(index); }
	}

	/// @brief Deselect an item by index.
	/// @param index Item index.
	void deselectItem(int index)
	{
		m_selectedIndices.erase(index);
		syncNodeState();
	}

	/// @brief Clear all selections.
	void clearSelection()
	{
		m_selectedIndices.clear();
		syncNodeState();
	}

	/// @brief Check if an item is selected.
	/// @param index Item index.
	[[nodiscard]] bool isSelected(int index) const
	{
		return m_selectedIndices.count(index) > 0;
	}

	// ── Navigation ───────────────────────────────────────────

	/// @brief Move focus to the previous item (Up key).
	void focusPrevious()
	{
		if (m_items.empty()) { return; }
		m_focusedIndex = (m_focusedIndex <= 0)
			? static_cast<int>(m_items.size()) - 1
			: m_focusedIndex - 1;
		ensureFocusedVisible();
		syncNodeState();
	}

	/// @brief Move focus to the next item (Down key).
	void focusNext()
	{
		if (m_items.empty()) { return; }
		m_focusedIndex = (m_focusedIndex >= static_cast<int>(m_items.size()) - 1)
			? 0
			: m_focusedIndex + 1;
		ensureFocusedVisible();
		syncNodeState();
	}

	/// @brief Select the currently focused item (Enter key).
	void confirmFocused()
	{
		if (m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size()))
		{
			selectItem(m_focusedIndex);
		}
	}

	/// @brief Scroll by a number of items.
	/// @param delta Positive = scroll down, negative = scroll up.
	void scroll(int delta)
	{
		const int maxScroll = std::max(0, static_cast<int>(m_items.size()) - m_visibleItems);
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxScroll);
		syncNodeState();
	}

private:
	/// @brief Ensure the focused item is within the visible scroll range.
	void ensureFocusedVisible()
	{
		if (m_focusedIndex < m_scrollOffset)
		{
			m_scrollOffset = m_focusedIndex;
		}
		else if (m_focusedIndex >= m_scrollOffset + m_visibleItems)
		{
			m_scrollOffset = m_focusedIndex - m_visibleItems + 1;
		}
	}

	/// @brief Synchronize state to the UINode properties.
	void syncNodeState()
	{
		m_node->setProperty("item_count", std::to_string(m_items.size()));
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
		m_node->setProperty("focused", std::to_string(m_focusedIndex));

		// Encode selected indices as comma-separated string.
		std::string selStr;
		for (const auto idx : m_selectedIndices)
		{
			if (!selStr.empty()) { selStr += ","; }
			selStr += std::to_string(idx);
		}
		m_node->setProperty("selected", selStr);

		// Encode visible item texts for renderer convenience.
		const auto [start, end] = visibleRange();
		m_node->setProperty("visible_start", std::to_string(start));
		m_node->setProperty("visible_end", std::to_string(end));

		for (int i = start; i < end; ++i)
		{
			const auto key = "item_" + std::to_string(i - start);
			m_node->setProperty(key, m_items[static_cast<std::size_t>(i)].text);
			m_node->setProperty(key + "_icon", m_items[static_cast<std::size_t>(i)].icon);
		}
	}
};

} // namespace mitiru::ui
