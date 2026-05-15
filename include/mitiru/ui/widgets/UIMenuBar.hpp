#pragma once

/// @file UIMenuBar.hpp
/// @brief Horizontal menu bar with dropdown menus (File, Edit, View, etc.).

#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/widgets/UIContextMenu.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Definition of a top-level menu in the menu bar.
struct UIMenuDef
{
	std::string label;                      ///< Menu label (e.g. "File").
	std::vector<UIMenuItemDef> items;       ///< Dropdown items.
	bool enabled = true;                    ///< Whether the menu is interactive.
};

/// @brief Configuration for creating a UIMenuBar.
struct UIMenuBarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIMenuDef> menus;

	// ── Layout ────────────────────────────────────────────────
	float height = 28.0f;                   ///< Bar height.
	float itemPaddingX = 12.0f;             ///< Horizontal padding inside each menu label.
	float itemPaddingY = 4.0f;              ///< Vertical padding inside each menu label.
	float fontSize = 14.0f;                 ///< Font size for menu labels.
	float barWidth = 0.0f;                  ///< Total bar width (0 = auto/full width).

	// ── Dropdown config ───────────────────────────────────────
	float dropdownWidth = 200.0f;           ///< Width of dropdown menus.
	float dropdownItemHeight = 28.0f;       ///< Height of dropdown items.
	int dropdownMaxVisibleItems = 12;       ///< Max visible dropdown items before scroll.
	float dropdownPadding = 4.0f;           ///< Dropdown inner padding.

	// ── Screen bounds ─────────────────────────────────────────
	float screenWidth = 1920.0f;
	float screenHeight = 1080.0f;

	// ── Image keys ────────────────────────────────────────────
	std::string backgroundImageKey;         ///< Image key for the bar background.
	std::string activeItemImageKey;         ///< Image key for the active/selected menu label.
	std::string hoverItemImageKey;          ///< Image key for the hovered menu label.
	std::string dropdownBackgroundImageKey; ///< Image key for dropdown background.
	std::string dropdownItemHoverImageKey;  ///< Image key for hovered dropdown item.
	std::string dropdownSeparatorImageKey;  ///< Image key for dropdown separator.
};

/// @brief Horizontal menu bar widget with dropdown menus.
///
/// Manages top-level menu labels and their associated dropdown menus (UIContextMenu).
/// Supports hover-to-switch when a dropdown is open, Alt key activation,
/// and full keyboard navigation. Rendering is handled externally by UIRenderer.
///
/// @code
///   UIMenuBarConfig cfg;
///   cfg.id = 300;
///   cfg.menus = {
///       {"File", {{"New", "", "Ctrl+N"}, {"Open", "", "Ctrl+O"}, {"Save", "", "Ctrl+S"}}},
///       {"Edit", {{"Undo", "", "Ctrl+Z"}, {"Redo", "", "Ctrl+Y"}}},
///   };
///   UIMenuBar menuBar(cfg);
///
///   menuBar.setOnMenuItemSelected([](int menuIdx, int itemIdx) { });
///   menuBar.update(mouseX, mouseY, mouseClicked, altPressed);
/// @endcode
class UIMenuBar
{
	/// @brief Runtime state of a single top-level menu entry.
	struct MenuEntry
	{
		UIMenuDef def;
		float x = 0.0f;           ///< Left edge of the label area.
		float width = 0.0f;       ///< Width of the label area.
		bool hovered = false;
	};

	std::shared_ptr<UINode> m_node;
	std::vector<MenuEntry> m_entries;
	UIContextMenu m_dropdown;

	// ── Config copies ─────────────────────────────────────────
	float m_height;
	float m_itemPaddingX;
	float m_itemPaddingY;
	float m_fontSize;
	float m_barWidth;
	float m_screenWidth;
	float m_screenHeight;
	std::string m_backgroundImageKey;
	std::string m_activeItemImageKey;
	std::string m_hoverItemImageKey;

	// ── Runtime state ─────────────────────────────────────────
	int m_activeMenuIndex = -1;     ///< Currently open menu index (-1 = none).
	int m_hoveredMenuIndex = -1;    ///< Currently hovered menu label index.
	bool m_activated = false;       ///< Whether the bar is in activated state (Alt pressed).

	// ── Callbacks ─────────────────────────────────────────────
	std::function<void(int, int)> m_onMenuItemSelected;

public:
	/// @brief Construct a menu bar from configuration.
	/// @param config Menu bar configuration.
	explicit UIMenuBar(const UIMenuBarConfig& config)
		: m_dropdown(buildDropdownConfig(config))
		, m_height(config.height)
		, m_itemPaddingX(config.itemPaddingX)
		, m_itemPaddingY(config.itemPaddingY)
		, m_fontSize(config.fontSize)
		, m_barWidth(config.barWidth)
		, m_screenWidth(config.screenWidth)
		, m_screenHeight(config.screenHeight)
		, m_backgroundImageKey(config.backgroundImageKey)
		, m_activeItemImageKey(config.activeItemImageKey)
		, m_hoverItemImageKey(config.hoverItemImageKey)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Container;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.barWidth, config.height);
		data.properties["widget_type"] = "menu_bar";
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["active_item_image"] = config.activeItemImageKey;
		data.properties["hover_item_image"] = config.hoverItemImageKey;
		data.properties["font_size"] = std::to_string(config.fontSize);

		m_node = std::make_shared<UINode>(std::move(data));

		// Build entries from config.
		for (const auto& menuDef : config.menus)
		{
			addMenuInternal(menuDef);
		}

		// Wire dropdown callback to route through our callback.
		m_dropdown.setOnItemSelected([this](int itemIdx, const std::vector<int>& /*path*/) {
			if (m_onMenuItemSelected && m_activeMenuIndex >= 0)
			{
				m_onMenuItemSelected(m_activeMenuIndex, itemIdx);
			}
		});

		m_dropdown.setOnClosed([this]() {
			m_activeMenuIndex = -1;
			syncNodeState();
		});

		syncNodeState();
	}

	// ── Accessors ─────────────────────────────────────────────

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the dropdown context menu (for rendering access).
	[[nodiscard]] const UIContextMenu& dropdown() const noexcept { return m_dropdown; }

	/// @brief Check if any dropdown menu is currently open.
	[[nodiscard]] bool isMenuOpen() const noexcept { return m_activeMenuIndex >= 0 && m_dropdown.isOpen(); }

	/// @brief Get the index of the currently open menu (-1 = none).
	[[nodiscard]] int activeMenuIndex() const noexcept { return m_activeMenuIndex; }

	/// @brief Get the index of the currently hovered menu label (-1 = none).
	[[nodiscard]] int hoveredMenuIndex() const noexcept { return m_hoveredMenuIndex; }

	/// @brief Get the number of top-level menus.
	[[nodiscard]] std::size_t menuCount() const noexcept { return m_entries.size(); }

	/// @brief Check if the menu bar is in activated mode (Alt pressed).
	[[nodiscard]] bool isActivated() const noexcept { return m_activated; }

	/// @brief Get the bar height.
	[[nodiscard]] float height() const noexcept { return m_height; }

	/// @brief Get the background image key.
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief Get the active item image key.
	[[nodiscard]] const std::string& activeItemImageKey() const noexcept { return m_activeItemImageKey; }

	/// @brief Get a menu entry label and bounds for rendering.
	/// @param index Menu index.
	/// @return Pair of (label, Rectf bounds) or empty if invalid.
	struct MenuLabelInfo
	{
		std::string label;
		sgc::Rectf bounds;
		bool hovered = false;
		bool active = false;
		bool enabled = true;
	};

	[[nodiscard]] MenuLabelInfo menuLabelInfo(std::size_t index) const noexcept
	{
		if (index >= m_entries.size()) { return {}; }
		const auto& entry = m_entries[index];
		return {
			entry.def.label,
			sgc::Rectf(entry.x, 0.0f, entry.width, m_height),
			entry.hovered,
			static_cast<int>(index) == m_activeMenuIndex,
			entry.def.enabled
		};
	}

	// ── Configuration ─────────────────────────────────────────

	/// @brief Add a menu to the bar.
	/// @param label Menu label.
	/// @param items Dropdown items.
	void addMenu(const std::string& label, const std::vector<UIMenuItemDef>& items)
	{
		UIMenuDef def;
		def.label = label;
		def.items = items;
		addMenuInternal(def);
		syncNodeState();
	}

	/// @brief Set the callback invoked when a dropdown item is selected.
	/// @param callback Function receiving (menuIndex, itemIndex).
	void setOnMenuItemSelected(std::function<void(int, int)> callback)
	{
		m_onMenuItemSelected = std::move(callback);
	}

	/// @brief Set estimated label width for a specific menu (called by layout/renderer).
	/// @param index Menu index.
	/// @param width Label width.
	void setMenuLabelWidth(std::size_t index, float width)
	{
		if (index >= m_entries.size()) { return; }
		m_entries[index].width = width + m_itemPaddingX * 2.0f;
		recalculateLayout();
	}

	/// @brief Set screen bounds for dropdown positioning.
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
		m_dropdown.setScreenBounds(width, height);
	}

	// ── Input handling ────────────────────────────────────────

	/// @brief Process mouse movement.
	/// @param mouseX Mouse X in screen space.
	/// @param mouseY Mouse Y in screen space.
	void onMouseMove(float mouseX, float mouseY)
	{
		// Forward to dropdown if open.
		if (m_dropdown.isOpen())
		{
			m_dropdown.onMouseMove(mouseX, mouseY);
		}

		// Check hover over bar labels.
		m_hoveredMenuIndex = -1;
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			auto& entry = m_entries[i];
			const bool wasHovered = entry.hovered;
			entry.hovered = mouseY >= 0.0f && mouseY < m_height
				&& mouseX >= entry.x && mouseX < entry.x + entry.width;

			if (entry.hovered)
			{
				m_hoveredMenuIndex = static_cast<int>(i);

				// Hover-to-switch: if a dropdown is open and we hover a different label, switch.
				if (isMenuOpen() && m_activeMenuIndex != static_cast<int>(i) && entry.def.enabled)
				{
					openDropdown(static_cast<int>(i));
				}
			}

			if (wasHovered != entry.hovered)
			{
				syncNodeState();
			}
		}
	}

	/// @brief Process mouse click.
	/// @param mouseX Mouse X in screen space.
	/// @param mouseY Mouse Y in screen space.
	void onMouseClick(float mouseX, float mouseY)
	{
		// Check if click is on a bar label.
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			const auto& entry = m_entries[i];
			if (mouseY >= 0.0f && mouseY < m_height
				&& mouseX >= entry.x && mouseX < entry.x + entry.width)
			{
				if (!entry.def.enabled) { return; }

				if (m_activeMenuIndex == static_cast<int>(i) && m_dropdown.isOpen())
				{
					// Toggle: close if already open.
					m_dropdown.close();
					m_activeMenuIndex = -1;
				}
				else
				{
					openDropdown(static_cast<int>(i));
				}
				syncNodeState();
				return;
			}
		}

		// Forward click to dropdown.
		if (m_dropdown.isOpen())
		{
			m_dropdown.onMouseClick(mouseX, mouseY);
		}
	}

	// ── Keyboard navigation ───────────────────────────────────

	/// @brief Toggle activation of the menu bar (Alt key).
	void toggleActivation()
	{
		m_activated = !m_activated;
		if (m_activated && !m_entries.empty())
		{
			m_hoveredMenuIndex = 0;
		}
		else
		{
			m_hoveredMenuIndex = -1;
			if (m_dropdown.isOpen())
			{
				m_dropdown.close();
				m_activeMenuIndex = -1;
			}
		}
		syncNodeState();
	}

	/// @brief Navigate to the next menu label (Right arrow when activated).
	void navigateNextMenu()
	{
		if (m_entries.empty()) { return; }

		const int current = isMenuOpen() ? m_activeMenuIndex : m_hoveredMenuIndex;
		int next = current + 1;
		if (next >= static_cast<int>(m_entries.size())) { next = 0; }

		// Skip disabled menus.
		const int start = next;
		do
		{
			if (m_entries[static_cast<std::size_t>(next)].def.enabled) { break; }
			next = (next + 1) % static_cast<int>(m_entries.size());
		} while (next != start);

		m_hoveredMenuIndex = next;
		if (isMenuOpen())
		{
			openDropdown(next);
		}
		syncNodeState();
	}

	/// @brief Navigate to the previous menu label (Left arrow when activated).
	void navigatePreviousMenu()
	{
		if (m_entries.empty()) { return; }

		const int current = isMenuOpen() ? m_activeMenuIndex : m_hoveredMenuIndex;
		int prev = current - 1;
		if (prev < 0) { prev = static_cast<int>(m_entries.size()) - 1; }

		const int start = prev;
		do
		{
			if (m_entries[static_cast<std::size_t>(prev)].def.enabled) { break; }
			prev = (prev - 1 + static_cast<int>(m_entries.size())) % static_cast<int>(m_entries.size());
		} while (prev != start);

		m_hoveredMenuIndex = prev;
		if (isMenuOpen())
		{
			openDropdown(prev);
		}
		syncNodeState();
	}

	/// @brief Open the dropdown for the currently hovered menu (Enter/Down arrow).
	void openHoveredMenu()
	{
		if (m_hoveredMenuIndex >= 0 && m_hoveredMenuIndex < static_cast<int>(m_entries.size()))
		{
			openDropdown(m_hoveredMenuIndex);
			syncNodeState();
		}
	}

	/// @brief Forward keyboard navigation to the open dropdown.
	void dropdownNavigateUp()    { m_dropdown.navigateUp(); }
	void dropdownNavigateDown()  { m_dropdown.navigateDown(); }
	void dropdownNavigateRight() { m_dropdown.navigateRight(); }
	void dropdownNavigateLeft()  { m_dropdown.navigateLeft(); }
	void dropdownConfirm()       { m_dropdown.confirmSelection(); }
	void dropdownCancel()        { m_dropdown.cancel(); m_activeMenuIndex = -1; syncNodeState(); }

private:
	void addMenuInternal(const UIMenuDef& menuDef)
	{
		MenuEntry entry;
		entry.def = menuDef;
		// Default width estimate; caller should use setMenuLabelWidth for accurate sizing.
		entry.width = static_cast<float>(menuDef.label.size()) * m_fontSize * 0.6f + m_itemPaddingX * 2.0f;
		m_entries.push_back(std::move(entry));
		recalculateLayout();
	}

	void recalculateLayout()
	{
		float xOffset = 0.0f;
		for (auto& entry : m_entries)
		{
			entry.x = xOffset;
			xOffset += entry.width;
		}
		m_barWidth = xOffset;
		m_node->setBounds(sgc::Rectf(0.0f, 0.0f, m_barWidth, m_height));
	}

	void openDropdown(int menuIndex)
	{
		if (menuIndex < 0 || menuIndex >= static_cast<int>(m_entries.size())) { return; }
		const auto& entry = m_entries[static_cast<std::size_t>(menuIndex)];

		m_activeMenuIndex = menuIndex;
		const float dropX = entry.x;
		const float dropY = m_height;

		m_dropdown.open(dropX, dropY, entry.def.items);
	}

	[[nodiscard]] static UIContextMenuConfig buildDropdownConfig(const UIMenuBarConfig& config)
	{
		UIContextMenuConfig dropCfg;
		dropCfg.width = config.dropdownWidth;
		dropCfg.itemHeight = config.dropdownItemHeight;
		dropCfg.maxVisibleItems = config.dropdownMaxVisibleItems;
		dropCfg.padding = config.dropdownPadding;
		dropCfg.screenWidth = config.screenWidth;
		dropCfg.screenHeight = config.screenHeight;
		dropCfg.backgroundImageKey = config.dropdownBackgroundImageKey;
		dropCfg.itemHoverImageKey = config.dropdownItemHoverImageKey;
		dropCfg.separatorImageKey = config.dropdownSeparatorImageKey;
		return dropCfg;
	}

	void syncNodeState()
	{
		m_node->setProperty("state", isMenuOpen() ? "open" : (m_activated ? "activated" : "normal"));
		m_node->setProperty("active_menu", std::to_string(m_activeMenuIndex));
		m_node->setProperty("hovered_menu", std::to_string(m_hoveredMenuIndex));
		m_node->setProperty("menu_count", std::to_string(m_entries.size()));
		m_node->setProperty("background_image", m_backgroundImageKey);
		m_node->setProperty("active_item_image", m_activeItemImageKey);
		m_node->setProperty("hover_item_image", m_hoverItemImageKey);
	}
};

} // namespace mitiru::ui
