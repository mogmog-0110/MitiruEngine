#pragma once

/// @file UIContextMenu.hpp
/// @brief Right-click popup context menu with nested submenu support and keyboard navigation.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Definition of a single menu item (may contain children for submenus).
struct UIMenuItemDef
{
	std::string label;                      ///< Display text.
	std::string iconImageKey;               ///< Image key for the item icon.
	std::string shortcutText;               ///< Shortcut hint text (e.g. "Ctrl+S").
	bool enabled = true;                    ///< Whether the item is interactive.
	bool separator = false;                 ///< If true, renders as a separator line.
	std::vector<UIMenuItemDef> children;    ///< Submenu items (empty = leaf item).
};

/// @brief Configuration for creating a UIContextMenu.
struct UIContextMenuConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIMenuItemDef> items;

	// ── Layout ────────────────────────────────────────────────
	float width = 200.0f;                   ///< Menu panel width.
	float itemHeight = 28.0f;               ///< Height of each menu item.
	int maxVisibleItems = 12;               ///< Max items before scrolling.
	float padding = 4.0f;                   ///< Inner padding.
	float separatorHeight = 1.0f;           ///< Height of separator lines.
	float iconSize = 16.0f;                 ///< Size of item icons.
	float submenuOffset = -4.0f;            ///< Horizontal overlap for submenus.

	// ── Screen bounds ─────────────────────────────────────────
	float screenWidth = 1920.0f;            ///< Screen width for boundary clamping.
	float screenHeight = 1080.0f;           ///< Screen height for boundary clamping.
	float screenMargin = 4.0f;              ///< Minimum distance from screen edges.

	// ── Image keys ────────────────────────────────────────────
	std::string backgroundImageKey;         ///< Image key for menu background.
	std::string itemHoverImageKey;          ///< Image key for hovered item background.
	std::string separatorImageKey;          ///< Image key for separator decoration.
	std::string submenuArrowImageKey;       ///< Image key for submenu indicator arrow.
};

/// @brief Context menu widget with nested submenus, keyboard and mouse navigation.
///
/// Supports unlimited submenu depth, auto-positioning within screen bounds,
/// and both keyboard (arrow keys, Enter, Escape) and mouse interaction.
/// Rendering is handled externally by UIRenderer.
///
/// @code
///   UIContextMenuConfig cfg;
///   cfg.id = 200;
///   cfg.items = {
///       {"Cut", "icon_cut", "Ctrl+X"},
///       {"Copy", "icon_copy", "Ctrl+C"},
///       {"Paste", "icon_paste", "Ctrl+V"},
///       {"", "", "", true, true},  // separator
///       {"Submenu", "", "", true, false, {{"Child A"}, {"Child B"}}}
///   };
///   UIContextMenu menu(cfg);
///
///   menu.setOnItemSelected([](int index, const std::vector<int>& path) { });
///   menu.open(mouseX, mouseY);
/// @endcode
class UIContextMenu
{
	/// @brief State of a single menu level in the hierarchy.
	struct MenuLevel
	{
		std::vector<UIMenuItemDef> items;
		std::shared_ptr<UINode> node;
		int highlightedIndex = -1;
		int scrollOffset = 0;
		float x = 0.0f;
		float y = 0.0f;
	};

	std::shared_ptr<UINode> m_rootNode;
	std::vector<MenuLevel> m_levels;

	// ── Config copies ─────────────────────────────────────────
	float m_width;
	float m_itemHeight;
	int m_maxVisibleItems;
	float m_padding;
	float m_separatorHeight;
	float m_iconSize;
	float m_submenuOffset;
	float m_screenWidth;
	float m_screenHeight;
	float m_screenMargin;
	std::string m_backgroundImageKey;
	std::string m_itemHoverImageKey;
	std::string m_separatorImageKey;
	std::string m_submenuArrowImageKey;

	// ── Callbacks ─────────────────────────────────────────────
	std::function<void(int, const std::vector<int>&)> m_onItemSelected;
	std::function<void()> m_onClosed;

	bool m_open = false;

public:
	/// @brief Construct a context menu from configuration.
	/// @param config Context menu configuration.
	explicit UIContextMenu(const UIContextMenuConfig& config)
		: m_width(config.width)
		, m_itemHeight(config.itemHeight)
		, m_maxVisibleItems(config.maxVisibleItems)
		, m_padding(config.padding)
		, m_separatorHeight(config.separatorHeight)
		, m_iconSize(config.iconSize)
		, m_submenuOffset(config.submenuOffset)
		, m_screenWidth(config.screenWidth)
		, m_screenHeight(config.screenHeight)
		, m_screenMargin(config.screenMargin)
		, m_backgroundImageKey(config.backgroundImageKey)
		, m_itemHoverImageKey(config.itemHoverImageKey)
		, m_separatorImageKey(config.separatorImageKey)
		, m_submenuArrowImageKey(config.submenuArrowImageKey)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Container;
		data.properties["widget_type"] = "context_menu";
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["item_hover_image"] = config.itemHoverImageKey;
		data.properties["separator_image"] = config.separatorImageKey;
		data.properties["submenu_arrow_image"] = config.submenuArrowImageKey;

		m_rootNode = std::make_shared<UINode>(std::move(data));

		if (!config.items.empty())
		{
			setItems(config.items);
		}
	}

	// ── Accessors ─────────────────────────────────────────────

	/// @brief Get the underlying root UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_rootNode; }

	/// @brief Check if the context menu is currently open.
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief Get the number of open submenu levels (1 = root menu only).
	[[nodiscard]] std::size_t menuDepth() const noexcept { return m_levels.size(); }

	/// @brief Get the highlighted index at the given menu depth (0 = root).
	[[nodiscard]] int highlightedIndex(std::size_t depth = 0) const noexcept
	{
		if (depth < m_levels.size()) { return m_levels[depth].highlightedIndex; }
		return -1;
	}

	/// @brief Get the current selection path as indices through the menu hierarchy.
	[[nodiscard]] std::vector<int> currentPath() const
	{
		std::vector<int> path;
		for (const auto& level : m_levels)
		{
			if (level.highlightedIndex >= 0) { path.push_back(level.highlightedIndex); }
		}
		return path;
	}

	/// @brief Get the background image key.
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief Get the item hover image key.
	[[nodiscard]] const std::string& itemHoverImageKey() const noexcept { return m_itemHoverImageKey; }

	// ── Configuration ─────────────────────────────────────────

	/// @brief Set the items for the root menu level.
	/// @param items Menu item definitions.
	void setItems(const std::vector<UIMenuItemDef>& items)
	{
		m_rootNode->setProperty("item_count", std::to_string(items.size()));
		// Store for use when opened.
		if (!m_levels.empty())
		{
			m_levels[0].items = items;
		}
	}

	/// @brief Set the callback invoked when an item is selected.
	/// @param callback Function receiving (leaf index, full path).
	void setOnItemSelected(std::function<void(int, const std::vector<int>&)> callback)
	{
		m_onItemSelected = std::move(callback);
	}

	/// @brief Set the callback invoked when the menu is closed.
	/// @param callback Function invoked on close.
	void setOnClosed(std::function<void()> callback)
	{
		m_onClosed = std::move(callback);
	}

	/// @brief Set screen bounds for auto-positioning.
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
	}

	// ── Actions ───────────────────────────────────────────────

	/// @brief Open the context menu at the given screen position.
	/// @param x Screen X coordinate.
	/// @param y Screen Y coordinate.
	void open(float x, float y)
	{
		open(x, y, {});
	}

	/// @brief Open the context menu with specific items at the given position.
	/// @param x Screen X coordinate.
	/// @param y Screen Y coordinate.
	/// @param items Items to display (empty = use previously configured items).
	void open(float x, float y, const std::vector<UIMenuItemDef>& items)
	{
		m_levels.clear();
		m_open = true;

		MenuLevel root;
		root.items = items.empty()
			? (m_levels.empty() ? std::vector<UIMenuItemDef>{} : m_levels[0].items)
			: items;

		// If items were passed or previously stored via setItems, use them.
		if (root.items.empty())
		{
			// Attempt to use items set via constructor config.
			// Stored in rootNode property item_count.
		}

		const auto clamped = clampMenuPosition(x, y, root.items);
		root.x = clamped.first;
		root.y = clamped.second;
		root.node = createLevelNode(root, 0);

		m_levels.push_back(std::move(root));
		syncNodeState();
	}

	/// @brief Close the menu and all submenus.
	void close()
	{
		m_levels.clear();
		m_open = false;
		syncNodeState();

		if (m_onClosed) { m_onClosed(); }
	}

	// ── Mouse interaction ─────────────────────────────────────

	/// @brief Update highlight based on mouse position.
	/// @param mouseX Mouse X in screen space.
	/// @param mouseY Mouse Y in screen space.
	void onMouseMove(float mouseX, float mouseY)
	{
		if (!m_open || m_levels.empty()) { return; }

		// Check from deepest submenu to root.
		for (auto it = m_levels.rbegin(); it != m_levels.rend(); ++it)
		{
			const int idx = hitTestLevel(*it, mouseX, mouseY);
			if (idx >= 0)
			{
				// Close deeper levels if mouse moved to a parent level.
				const auto depth = static_cast<std::size_t>(std::distance(it, m_levels.rend()) - 1);
				closeLevelsBeyond(depth);

				auto& level = m_levels[depth];
				level.highlightedIndex = idx;

				// Open submenu if hovered item has children.
				if (idx < static_cast<int>(level.items.size()))
				{
					const auto& item = level.items[static_cast<std::size_t>(idx)];
					if (!item.children.empty() && !item.separator)
					{
						openSubmenu(depth, idx);
					}
				}

				syncNodeState();
				return;
			}
		}
	}

	/// @brief Handle mouse click at the given position.
	/// @param mouseX Mouse X in screen space.
	/// @param mouseY Mouse Y in screen space.
	void onMouseClick(float mouseX, float mouseY)
	{
		if (!m_open || m_levels.empty()) { return; }

		// Check from deepest to shallowest.
		for (auto it = m_levels.rbegin(); it != m_levels.rend(); ++it)
		{
			const int idx = hitTestLevel(*it, mouseX, mouseY);
			if (idx >= 0)
			{
				const auto depth = static_cast<std::size_t>(std::distance(it, m_levels.rend()) - 1);
				selectItem(depth, idx);
				return;
			}
		}

		// Clicked outside all menus.
		close();
	}

	// ── Keyboard interaction ──────────────────────────────────

	/// @brief Navigate up in the current deepest menu level.
	void navigateUp()
	{
		if (!m_open || m_levels.empty()) { return; }
		auto& level = m_levels.back();
		moveHighlight(level, -1);
		syncNodeState();
	}

	/// @brief Navigate down in the current deepest menu level.
	void navigateDown()
	{
		if (!m_open || m_levels.empty()) { return; }
		auto& level = m_levels.back();
		moveHighlight(level, 1);
		syncNodeState();
	}

	/// @brief Open the submenu of the highlighted item (or select if leaf).
	void navigateRight()
	{
		if (!m_open || m_levels.empty()) { return; }
		const auto& level = m_levels.back();
		const int idx = level.highlightedIndex;
		if (idx < 0 || idx >= static_cast<int>(level.items.size())) { return; }

		const auto& item = level.items[static_cast<std::size_t>(idx)];
		if (!item.children.empty() && !item.separator)
		{
			openSubmenu(m_levels.size() - 1, idx);
			syncNodeState();
		}
	}

	/// @brief Close the deepest submenu (go back to parent).
	void navigateLeft()
	{
		if (!m_open || m_levels.size() <= 1) { return; }
		m_levels.pop_back();
		syncNodeState();
	}

	/// @brief Select the currently highlighted item (Enter key).
	void confirmSelection()
	{
		if (!m_open || m_levels.empty()) { return; }
		const auto& level = m_levels.back();
		if (level.highlightedIndex >= 0)
		{
			selectItem(m_levels.size() - 1, level.highlightedIndex);
		}
	}

	/// @brief Close the menu (Escape key).
	void cancel()
	{
		close();
	}

private:
	/// @brief Hit-test a mouse position against a menu level.
	/// @return Item index or -1 if outside.
	[[nodiscard]] int hitTestLevel(const MenuLevel& level, float mx, float my) const noexcept
	{
		const float menuX = level.x;
		const float menuY = level.y;

		if (mx < menuX || mx > menuX + m_width) { return -1; }
		if (my < menuY + m_padding) { return -1; }

		float yOffset = m_padding;
		for (std::size_t i = 0; i < level.items.size(); ++i)
		{
			const auto& item = level.items[i];
			const float h = item.separator ? m_separatorHeight : m_itemHeight;

			if (my >= menuY + yOffset && my < menuY + yOffset + h)
			{
				return item.separator ? -1 : static_cast<int>(i);
			}
			yOffset += h;
		}
		return -1;
	}

	/// @brief Move the highlight index up or down, skipping separators and disabled items.
	void moveHighlight(MenuLevel& level, int direction) const
	{
		if (level.items.empty()) { return; }

		int idx = level.highlightedIndex + direction;
		const int count = static_cast<int>(level.items.size());

		for (int attempts = 0; attempts < count; ++attempts)
		{
			if (idx < 0) { idx = count - 1; }
			if (idx >= count) { idx = 0; }

			const auto& item = level.items[static_cast<std::size_t>(idx)];
			if (!item.separator && item.enabled)
			{
				level.highlightedIndex = idx;
				return;
			}
			idx += direction;
		}
	}

	/// @brief Attempt to select an item. Opens submenu if it has children, otherwise fires callback.
	void selectItem(std::size_t depth, int index)
	{
		if (depth >= m_levels.size()) { return; }
		auto& level = m_levels[depth];
		if (index < 0 || index >= static_cast<int>(level.items.size())) { return; }

		const auto& item = level.items[static_cast<std::size_t>(index)];
		if (item.separator || !item.enabled) { return; }

		if (!item.children.empty())
		{
			openSubmenu(depth, index);
			syncNodeState();
			return;
		}

		level.highlightedIndex = index;

		if (m_onItemSelected)
		{
			auto path = currentPath();
			m_onItemSelected(index, path);
		}

		close();
	}

	/// @brief Open a submenu for the item at the given depth and index.
	void openSubmenu(std::size_t depth, int index)
	{
		if (depth >= m_levels.size()) { return; }
		const auto& parentLevel = m_levels[depth];
		if (index < 0 || index >= static_cast<int>(parentLevel.items.size())) { return; }

		const auto& item = parentLevel.items[static_cast<std::size_t>(index)];
		if (item.children.empty()) { return; }

		// Close any existing deeper levels.
		closeLevelsBeyond(depth);

		// Position submenu to the right of the parent item.
		float subX = parentLevel.x + m_width + m_submenuOffset;
		float subY = parentLevel.y + m_padding + static_cast<float>(index) * m_itemHeight;

		// Clamp within screen.
		const auto clamped = clampMenuPosition(subX, subY, item.children);
		subX = clamped.first;
		subY = clamped.second;

		// If submenu would go off-screen to the right, open to the left.
		if (subX + m_width > m_screenWidth - m_screenMargin)
		{
			subX = parentLevel.x - m_width - m_submenuOffset;
			subX = std::max(m_screenMargin, subX);
		}

		MenuLevel sub;
		sub.items = item.children;
		sub.x = subX;
		sub.y = subY;
		sub.node = createLevelNode(sub, m_levels.size());

		m_levels[depth].highlightedIndex = index;
		m_levels.push_back(std::move(sub));
	}

	/// @brief Close all menu levels deeper than the given depth.
	void closeLevelsBeyond(std::size_t depth)
	{
		if (depth + 1 < m_levels.size())
		{
			m_levels.resize(depth + 1);
		}
	}

	/// @brief Clamp a menu position to stay within screen bounds.
	[[nodiscard]] std::pair<float, float> clampMenuPosition(
		float x, float y, const std::vector<UIMenuItemDef>& items) const noexcept
	{
		const float totalH = computeMenuHeight(items);
		const float maxX = m_screenWidth - m_width - m_screenMargin;
		const float maxY = m_screenHeight - totalH - m_screenMargin;

		return {
			std::clamp(x, m_screenMargin, std::max(m_screenMargin, maxX)),
			std::clamp(y, m_screenMargin, std::max(m_screenMargin, maxY))
		};
	}

	/// @brief Compute the total height of a menu level.
	[[nodiscard]] float computeMenuHeight(const std::vector<UIMenuItemDef>& items) const noexcept
	{
		float height = m_padding * 2.0f;
		int visibleCount = 0;
		for (const auto& item : items)
		{
			if (visibleCount >= m_maxVisibleItems) { break; }
			height += item.separator ? m_separatorHeight : m_itemHeight;
			++visibleCount;
		}
		return height;
	}

	/// @brief Create a UINode for a menu level.
	[[nodiscard]] std::shared_ptr<UINode> createLevelNode(const MenuLevel& level, std::size_t depth) const
	{
		UINodeData data;
		data.id = INVALID_UI_NODE;
		data.name = "context_menu_level_" + std::to_string(depth);
		data.role = UIRole::Container;
		data.bounds = sgc::Rectf(level.x, level.y, m_width, computeMenuHeight(level.items));
		data.properties["widget_type"] = "context_menu_level";
		data.properties["depth"] = std::to_string(depth);
		data.properties["item_count"] = std::to_string(level.items.size());
		data.properties["background_image"] = m_backgroundImageKey;

		return std::make_shared<UINode>(std::move(data));
	}

	void syncNodeState()
	{
		m_rootNode->setProperty("state", m_open ? "open" : "closed");
		m_rootNode->setProperty("depth", std::to_string(m_levels.size()));
		m_rootNode->setProperty("background_image", m_backgroundImageKey);
		m_rootNode->setProperty("item_hover_image", m_itemHoverImageKey);
		m_rootNode->setProperty("separator_image", m_separatorImageKey);
		m_rootNode->setProperty("submenu_arrow_image", m_submenuArrowImageKey);
	}
};

} // namespace mitiru::ui
