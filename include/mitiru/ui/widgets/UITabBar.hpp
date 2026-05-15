#pragma once

/// @file UITabBar.hpp
/// @brief Tab bar widget for switching between content panels.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Position of the tab bar relative to content.
enum class TabBarOrientation : std::uint8_t
{
	Top,
	Bottom,
	Left,
	Right
};

/// @brief Configuration for creating a UITabBar.
struct UITabBarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<std::string> tabs;
	int selectedIndex = 0;
	TabBarOrientation orientation = TabBarOrientation::Top;
	float tabWidth = 0.0f;    ///< 0 = auto-size from text.
	float tabHeight = 32.0f;
	float totalWidth = 400.0f; ///< Total bar width (horizontal) or height (vertical).
};

/// @brief Tab bar widget that wraps a UINode with tab switching logic.
///
/// Manages which tab is active and notifies via callback on change.
/// Individual tab nodes are created as children, with the active one
/// marked for the renderer to highlight.
///
/// @code
///   UITabBarConfig cfg;
///   cfg.id = 70;
///   cfg.tabs = {"General", "Audio", "Video", "Controls"};
///   cfg.selectedIndex = 0;
///   UITabBar tabBar(cfg);
///
///   tabBar.setOnTabChanged([](int idx) { /* switch content */ });
///   tabBar.selectTab(2);
/// @endcode
class UITabBar
{
	std::shared_ptr<UINode> m_node;
	std::vector<std::string> m_tabs;
	int m_selectedIndex;
	TabBarOrientation m_orientation;
	std::function<void(int)> m_onTabChanged;

public:
	/// @brief Construct a tab bar from configuration.
	/// @param config Tab bar configuration.
	explicit UITabBar(const UITabBarConfig& config)
		: m_tabs(config.tabs)
		, m_selectedIndex(config.selectedIndex)
		, m_orientation(config.orientation)
	{
		const bool horizontal = (m_orientation == TabBarOrientation::Top ||
		                         m_orientation == TabBarOrientation::Bottom);

		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::TabBar;
		data.properties["widget_type"] = "tab_bar";
		data.properties["orientation"] = orientationToString(m_orientation);
		data.properties["tab_count"] = std::to_string(m_tabs.size());

		if (horizontal)
		{
			data.bounds = sgc::Rectf(0.0f, 0.0f, config.totalWidth, config.tabHeight);
		}
		else
		{
			const float tabW = (config.tabWidth > 0.0f) ? config.tabWidth : 100.0f;
			data.bounds = sgc::Rectf(0.0f, 0.0f, tabW, config.totalWidth);
		}

		m_node = std::make_shared<UINode>(std::move(data));

		// Create child nodes for each tab.
		for (std::size_t i = 0; i < m_tabs.size(); ++i)
		{
			UINodeData tabData;
			tabData.id = config.id + static_cast<UINodeId>(i) + 1;
			tabData.name = config.name + "_tab_" + std::to_string(i);
			tabData.role = UIRole::MenuItem;
			tabData.text = m_tabs[i];
			tabData.properties["tab_index"] = std::to_string(i);
			m_node->addChild(std::make_shared<UINode>(std::move(tabData)));
		}

		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the currently selected tab index.
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief Get the text of the currently selected tab.
	[[nodiscard]] const std::string& selectedTabText() const
	{
		static const std::string empty;
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_tabs.size()))
		{
			return m_tabs[static_cast<std::size_t>(m_selectedIndex)];
		}
		return empty;
	}

	/// @brief Get the tab labels.
	[[nodiscard]] const std::vector<std::string>& tabs() const noexcept { return m_tabs; }

	/// @brief Get the tab count.
	[[nodiscard]] std::size_t tabCount() const noexcept { return m_tabs.size(); }

	// ── Configuration ────────────────────────────────────────

	/// @brief Set the tab-changed callback.
	void setOnTabChanged(std::function<void(int)> callback) { m_onTabChanged = std::move(callback); }

	/// @brief Replace the tab list.
	/// @param tabs New tab labels.
	void setTabs(std::vector<std::string> tabs)
	{
		m_tabs = std::move(tabs);

		if (m_selectedIndex >= static_cast<int>(m_tabs.size()))
		{
			m_selectedIndex = m_tabs.empty() ? -1 : 0;
		}

		m_node->setProperty("tab_count", std::to_string(m_tabs.size()));
		syncNodeState();
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief Select a tab by index.
	/// @param index Tab index to select.
	void selectTab(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_tabs.size())) { return; }
		if (m_selectedIndex == index) { return; }

		m_selectedIndex = index;
		syncNodeState();
		if (m_onTabChanged) { m_onTabChanged(m_selectedIndex); }
	}

	/// @brief Select the next tab (wraps around).
	void selectNext()
	{
		if (m_tabs.empty()) { return; }
		selectTab((m_selectedIndex + 1) % static_cast<int>(m_tabs.size()));
	}

	/// @brief Select the previous tab (wraps around).
	void selectPrevious()
	{
		if (m_tabs.empty()) { return; }
		const int count = static_cast<int>(m_tabs.size());
		selectTab((m_selectedIndex - 1 + count) % count);
	}

private:
	/// @brief Convert orientation enum to string.
	[[nodiscard]] static const char* orientationToString(TabBarOrientation o) noexcept
	{
		switch (o)
		{
		case TabBarOrientation::Top:    return "top";
		case TabBarOrientation::Bottom: return "bottom";
		case TabBarOrientation::Left:   return "left";
		case TabBarOrientation::Right:  return "right";
		}
		return "top";
	}

	/// @brief Synchronize active tab state to the UINode tree.
	void syncNodeState()
	{
		m_node->setProperty("selected", std::to_string(m_selectedIndex));
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_tabs.size()))
		{
			m_node->setText(m_tabs[static_cast<std::size_t>(m_selectedIndex)]);
		}

		// Mark active/inactive on child tab nodes.
		const auto& children = m_node->children();
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const bool active = (static_cast<int>(i) == m_selectedIndex);
			children[i]->setProperty("active", active ? "true" : "false");
		}
	}
};

} // namespace mitiru::ui
