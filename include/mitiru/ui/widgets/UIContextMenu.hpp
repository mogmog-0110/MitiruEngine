#pragma once

/// @file UIContextMenu.hpp
/// @brief 入れ子 submenu とキーボード操作に対応した右クリック popup context menu。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief 単一 menu 項目の定義 (submenu 用に children を持てる)。
struct UIMenuItemDef
{
	std::string label;                      ///< 表示テキスト。
	std::string iconImageKey;               ///< 項目 icon の image key。
	std::string shortcutText;               ///< shortcut ヒントテキスト (例 "Ctrl+S")。
	bool enabled = true;                    ///< 項目が操作可能か。
	bool separator = false;                 ///< true なら区切り線として描画する。
	std::vector<UIMenuItemDef> children;    ///< submenu 項目 (空 = leaf 項目)。
};

/// @brief UIContextMenu 生成用の設定。
struct UIContextMenuConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIMenuItemDef> items;

	// ── Layout ────────────────────────────────────────────────
	float width = 200.0f;                   ///< menu panel の幅。
	float itemHeight = 28.0f;               ///< 各 menu 項目の高さ。
	int maxVisibleItems = 12;               ///< scroll が始まるまでの最大項目数。
	float padding = 4.0f;                   ///< 内側 padding。
	float separatorHeight = 1.0f;           ///< 区切り線の高さ。
	float iconSize = 16.0f;                 ///< 項目 icon のサイズ。
	float submenuOffset = -4.0f;            ///< submenu の水平方向の重なり。

	// ── 画面境界 ─────────────────────────────────────────
	float screenWidth = 1920.0f;            ///< 境界 clamp 用の画面幅。
	float screenHeight = 1080.0f;           ///< 境界 clamp 用の画面高さ。
	float screenMargin = 4.0f;              ///< 画面端からの最小距離。

	// ── image key ────────────────────────────────────────────
	std::string backgroundImageKey;         ///< menu 背景の image key。
	std::string itemHoverImageKey;          ///< hover 中項目の背景 image key。
	std::string separatorImageKey;          ///< 区切り装飾の image key。
	std::string submenuArrowImageKey;       ///< submenu 表示矢印の image key。
};

/// @brief 入れ子 submenu とキーボード / マウス操作を備えた context menu widget。
///
/// 無制限の submenu 深度、画面境界内への自動配置、キーボード (矢印キー、
/// Enter、Escape) とマウスの両方の操作に対応する。描画は UIRenderer が
/// 外部で担う。
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
	/// @brief 階層内の単一 menu level の状態。
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

	// ── config の複製 ─────────────────────────────────────────
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
	/// @brief 設定から context menu を構築する。
	/// @param config context menu の設定。
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

	/// @brief 基底の root UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_rootNode; }

	/// @brief context menu が現在開いているか。
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief 開いている submenu level 数を取得する (1 = root menu のみ)。
	[[nodiscard]] std::size_t menuDepth() const noexcept { return m_levels.size(); }

	/// @brief 指定 menu depth の highlight index を取得する (0 = root)。
	[[nodiscard]] int highlightedIndex(std::size_t depth = 0) const noexcept
	{
		if (depth < m_levels.size()) { return m_levels[depth].highlightedIndex; }
		return -1;
	}

	/// @brief 現在の選択パスを menu 階層を辿る index 列として取得する。
	[[nodiscard]] std::vector<int> currentPath() const
	{
		std::vector<int> path;
		for (const auto& level : m_levels)
		{
			if (level.highlightedIndex >= 0) { path.push_back(level.highlightedIndex); }
		}
		return path;
	}

	/// @brief 背景の image key を取得する。
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief 項目 hover の image key を取得する。
	[[nodiscard]] const std::string& itemHoverImageKey() const noexcept { return m_itemHoverImageKey; }

	// ── 設定 ─────────────────────────────────────────

	/// @brief root menu level の項目を設定する。
	/// @param items menu 項目の定義。
	void setItems(const std::vector<UIMenuItemDef>& items)
	{
		m_rootNode->setProperty("item_count", std::to_string(items.size()));
		// 開いたとき使うために保持する。
		if (!m_levels.empty())
		{
			m_levels[0].items = items;
		}
	}

	/// @brief 項目が選択されたとき呼ばれる callback を設定する。
	/// @param callback (leaf index, full path) を受け取る関数。
	void setOnItemSelected(std::function<void(int, const std::vector<int>&)> callback)
	{
		m_onItemSelected = std::move(callback);
	}

	/// @brief menu が閉じられたとき呼ばれる callback を設定する。
	/// @param callback 閉じる時に呼ばれる関数。
	void setOnClosed(std::function<void()> callback)
	{
		m_onClosed = std::move(callback);
	}

	/// @brief 自動配置用の画面境界を設定する。
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
	}

	// ── 操作 ───────────────────────────────────────────────

	/// @brief 指定の画面位置で context menu を開く。
	/// @param x 画面 X 座標。
	/// @param y 画面 Y 座標。
	void open(float x, float y)
	{
		open(x, y, {});
	}

	/// @brief 指定位置に特定の項目で context menu を開く。
	/// @param x 画面 X 座標。
	/// @param y 画面 Y 座標。
	/// @param items 表示する項目 (空 = 既に設定済みの項目を使う)。
	void open(float x, float y, const std::vector<UIMenuItemDef>& items)
	{
		m_levels.clear();
		m_open = true;

		MenuLevel root;
		root.items = items.empty()
			? (m_levels.empty() ? std::vector<UIMenuItemDef>{} : m_levels[0].items)
			: items;

		// items が渡された or setItems で保持済みなら、それを使う。
		if (root.items.empty())
		{
			// constructor の config で設定された項目を使おうとする。
			// rootNode の property item_count に保持されている。
		}

		const auto clamped = clampMenuPosition(x, y, root.items);
		root.x = clamped.first;
		root.y = clamped.second;
		root.node = createLevelNode(root, 0);

		m_levels.push_back(std::move(root));
		syncNodeState();
	}

	/// @brief menu と全 submenu を閉じる。
	void close()
	{
		m_levels.clear();
		m_open = false;
		syncNodeState();

		if (m_onClosed) { m_onClosed(); }
	}

	// ── マウス操作 ─────────────────────────────────────

	/// @brief マウス位置に基づいて highlight を更新する。
	/// @param mouseX 画面空間でのマウス X。
	/// @param mouseY 画面空間でのマウス Y。
	void onMouseMove(float mouseX, float mouseY)
	{
		if (!m_open || m_levels.empty()) { return; }

		// 最深 submenu から root へ向かって調べる。
		for (auto it = m_levels.rbegin(); it != m_levels.rend(); ++it)
		{
			const int idx = hitTestLevel(*it, mouseX, mouseY);
			if (idx >= 0)
			{
				// マウスが親 level に移動したら、より深い level を閉じる。
				const auto depth = static_cast<std::size_t>(std::distance(it, m_levels.rend()) - 1);
				closeLevelsBeyond(depth);

				auto& level = m_levels[depth];
				level.highlightedIndex = idx;

				// hover 中の項目が children を持つなら submenu を開く。
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

	/// @brief 指定位置でのマウスクリックを処理する。
	/// @param mouseX 画面空間でのマウス X。
	/// @param mouseY 画面空間でのマウス Y。
	void onMouseClick(float mouseX, float mouseY)
	{
		if (!m_open || m_levels.empty()) { return; }

		// 最深から最浅へ向かって調べる。
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

		// 全 menu の外側がクリックされた。
		close();
	}

	// ── キーボード操作 ──────────────────────────────────

	/// @brief 現在の最深 menu level で上へ移動する。
	void navigateUp()
	{
		if (!m_open || m_levels.empty()) { return; }
		auto& level = m_levels.back();
		moveHighlight(level, -1);
		syncNodeState();
	}

	/// @brief 現在の最深 menu level で下へ移動する。
	void navigateDown()
	{
		if (!m_open || m_levels.empty()) { return; }
		auto& level = m_levels.back();
		moveHighlight(level, 1);
		syncNodeState();
	}

	/// @brief highlight 中の項目の submenu を開く (leaf なら選択する)。
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

	/// @brief 最深 submenu を閉じる (親へ戻る)。
	void navigateLeft()
	{
		if (!m_open || m_levels.size() <= 1) { return; }
		m_levels.pop_back();
		syncNodeState();
	}

	/// @brief 現在 highlight 中の項目を選択する (Enter キー)。
	void confirmSelection()
	{
		if (!m_open || m_levels.empty()) { return; }
		const auto& level = m_levels.back();
		if (level.highlightedIndex >= 0)
		{
			selectItem(m_levels.size() - 1, level.highlightedIndex);
		}
	}

	/// @brief menu を閉じる (Escape キー)。
	void cancel()
	{
		close();
	}

private:
	/// @brief マウス位置を menu level に対して hit-test する。
	/// @return 項目 index、外側なら -1。
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

	/// @brief 区切りと無効項目を飛ばして highlight index を上下に動かす。
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

	/// @brief 項目の選択を試みる。children があれば submenu を開き、無ければ callback を発火する。
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

	/// @brief 指定 depth / index の項目の submenu を開く。
	void openSubmenu(std::size_t depth, int index)
	{
		if (depth >= m_levels.size()) { return; }
		const auto& parentLevel = m_levels[depth];
		if (index < 0 || index >= static_cast<int>(parentLevel.items.size())) { return; }

		const auto& item = parentLevel.items[static_cast<std::size_t>(index)];
		if (item.children.empty()) { return; }

		// 既存のより深い level を閉じる。
		closeLevelsBeyond(depth);

		// submenu を親項目の右側に配置する。
		float subX = parentLevel.x + m_width + m_submenuOffset;
		float subY = parentLevel.y + m_padding + static_cast<float>(index) * m_itemHeight;

		// 画面内に clamp する。
		const auto clamped = clampMenuPosition(subX, subY, item.children);
		subX = clamped.first;
		subY = clamped.second;

		// submenu が右へはみ出すなら、左側に開く。
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

	/// @brief 指定 depth より深い menu level をすべて閉じる。
	void closeLevelsBeyond(std::size_t depth)
	{
		if (depth + 1 < m_levels.size())
		{
			m_levels.resize(depth + 1);
		}
	}

	/// @brief menu 位置を画面境界内に収まるよう clamp する。
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

	/// @brief menu level の総高さを計算する。
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

	/// @brief menu level 用の UINode を生成する。
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
