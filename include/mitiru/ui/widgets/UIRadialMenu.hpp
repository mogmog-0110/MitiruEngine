#pragma once

/// @file UIRadialMenu.hpp
/// @brief Circular/pie radial menu widget for quick item selection.

#include <mitiru/ui/Easing.hpp>
#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Animation type for opening/closing the radial menu.
enum class RadialMenuAnimation : std::uint8_t
{
	Scale, ///< Scale from center outward.
	Fade,  ///< Fade in/out.
};

/// @brief A single item in the radial menu.
struct UIRadialMenuItem
{
	std::string label;                             ///< Display label.
	std::string iconImageKey;                      ///< Icon image key for the renderer.
	bool enabled            = true;                ///< Whether this item can be selected.
	std::string shortcutKey;                       ///< Keyboard shortcut (e.g. "1", "Q").
	std::vector<UIRadialMenuItem> subItems;        ///< Nested sub-items for a second ring.
};

/// @brief Configuration for the radial menu.
struct UIRadialMenuConfig
{
	float innerRadius           = 40.0f;           ///< Inner radius (dead zone boundary).
	float outerRadius           = 150.0f;          ///< Outer radius.
	std::vector<UIRadialMenuItem> items;            ///< Menu items (sectors).
	std::string centerIconImageKey;                ///< Center area icon image key.
	std::string sectorBackgroundImageKey;          ///< Sector normal state image key.
	std::string sectorHoverImageKey;               ///< Sector hovered state image key.
	std::string sectorDisabledImageKey;            ///< Sector disabled state image key.
	float labelFontSize         = 14.0f;           ///< Label font size.
	float iconSize              = 32.0f;           ///< Icon display size in pixels.
	float animationDuration     = 0.2f;            ///< Open/close animation duration in seconds.
	RadialMenuAnimation openAnimation = RadialMenuAnimation::Scale; ///< Animation type.
	float centerDeadZone        = 30.0f;           ///< Radius within which no sector is selected.
	float subRingInnerRadius    = 160.0f;          ///< Sub-ring inner radius.
	float subRingOuterRadius    = 250.0f;          ///< Sub-ring outer radius.
};

/// @brief Computed per-sector geometry for rendering.
struct UIRadialSectorInfo
{
	int index                   = -1;              ///< Item index in the items array.
	float startAngle            = 0.0f;            ///< Start angle in radians.
	float endAngle              = 0.0f;            ///< End angle in radians.
	float midAngle              = 0.0f;            ///< Center angle in radians.
	float iconX                 = 0.0f;            ///< Icon center X (relative to menu center).
	float iconY                 = 0.0f;            ///< Icon center Y (relative to menu center).
	float labelX                = 0.0f;            ///< Label position X (relative to menu center).
	float labelY                = 0.0f;            ///< Label position Y (relative to menu center).
	bool hovered                = false;           ///< Whether this sector is hovered.
	bool enabled                = true;            ///< Whether this sector is enabled.
	bool hasSubItems            = false;           ///< Whether this sector has nested sub-items.
};

/// @brief Circular/pie menu for quick selection via mouse direction or keyboard.
///
/// Sectors are evenly distributed around the circle. Mouse direction from
/// center determines which sector is hovered. Keyboard number keys provide
/// quick selection. Items with subItems expand a second ring on hover.
///
/// @code
///   UIRadialMenuConfig cfg;
///   cfg.innerRadius = 40.0f;
///   cfg.outerRadius = 160.0f;
///   cfg.items = {
///       {"Attack", "icon_sword", true, "1", {}},
///       {"Defend", "icon_shield", true, "2", {}},
///       {"Magic",  "icon_magic",  true, "3", {
///           {"Fire",  "icon_fire",  true, "1", {}},
///           {"Ice",   "icon_ice",   true, "2", {}},
///           {"Thunder","icon_thunder",true,"3", {}},
///       }},
///       {"Items",  "icon_bag",   true, "4", {}},
///   };
///   UIRadialMenu menu(cfg);
///
///   menu.setOnItemSelected([](int idx) { /* handle selection */ });
///   menu.open(400.0f, 300.0f);
///
///   // Each frame:
///   menu.update(mouseX, mouseY, dt);
///   // Render using menu.sectors(), menu.animationProgress(), etc.
/// @endcode
class UIRadialMenu
{
	std::shared_ptr<UINode> m_node;
	UIRadialMenuConfig m_config;
	bool m_open                 = false;
	float m_centerX             = 0.0f;
	float m_centerY             = 0.0f;
	int m_hoveredIndex          = -1;
	int m_hoveredSubIndex       = -1;
	bool m_subRingOpen          = false;

	// Animation state.
	float m_animProgress        = 0.0f;    ///< 0 = closed, 1 = fully open.
	bool m_animating            = false;
	bool m_animOpening          = false;

	// Computed sector geometry.
	std::vector<UIRadialSectorInfo> m_sectors;
	std::vector<UIRadialSectorInfo> m_subSectors;

	// Callbacks.
	std::function<void(int)> m_onItemSelected;
	std::function<void(int, int)> m_onSubItemSelected;

public:
	/// @brief Construct with default configuration.
	UIRadialMenu() { rebuildSectors(); }

	/// @brief Construct with custom configuration.
	/// @param config Menu configuration.
	explicit UIRadialMenu(const UIRadialMenuConfig& config)
		: m_config(config)
	{
		UINodeData data;
		data.id   = INVALID_UI_NODE;
		data.name = "radial_menu";
		data.role = UIRole::Custom;
		data.bounds = sgc::Rectf(
			0.0f, 0.0f,
			config.outerRadius * 2.0f,
			config.outerRadius * 2.0f);
		data.properties["widget_type"] = "radial_menu";

		m_node = std::make_shared<UINode>(std::move(data));
		rebuildSectors();
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current configuration.
	[[nodiscard]] const UIRadialMenuConfig& config() const noexcept { return m_config; }

	/// @brief Set a new configuration and rebuild sectors.
	/// @param config New configuration.
	void setConfig(const UIRadialMenuConfig& config)
	{
		m_config = config;
		rebuildSectors();
		syncNodeState();
	}

	// ── Open/Close ───────────────────────────────────────────

	/// @brief Open the menu at a screen position.
	/// @param centerX Center X position.
	/// @param centerY Center Y position.
	void open(float centerX, float centerY)
	{
		m_centerX = centerX;
		m_centerY = centerY;
		m_open = true;
		m_hoveredIndex = -1;
		m_hoveredSubIndex = -1;
		m_subRingOpen = false;
		m_animating = true;
		m_animOpening = true;
		m_animProgress = 0.0f;
		m_subSectors.clear();

		if (m_node)
		{
			m_node->setBounds(sgc::Rectf(
				centerX - m_config.outerRadius,
				centerY - m_config.outerRadius,
				m_config.outerRadius * 2.0f,
				m_config.outerRadius * 2.0f));
		}
		syncNodeState();
	}

	/// @brief Close the menu (begins close animation).
	void close()
	{
		if (!m_open) { return; }
		m_animating = true;
		m_animOpening = false;
		m_subRingOpen = false;
		m_subSectors.clear();
		syncNodeState();
	}

	/// @brief Check if the menu is open (including during animation).
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief Check if the menu is currently animating.
	[[nodiscard]] bool isAnimating() const noexcept { return m_animating; }

	/// @brief Get the current animation progress (0 = closed, 1 = open).
	[[nodiscard]] float animationProgress() const noexcept { return m_animProgress; }

	/// @brief Get the menu center X.
	[[nodiscard]] float centerX() const noexcept { return m_centerX; }

	/// @brief Get the menu center Y.
	[[nodiscard]] float centerY() const noexcept { return m_centerY; }

	// ── Update ───────────────────────────────────────────────

	/// @brief Update hover state and animation.
	/// @param mouseX Current mouse X position.
	/// @param mouseY Current mouse Y position.
	/// @param dt Delta time in seconds (for animation).
	void update(float mouseX, float mouseY, float dt)
	{
		// Advance animation.
		if (m_animating)
		{
			const float speed = (m_config.animationDuration > 0.0f)
				? (1.0f / m_config.animationDuration) : 100.0f;

			if (m_animOpening)
			{
				m_animProgress = std::min(m_animProgress + speed * dt, 1.0f);
				if (m_animProgress >= 1.0f)
				{
					m_animating = false;
				}
			}
			else
			{
				m_animProgress = std::max(m_animProgress - speed * dt, 0.0f);
				if (m_animProgress <= 0.0f)
				{
					m_animating = false;
					m_open = false;
				}
			}
		}

		if (!m_open) { return; }

		// Compute direction from center.
		const float dx = mouseX - m_centerX;
		const float dy = mouseY - m_centerY;
		const float dist = std::sqrt(dx * dx + dy * dy);

		// Reset hover states.
		for (auto& sector : m_sectors) { sector.hovered = false; }
		for (auto& sector : m_subSectors) { sector.hovered = false; }

		const int prevHovered = m_hoveredIndex;

		if (dist < m_config.centerDeadZone)
		{
			// In dead zone: no selection.
			m_hoveredIndex = -1;
			m_hoveredSubIndex = -1;
		}
		else if (m_subRingOpen && dist >= m_config.subRingInnerRadius && dist <= m_config.subRingOuterRadius)
		{
			// Hovering in sub-ring.
			m_hoveredSubIndex = findSectorAtAngle(m_subSectors, dx, dy);
			if (m_hoveredSubIndex >= 0 && m_hoveredSubIndex < static_cast<int>(m_subSectors.size()))
			{
				m_subSectors[static_cast<std::size_t>(m_hoveredSubIndex)].hovered = true;
			}
		}
		else if (dist >= m_config.innerRadius && dist <= m_config.outerRadius)
		{
			// Hovering in main ring.
			m_hoveredIndex = findSectorAtAngle(m_sectors, dx, dy);
			m_hoveredSubIndex = -1;

			if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_sectors.size()))
			{
				m_sectors[static_cast<std::size_t>(m_hoveredIndex)].hovered = true;

				// Open sub-ring if item has sub-items.
				if (m_sectors[static_cast<std::size_t>(m_hoveredIndex)].hasSubItems
					&& m_hoveredIndex != prevHovered)
				{
					openSubRing(m_hoveredIndex);
				}
				else if (!m_sectors[static_cast<std::size_t>(m_hoveredIndex)].hasSubItems)
				{
					m_subRingOpen = false;
					m_subSectors.clear();
				}
			}
		}
		else
		{
			m_hoveredIndex = -1;
			m_hoveredSubIndex = -1;
		}

		syncNodeState();
	}

	/// @brief Confirm the currently hovered item (mouse click or gamepad confirm).
	void confirm()
	{
		if (!m_open) { return; }

		// Sub-ring selection takes priority.
		if (m_hoveredSubIndex >= 0 && m_subRingOpen && m_hoveredIndex >= 0)
		{
			const auto& subItems = m_config.items[static_cast<std::size_t>(m_hoveredIndex)].subItems;
			if (m_hoveredSubIndex < static_cast<int>(subItems.size())
				&& subItems[static_cast<std::size_t>(m_hoveredSubIndex)].enabled)
			{
				if (m_onSubItemSelected)
				{
					m_onSubItemSelected(m_hoveredIndex, m_hoveredSubIndex);
				}
				close();
			}
			return;
		}

		// Main ring selection.
		if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_config.items.size()))
		{
			const auto& item = m_config.items[static_cast<std::size_t>(m_hoveredIndex)];
			if (item.enabled && item.subItems.empty())
			{
				if (m_onItemSelected)
				{
					m_onItemSelected(m_hoveredIndex);
				}
				close();
			}
		}
	}

	/// @brief Select an item by keyboard shortcut key.
	/// @param key The shortcut key string (e.g. "1", "2").
	void selectByKey(const std::string& key)
	{
		if (!m_open) { return; }

		// Check sub-ring first if open.
		if (m_subRingOpen && m_hoveredIndex >= 0)
		{
			const auto& subItems = m_config.items[static_cast<std::size_t>(m_hoveredIndex)].subItems;
			for (std::size_t i = 0; i < subItems.size(); ++i)
			{
				if (subItems[i].shortcutKey == key && subItems[i].enabled)
				{
					if (m_onSubItemSelected)
					{
						m_onSubItemSelected(m_hoveredIndex, static_cast<int>(i));
					}
					close();
					return;
				}
			}
		}

		// Check main ring.
		for (std::size_t i = 0; i < m_config.items.size(); ++i)
		{
			if (m_config.items[i].shortcutKey == key && m_config.items[i].enabled)
			{
				if (m_config.items[i].subItems.empty())
				{
					if (m_onItemSelected)
					{
						m_onItemSelected(static_cast<int>(i));
					}
					close();
				}
				else
				{
					// Open sub-ring for this item.
					m_hoveredIndex = static_cast<int>(i);
					openSubRing(m_hoveredIndex);
				}
				return;
			}
		}
	}

	// ── Query ────────────────────────────────────────────────

	/// @brief Get the currently hovered main sector index (-1 if none).
	[[nodiscard]] int hoveredIndex() const noexcept { return m_hoveredIndex; }

	/// @brief Get the currently hovered sub-sector index (-1 if none).
	[[nodiscard]] int hoveredSubIndex() const noexcept { return m_hoveredSubIndex; }

	/// @brief Check if the sub-ring is open.
	[[nodiscard]] bool isSubRingOpen() const noexcept { return m_subRingOpen; }

	/// @brief Get computed sector geometry for the main ring (for rendering).
	[[nodiscard]] const std::vector<UIRadialSectorInfo>& sectors() const noexcept
	{
		return m_sectors;
	}

	/// @brief Get computed sector geometry for the sub-ring (for rendering).
	[[nodiscard]] const std::vector<UIRadialSectorInfo>& subSectors() const noexcept
	{
		return m_subSectors;
	}

	/// @brief Get the menu items.
	[[nodiscard]] const std::vector<UIRadialMenuItem>& items() const noexcept
	{
		return m_config.items;
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief Set callback when a main item is selected.
	/// @param callback Receives the item index.
	void setOnItemSelected(std::function<void(int)> callback)
	{
		m_onItemSelected = std::move(callback);
	}

	/// @brief Set callback when a sub-item is selected.
	/// @param callback Receives (parentIndex, subItemIndex).
	void setOnSubItemSelected(std::function<void(int, int)> callback)
	{
		m_onSubItemSelected = std::move(callback);
	}

private:
	static constexpr float kPi = 3.14159265358979323846f;
	static constexpr float kTwoPi = 6.28318530717958647692f;

	/// @brief Rebuild sector geometry from current config items.
	void rebuildSectors()
	{
		m_sectors.clear();
		const std::size_t count = m_config.items.size();
		if (count == 0) { return; }

		const float sectorAngle = kTwoPi / static_cast<float>(count);
		const float midRadius = (m_config.innerRadius + m_config.outerRadius) * 0.5f;

		// Sectors start from top (-pi/2) and go clockwise.
		const float startOffset = -kPi * 0.5f - sectorAngle * 0.5f;

		for (std::size_t i = 0; i < count; ++i)
		{
			UIRadialSectorInfo sector;
			sector.index = static_cast<int>(i);
			sector.startAngle = startOffset + sectorAngle * static_cast<float>(i);
			sector.endAngle = sector.startAngle + sectorAngle;
			sector.midAngle = sector.startAngle + sectorAngle * 0.5f;
			sector.iconX = std::cos(sector.midAngle) * midRadius;
			sector.iconY = std::sin(sector.midAngle) * midRadius;
			sector.labelX = std::cos(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.labelY = std::sin(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.enabled = m_config.items[i].enabled;
			sector.hasSubItems = !m_config.items[i].subItems.empty();

			m_sectors.push_back(sector);
		}
	}

	/// @brief Build sub-ring sectors from a parent item's subItems.
	/// @param parentIndex Index of the parent item.
	void openSubRing(int parentIndex)
	{
		m_subSectors.clear();
		m_subRingOpen = false;

		if (parentIndex < 0 || parentIndex >= static_cast<int>(m_config.items.size()))
		{
			return;
		}

		const auto& subItems = m_config.items[static_cast<std::size_t>(parentIndex)].subItems;
		if (subItems.empty()) { return; }

		m_subRingOpen = true;
		const std::size_t count = subItems.size();
		const float sectorAngle = kTwoPi / static_cast<float>(count);
		const float midRadius = (m_config.subRingInnerRadius + m_config.subRingOuterRadius) * 0.5f;
		const float startOffset = -kPi * 0.5f - sectorAngle * 0.5f;

		for (std::size_t i = 0; i < count; ++i)
		{
			UIRadialSectorInfo sector;
			sector.index = static_cast<int>(i);
			sector.startAngle = startOffset + sectorAngle * static_cast<float>(i);
			sector.endAngle = sector.startAngle + sectorAngle;
			sector.midAngle = sector.startAngle + sectorAngle * 0.5f;
			sector.iconX = std::cos(sector.midAngle) * midRadius;
			sector.iconY = std::sin(sector.midAngle) * midRadius;
			sector.labelX = std::cos(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.labelY = std::sin(sector.midAngle) * (midRadius + m_config.iconSize * 0.3f);
			sector.enabled = subItems[i].enabled;
			sector.hasSubItems = false;

			m_subSectors.push_back(sector);
		}
	}

	/// @brief Find which sector contains the given direction vector.
	/// @param sectors Sector list to search.
	/// @param dx Direction X from center.
	/// @param dy Direction Y from center.
	/// @return Sector index, or -1 if none found.
	[[nodiscard]] static int findSectorAtAngle(
		const std::vector<UIRadialSectorInfo>& sectors, float dx, float dy)
	{
		if (sectors.empty()) { return -1; }

		float angle = std::atan2(dy, dx);

		for (const auto& sector : sectors)
		{
			// Normalize angle relative to sector start.
			float relAngle = angle - sector.startAngle;
			// Wrap to [-pi, pi).
			while (relAngle > kPi) { relAngle -= kTwoPi; }
			while (relAngle < -kPi) { relAngle += kTwoPi; }

			const float sectorSpan = sector.endAngle - sector.startAngle;
			if (relAngle >= 0.0f && relAngle < sectorSpan)
			{
				return sector.index;
			}
		}

		return -1;
	}

	/// @brief Synchronize state to the UINode properties.
	void syncNodeState()
	{
		if (!m_node) { return; }

		m_node->setProperty("open", m_open ? "true" : "false");
		m_node->setProperty("hovered", std::to_string(m_hoveredIndex));
		m_node->setProperty("hovered_sub", std::to_string(m_hoveredSubIndex));
		m_node->setProperty("sub_ring_open", m_subRingOpen ? "true" : "false");
		m_node->setProperty("anim_progress", std::to_string(m_animProgress));
		m_node->setProperty("center_x", std::to_string(m_centerX));
		m_node->setProperty("center_y", std::to_string(m_centerY));
		m_node->setProperty("item_count", std::to_string(m_config.items.size()));
	}
};

} // namespace mitiru::ui
