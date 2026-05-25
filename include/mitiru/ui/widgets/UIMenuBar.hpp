#pragma once

/// @file UIMenuBar.hpp
/// @brief dropdown menu を持つ水平 menu bar (File, Edit, View 等)。

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

/// @brief menu bar 内の top-level menu の定義。
struct UIMenuDef
{
	std::string label;                      ///< menu の label (例 "File")。
	std::vector<UIMenuItemDef> items;       ///< dropdown 項目。
	bool enabled = true;                    ///< menu が操作可能か。
};

/// @brief UIMenuBar 生成用の設定。
struct UIMenuBarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIMenuDef> menus;

	// ── layout ────────────────────────────────────────────────
	float height = 28.0f;                   ///< bar の高さ。
	float itemPaddingX = 12.0f;             ///< 各 menu label 内の水平 padding。
	float itemPaddingY = 4.0f;              ///< 各 menu label 内の垂直 padding。
	float fontSize = 14.0f;                 ///< menu label の font size。
	float barWidth = 0.0f;                  ///< bar の総幅 (0 = auto/全幅)。

	// ── dropdown 設定 ─────────────────────────────────────────
	float dropdownWidth = 200.0f;           ///< dropdown menu の幅。
	float dropdownItemHeight = 28.0f;       ///< dropdown 項目の高さ。
	int dropdownMaxVisibleItems = 12;       ///< scroll 前に表示する dropdown 項目の最大数。
	float dropdownPadding = 4.0f;           ///< dropdown の内側 padding。

	// ── 画面 bounds ───────────────────────────────────────────
	float screenWidth = 1920.0f;
	float screenHeight = 1080.0f;

	// ── 画像 key 群 ───────────────────────────────────────────
	std::string backgroundImageKey;         ///< bar 背景の画像 key。
	std::string activeItemImageKey;         ///< active/選択中の menu label の画像 key。
	std::string hoverItemImageKey;          ///< hover 中の menu label の画像 key。
	std::string dropdownBackgroundImageKey; ///< dropdown 背景の画像 key。
	std::string dropdownItemHoverImageKey;  ///< hover 中の dropdown 項目の画像 key。
	std::string dropdownSeparatorImageKey;  ///< dropdown separator の画像 key。
};

/// @brief dropdown menu を持つ水平 menu bar widget。
///
/// top-level の menu label と、それに紐づく dropdown menu (UIContextMenu) を管理する。
/// dropdown が開いている間の hover 切り替え、Alt key による activation、
/// 完全な keyboard navigation をサポートする。描画は外部の UIRenderer が担当する。
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
	/// @brief 単一の top-level menu entry の runtime state。
	struct MenuEntry
	{
		UIMenuDef def;
		float x = 0.0f;           ///< label 領域の左端。
		float width = 0.0f;       ///< label 領域の幅。
		bool hovered = false;
	};

	std::shared_ptr<UINode> m_node;
	std::vector<MenuEntry> m_entries;
	UIContextMenu m_dropdown;

	// ── config の複製 ─────────────────────────────────────────
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

	// ── runtime state ─────────────────────────────────────────
	int m_activeMenuIndex = -1;     ///< 現在開いている menu の index (-1 = なし)。
	int m_hoveredMenuIndex = -1;    ///< 現在 hover 中の menu label の index。
	bool m_activated = false;       ///< bar が activated 状態か (Alt 押下)。

	// ── Callbacks ─────────────────────────────────────────────
	std::function<void(int, int)> m_onMenuItemSelected;

public:
	/// @brief 設定から menu bar を構築する。
	/// @param config menu bar 設定。
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

		// config から entry を構築する。
		for (const auto& menuDef : config.menus)
		{
			addMenuInternal(menuDef);
		}

		// dropdown の callback を自分の callback 経由に配線する。
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

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief dropdown の context menu を取得する (描画アクセス用)。
	[[nodiscard]] const UIContextMenu& dropdown() const noexcept { return m_dropdown; }

	/// @brief いずれかの dropdown menu が現在開いているか確認する。
	[[nodiscard]] bool isMenuOpen() const noexcept { return m_activeMenuIndex >= 0 && m_dropdown.isOpen(); }

	/// @brief 現在開いている menu の index を取得する (-1 = なし)。
	[[nodiscard]] int activeMenuIndex() const noexcept { return m_activeMenuIndex; }

	/// @brief 現在 hover 中の menu label の index を取得する (-1 = なし)。
	[[nodiscard]] int hoveredMenuIndex() const noexcept { return m_hoveredMenuIndex; }

	/// @brief top-level menu の数を取得する。
	[[nodiscard]] std::size_t menuCount() const noexcept { return m_entries.size(); }

	/// @brief menu bar が activated mode か確認する (Alt 押下)。
	[[nodiscard]] bool isActivated() const noexcept { return m_activated; }

	/// @brief bar の高さを取得する。
	[[nodiscard]] float height() const noexcept { return m_height; }

	/// @brief 背景画像 key を取得する。
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief active item の画像 key を取得する。
	[[nodiscard]] const std::string& activeItemImageKey() const noexcept { return m_activeItemImageKey; }

	/// @brief 描画用に menu entry の label と bounds を取得する。
	/// @param index menu の index。
	/// @return (label, Rectf bounds) の組。無効なら空。
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

	/// @brief bar に menu を追加する。
	/// @param label menu の label。
	/// @param items dropdown 項目。
	void addMenu(const std::string& label, const std::vector<UIMenuItemDef>& items)
	{
		UIMenuDef def;
		def.label = label;
		def.items = items;
		addMenuInternal(def);
		syncNodeState();
	}

	/// @brief dropdown 項目が選択されたときに呼ばれる callback を設定する。
	/// @param callback (menuIndex, itemIndex) を受け取る関数。
	void setOnMenuItemSelected(std::function<void(int, int)> callback)
	{
		m_onMenuItemSelected = std::move(callback);
	}

	/// @brief 特定 menu の推定 label 幅を設定する (layout/renderer が呼ぶ)。
	/// @param index menu の index。
	/// @param width label の幅。
	void setMenuLabelWidth(std::size_t index, float width)
	{
		if (index >= m_entries.size()) { return; }
		m_entries[index].width = width + m_itemPaddingX * 2.0f;
		recalculateLayout();
	}

	/// @brief dropdown 配置用の screen bounds を設定する。
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
		m_dropdown.setScreenBounds(width, height);
	}

	// ── 入力処理 ──────────────────────────────────────────────

	/// @brief mouse の移動を処理する。
	/// @param mouseX screen 空間での mouse X。
	/// @param mouseY screen 空間での mouse Y。
	void onMouseMove(float mouseX, float mouseY)
	{
		// 開いていれば dropdown へ転送する。
		if (m_dropdown.isOpen())
		{
			m_dropdown.onMouseMove(mouseX, mouseY);
		}

		// bar label 上の hover を判定する。
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

				// hover 切り替え: dropdown が開いていて別の label を hover したら切り替える。
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

	/// @brief mouse の click を処理する。
	/// @param mouseX screen 空間での mouse X。
	/// @param mouseY screen 空間での mouse Y。
	void onMouseClick(float mouseX, float mouseY)
	{
		// click が bar label 上か判定する。
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			const auto& entry = m_entries[i];
			if (mouseY >= 0.0f && mouseY < m_height
				&& mouseX >= entry.x && mouseX < entry.x + entry.width)
			{
				if (!entry.def.enabled) { return; }

				if (m_activeMenuIndex == static_cast<int>(i) && m_dropdown.isOpen())
				{
					// toggle: 既に開いていれば閉じる。
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

		// click を dropdown へ転送する。
		if (m_dropdown.isOpen())
		{
			m_dropdown.onMouseClick(mouseX, mouseY);
		}
	}

	// ── keyboard navigation ───────────────────────────────────

	/// @brief menu bar の activation を toggle する (Alt key)。
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

	/// @brief 次の menu label へ移動する (activated 時の Right arrow)。
	void navigateNextMenu()
	{
		if (m_entries.empty()) { return; }

		const int current = isMenuOpen() ? m_activeMenuIndex : m_hoveredMenuIndex;
		int next = current + 1;
		if (next >= static_cast<int>(m_entries.size())) { next = 0; }

		// disabled な menu は飛ばす。
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

	/// @brief 前の menu label へ移動する (activated 時の Left arrow)。
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

	/// @brief 現在 hover 中の menu の dropdown を開く (Enter/Down arrow)。
	void openHoveredMenu()
	{
		if (m_hoveredMenuIndex >= 0 && m_hoveredMenuIndex < static_cast<int>(m_entries.size()))
		{
			openDropdown(m_hoveredMenuIndex);
			syncNodeState();
		}
	}

	/// @brief keyboard navigation を開いている dropdown へ転送する。
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
		// 既定の幅推定値。正確な sizing には caller が setMenuLabelWidth を使うこと。
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
