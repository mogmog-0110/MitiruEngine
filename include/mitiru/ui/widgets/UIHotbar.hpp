#pragma once

/// @file UIHotbar.hpp
/// @brief quick action slot bar widget (RPG の skill bar、item shortcut)。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief hotbar layout の向き。
enum class HotbarOrientation : std::uint8_t
{
	Horizontal,
	Vertical
};

/// @brief UIHotbar 生成用の設定。
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

/// @brief 単一 hotbar slot の data。
struct UIHotbarSlot
{
	std::string iconImageKey;
	std::string label;
	std::string keybind;
	float cooldownPercent = 0.0f;   ///< 0.0 = 使用可能、1.0 = cooldown 中
	std::uint32_t quantity = 0;
	bool enabled = true;
	bool active = false;
};

/// @brief quick action slot bar widget。
///
/// 設定可能な action slot の bar を提供する。各 slot は icon、任意の keybind label、
/// quantity、cooldown overlay を表示する。
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
	/// @brief 設定から hotbar を構築する。
	/// @param config hotbar 設定。
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

	// ── accessor ──────────────────────────────────────────────

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief slot 数を取得する。
	[[nodiscard]] std::uint32_t slotCount() const noexcept
	{
		return static_cast<std::uint32_t>(m_slots.size());
	}

	/// @brief index 指定で slot を取得する。
	/// @param index slot の index。
	/// @return slot data への const 参照。
	[[nodiscard]] const UIHotbarSlot& slot(std::uint32_t index) const noexcept
	{
		return m_slots[index];
	}

	/// @brief 現在 hover 中の slot index を取得する。なければ -1。
	[[nodiscard]] std::int32_t hoveredSlot() const noexcept { return m_hoveredSlot; }

	/// @brief bar の総幅を計算する。
	[[nodiscard]] float totalWidth() const noexcept
	{
		if (m_config.orientation == HotbarOrientation::Horizontal)
		{
			const auto n = static_cast<float>(m_config.slotCount);
			return 2.0f * m_config.padding + n * m_config.slotSize + (n - 1.0f) * m_config.spacing;
		}
		return 2.0f * m_config.padding + m_config.slotSize;
	}

	/// @brief bar の総高さを計算する。
	[[nodiscard]] float totalHeight() const noexcept
	{
		if (m_config.orientation == HotbarOrientation::Vertical)
		{
			const auto n = static_cast<float>(m_config.slotCount);
			return 2.0f * m_config.padding + n * m_config.slotSize + (n - 1.0f) * m_config.spacing;
		}
		return 2.0f * m_config.padding + m_config.slotSize;
	}

	/// @brief 特定 slot の local 空間での bounds を取得する。
	/// @param index slot の index。
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

	// ── slot 管理 ─────────────────────────────────────────────

	/// @brief 指定 index に slot data を設定する。
	/// @param index slot の index。
	/// @param slotData 設定する slot data。
	void setSlot(std::uint32_t index, const UIHotbarSlot& slotData)
	{
		if (index >= m_slots.size()) { return; }
		m_slots[index] = slotData;
		syncSlotProperty(index);
	}

	/// @brief slot を既定の空 state にクリアする。
	/// @param index slot の index。
	void clearSlot(std::uint32_t index)
	{
		if (index >= m_slots.size()) { return; }
		m_slots[index] = UIHotbarSlot{};
		syncSlotProperty(index);
	}

	/// @brief slot の action を trigger する。
	/// @param index slot の index。
	void triggerSlot(std::uint32_t index)
	{
		if (index >= m_slots.size()) { return; }
		if (!m_slots[index].enabled) { return; }
		if (m_slots[index].cooldownPercent > 0.0f) { return; }
		if (m_onSlotTriggered) { m_onSlotTriggered(index); }
	}

	/// @brief delta time だけ cooldown を更新する。毎 frame 呼ぶ。
	/// @param dt delta time (秒)。
	void update([[maybe_unused]] float dt)
	{
		// cooldown の tick-down は game-logic 駆動。この hook は将来
		// 必要になったとき animation state を更新するためのもの。
		syncNodeState();
	}

	// ── callback ──────────────────────────────────────────────

	/// @brief slot が trigger されたときに呼ばれる callback を設定する。
	void setOnSlotTriggered(std::function<void(std::uint32_t)> callback)
	{
		m_onSlotTriggered = std::move(callback);
	}

	/// @brief slot が right-click されたときに呼ばれる callback を設定する。
	void setOnSlotRightClicked(std::function<void(std::uint32_t)> callback)
	{
		m_onSlotRightClicked = std::move(callback);
	}

	// ── 操作 (event system から呼ばれる) ──────────────────────

	/// @brief hotbar 上を pointer が移動したときに呼ばれる。
	/// @param localX local hotbar 空間での X。
	/// @param localY local hotbar 空間での Y。
	void onPointerMove(float localX, float localY)
	{
		m_hoveredSlot = hitTestSlot(localX, localY);
		syncNodeState();
	}

	/// @brief pointer が hotbar 領域から離れたときに呼ばれる。
	void onPointerLeave()
	{
		m_hoveredSlot = -1;
		syncNodeState();
	}

	/// @brief hotbar 上で pointer が押された (left click) ときに呼ばれる。
	/// @param localX local hotbar 空間での X。
	/// @param localY local hotbar 空間での Y。
	void onPointerDown(float localX, float localY)
	{
		const auto idx = hitTestSlot(localX, localY);
		if (idx >= 0)
		{
			triggerSlot(static_cast<std::uint32_t>(idx));
		}
	}

	/// @brief hotbar 上で pointer が right-click されたときに呼ばれる。
	/// @param localX local hotbar 空間での X。
	/// @param localY local hotbar 空間での Y。
	void onRightClick(float localX, float localY)
	{
		const auto idx = hitTestSlot(localX, localY);
		if (idx >= 0 && m_onSlotRightClicked)
		{
			m_onSlotRightClicked(static_cast<std::uint32_t>(idx));
		}
	}

	/// @brief key が押されたときに呼ばれる (keybind 照合用)。
	/// @param key key 文字列 (例 "1"、"Q")。
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
	/// @brief local の点がどの slot index に入るか判定する。なければ -1。
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

	/// @brief 単一 slot の data を node の properties に同期する。
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

	/// @brief 全体の state を UINode に同期する。
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
