#pragma once

/// @file UIHotbar.hpp
/// @brief Quick action slot bar widget (RPG skill bar, item shortcuts).

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Orientation of the hotbar layout.
enum class HotbarOrientation : std::uint8_t
{
	Horizontal,
	Vertical
};

/// @brief Configuration for creating a UIHotbar.
struct UIHotbarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::uint32_t slotCount = 10;
	float slotSize = 48.0f;
	float spacing = 4.0f;
	HotbarOrientation orientation = HotbarOrientation::Horizontal;
	std::string backgroundImageKey;
	std::string slotImageKey;
	std::string slotHoverImageKey;
	std::string slotActiveImageKey;
	sgc::Colorf cooldownOverlayColor{0.0f, 0.0f, 0.0f, 0.6f};
	float keybindFontSize = 10.0f;
	float labelFontSize = 10.0f;
	float padding = 4.0f;
};

/// @brief Data for a single hotbar slot.
struct UIHotbarSlot
{
	std::string iconImageKey;
	std::string label;
	std::string keybind;
	float cooldownPercent = 0.0f;   ///< 0.0 = ready, 1.0 = fully on cooldown
	std::uint32_t quantity = 0;
	bool enabled = true;
	bool active = false;
};

/// @brief Quick action slot bar widget.
///
/// Provides a configurable bar of action slots, each showing an icon,
/// optional keybind label, quantity, and cooldown overlay.
///
/// @code
///   UIHotbarConfig cfg;
///   cfg.id = 200;
///   cfg.slotCount = 10;
///   UIHotbar bar(cfg);
///
///   UIHotbarSlot slot;
///   slot.iconImageKey = "icon_fireball";
///   slot.keybind = "1";
///   bar.setSlot(0, slot);
///   bar.setOnSlotTriggered([](std::uint32_t idx) { /* use skill */ });
/// @endcode
class UIHotbar
{
	std::shared_ptr<UINode> m_node;
	std::vector<UIHotbarSlot> m_slots;
	UIHotbarConfig m_config;
	std::int32_t m_hoveredSlot = -1;

	std::function<void(std::uint32_t)> m_onSlotTriggered;
	std::function<void(std::uint32_t)> m_onSlotRightClicked;

public:
	/// @brief Construct a hotbar from configuration.
	/// @param config Hotbar configuration.
	explicit UIHotbar(const UIHotbarConfig& config)
		: m_config(config)
		, m_slots(config.slotCount)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Container;
		data.bounds = sgc::Rectf(0.0f, 0.0f, totalWidth(), totalHeight());
		data.properties["widget_type"] = "hotbar";
		data.properties["slot_count"] = std::to_string(config.slotCount);
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["slot_image"] = config.slotImageKey;
		data.properties["slot_hover_image"] = config.slotHoverImageKey;
		data.properties["slot_active_image"] = config.slotActiveImageKey;
		data.properties["orientation"] = (config.orientation == HotbarOrientation::Horizontal)
			? "horizontal" : "vertical";
		data.properties["keybind_font_size"] = std::to_string(config.keybindFontSize);
		data.properties["label_font_size"] = std::to_string(config.labelFontSize);
		data.properties["padding"] = std::to_string(config.padding);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	// ── Accessors ────────────────────────────────────────────

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the number of slots.
	[[nodiscard]] std::uint32_t slotCount() const noexcept
	{
		return static_cast<std::uint32_t>(m_slots.size());
	}

	/// @brief Get a slot by index.
	/// @param index Slot index.
	/// @return Const reference to the slot data.
	[[nodiscard]] const UIHotbarSlot& slot(std::uint32_t index) const noexcept
	{
		return m_slots[index];
	}

	/// @brief Get the currently hovered slot index, or -1 if none.
	[[nodiscard]] std::int32_t hoveredSlot() const noexcept { return m_hoveredSlot; }

	/// @brief Compute the total width of the bar.
	[[nodiscard]] float totalWidth() const noexcept
	{
		if (m_config.orientation == HotbarOrientation::Horizontal)
		{
			const auto n = static_cast<float>(m_config.slotCount);
			return 2.0f * m_config.padding + n * m_config.slotSize + (n - 1.0f) * m_config.spacing;
		}
		return 2.0f * m_config.padding + m_config.slotSize;
	}

	/// @brief Compute the total height of the bar.
	[[nodiscard]] float totalHeight() const noexcept
	{
		if (m_config.orientation == HotbarOrientation::Vertical)
		{
			const auto n = static_cast<float>(m_config.slotCount);
			return 2.0f * m_config.padding + n * m_config.slotSize + (n - 1.0f) * m_config.spacing;
		}
		return 2.0f * m_config.padding + m_config.slotSize;
	}

	/// @brief Get the bounds of a specific slot in local space.
	/// @param index Slot index.
	[[nodiscard]] sgc::Rectf slotBounds(std::uint32_t index) const noexcept
	{
		const auto fi = static_cast<float>(index);
		const float offset = m_config.padding + fi * (m_config.slotSize + m_config.spacing);
		if (m_config.orientation == HotbarOrientation::Horizontal)
		{
			return sgc::Rectf(offset, m_config.padding, m_config.slotSize, m_config.slotSize);
		}
		return sgc::Rectf(m_config.padding, offset, m_config.slotSize, m_config.slotSize);
	}

	// ── Slot management ──────────────────────────────────────

	/// @brief Set slot data at the given index.
	/// @param index Slot index.
	/// @param slotData Slot data to set.
	void setSlot(std::uint32_t index, const UIHotbarSlot& slotData)
	{
		if (index >= m_slots.size()) { return; }
		m_slots[index] = slotData;
		syncSlotProperty(index);
	}

	/// @brief Clear a slot to its default empty state.
	/// @param index Slot index.
	void clearSlot(std::uint32_t index)
	{
		if (index >= m_slots.size()) { return; }
		m_slots[index] = UIHotbarSlot{};
		syncSlotProperty(index);
	}

	/// @brief Trigger the action of a slot.
	/// @param index Slot index.
	void triggerSlot(std::uint32_t index)
	{
		if (index >= m_slots.size()) { return; }
		if (!m_slots[index].enabled) { return; }
		if (m_slots[index].cooldownPercent > 0.0f) { return; }
		if (m_onSlotTriggered) { m_onSlotTriggered(index); }
	}

	/// @brief Update cooldowns by delta time. Call every frame.
	/// @param dt Delta time in seconds.
	void update([[maybe_unused]] float dt)
	{
		// Cooldown tick-down is game-logic driven; this hook allows
		// animation state updates if needed in the future.
		syncNodeState();
	}

	// ── Callbacks ────────────────────────────────────────────

	/// @brief Set callback invoked when a slot is triggered.
	void setOnSlotTriggered(std::function<void(std::uint32_t)> callback)
	{
		m_onSlotTriggered = std::move(callback);
	}

	/// @brief Set callback invoked when a slot is right-clicked.
	void setOnSlotRightClicked(std::function<void(std::uint32_t)> callback)
	{
		m_onSlotRightClicked = std::move(callback);
	}

	// ── Interaction (called by event system) ─────────────────

	/// @brief Called when pointer moves over the hotbar.
	/// @param localX X in local hotbar space.
	/// @param localY Y in local hotbar space.
	void onPointerMove(float localX, float localY)
	{
		m_hoveredSlot = hitTestSlot(localX, localY);
		syncNodeState();
	}

	/// @brief Called when pointer leaves the hotbar area.
	void onPointerLeave()
	{
		m_hoveredSlot = -1;
		syncNodeState();
	}

	/// @brief Called when pointer is pressed (left click) on the hotbar.
	/// @param localX X in local hotbar space.
	/// @param localY Y in local hotbar space.
	void onPointerDown(float localX, float localY)
	{
		const auto idx = hitTestSlot(localX, localY);
		if (idx >= 0)
		{
			triggerSlot(static_cast<std::uint32_t>(idx));
		}
	}

	/// @brief Called when pointer is right-clicked on the hotbar.
	/// @param localX X in local hotbar space.
	/// @param localY Y in local hotbar space.
	void onRightClick(float localX, float localY)
	{
		const auto idx = hitTestSlot(localX, localY);
		if (idx >= 0 && m_onSlotRightClicked)
		{
			m_onSlotRightClicked(static_cast<std::uint32_t>(idx));
		}
	}

	/// @brief Called when a key is pressed (for keybind matching).
	/// @param key The key string (e.g. "1", "Q").
	void onKeyDown(const std::string& key)
	{
		for (std::uint32_t i = 0; i < m_slots.size(); ++i)
		{
			if (!m_slots[i].keybind.empty() && m_slots[i].keybind == key)
			{
				triggerSlot(i);
				return;
			}
		}
	}

private:
	/// @brief Determine which slot index a local point falls in, or -1 if none.
	[[nodiscard]] std::int32_t hitTestSlot(float lx, float ly) const noexcept
	{
		for (std::uint32_t i = 0; i < m_slots.size(); ++i)
		{
			const auto b = slotBounds(i);
			if (lx >= b.x && lx <= b.x + b.w && ly >= b.y && ly <= b.y + b.h)
			{
				return static_cast<std::int32_t>(i);
			}
		}
		return -1;
	}

	/// @brief Sync a single slot's data to node properties.
	void syncSlotProperty(std::uint32_t index)
	{
		const auto prefix = "slot_" + std::to_string(index) + "_";
		const auto& s = m_slots[index];
		m_node->setProperty(prefix + "icon", s.iconImageKey);
		m_node->setProperty(prefix + "label", s.label);
		m_node->setProperty(prefix + "keybind", s.keybind);
		m_node->setProperty(prefix + "cooldown", std::to_string(s.cooldownPercent));
		m_node->setProperty(prefix + "quantity", std::to_string(s.quantity));
		m_node->setProperty(prefix + "enabled", s.enabled ? "true" : "false");
		m_node->setProperty(prefix + "active", s.active ? "true" : "false");
	}

	/// @brief Synchronize overall state to the UINode.
	void syncNodeState()
	{
		m_node->setProperty("hovered_slot", std::to_string(m_hoveredSlot));
		for (std::uint32_t i = 0; i < m_slots.size(); ++i)
		{
			syncSlotProperty(i);
		}
	}
};

} // namespace mitiru::ui
