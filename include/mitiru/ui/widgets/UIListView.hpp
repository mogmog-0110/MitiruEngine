#pragma once

/// @file UIListView.hpp
/// @brief スクロール可能な list view widget。virtual scrolling と選択に対応。

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

/// @brief 単一 list view item のデータモデル。
struct UIListItem
{
	std::string text;            ///< 表示テキスト。
	std::string icon;            ///< icon 識別子 (renderer が解釈)。
	std::any userData;           ///< 任意のユーザーデータ。
};

/// @brief UIListView 生成用の設定。
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

/// @brief virtual scrolling と単一 / 複数選択を備えた list view widget。
///
/// visible な item のみ UINode の child として公開され、大規模データセットも
/// 効率的に描画できる。scroll offset がデータモデルのどの slice を
/// 実体化するかを決める。
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
	/// @brief 設定から list view を構築する。
	/// @param config list view 設定。
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

	/// @brief 基となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief item の総数を取得する。
	[[nodiscard]] std::size_t itemCount() const noexcept { return m_items.size(); }

	/// @brief item データモデルを取得する。
	[[nodiscard]] const std::vector<UIListItem>& items() const noexcept { return m_items; }

	/// @brief 選択中の index の集合を取得する。
	[[nodiscard]] const std::set<int>& selectedIndices() const noexcept { return m_selectedIndices; }

	/// @brief 単一の選択 index を取得する (集合の先頭、なければ -1)。
	[[nodiscard]] int selectedIndex() const noexcept
	{
		return m_selectedIndices.empty() ? -1 : *m_selectedIndices.begin();
	}

	/// @brief focus 中 (keyboard 操作対象) の index を取得する。
	[[nodiscard]] int focusedIndex() const noexcept { return m_focusedIndex; }

	/// @brief 現在の scroll offset を取得する (最初の visible item の index)。
	[[nodiscard]] int scrollOffset() const noexcept { return m_scrollOffset; }

	/// @brief 現在 visible な item index の範囲 [start, end) を取得する。
	[[nodiscard]] std::pair<int, int> visibleRange() const noexcept
	{
		const int start = m_scrollOffset;
		const int end = std::min(m_scrollOffset + m_visibleItems, static_cast<int>(m_items.size()));
		return {start, end};
	}

	// ── Data Manipulation ────────────────────────────────────

	/// @brief list の末尾に item を追加する。
	/// @param item 追加する item。
	void addItem(UIListItem item)
	{
		m_items.push_back(std::move(item));
		syncNodeState();
	}

	/// @brief 指定 index に item を挿入する。
	/// @param index 挿入位置。
	/// @param item 挿入する item。
	void insertItem(std::size_t index, UIListItem item)
	{
		if (index > m_items.size()) { index = m_items.size(); }
		m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
		syncNodeState();
	}

	/// @brief index 指定で item を削除する。
	/// @param index 削除する item の index。
	void removeItem(std::size_t index)
	{
		if (index >= m_items.size()) { return; }
		m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));
		m_selectedIndices.erase(static_cast<int>(index));
		syncNodeState();
	}

	/// @brief 全 item をクリアする。
	void clearItems()
	{
		m_items.clear();
		m_selectedIndices.clear();
		m_focusedIndex = -1;
		m_scrollOffset = 0;
		syncNodeState();
	}

	/// @brief item リスト全体を設定する。
	/// @param items 新しい item の vector。
	void setItems(std::vector<UIListItem> items)
	{
		m_items = std::move(items);
		m_selectedIndices.clear();
		m_focusedIndex = -1;
		m_scrollOffset = 0;
		syncNodeState();
	}

	// ── Configuration ────────────────────────────────────────

	/// @brief item 選択時の callback を設定する。
	void setOnItemSelected(std::function<void(int)> callback) { m_onItemSelected = std::move(callback); }

	// ── Selection ────────────────────────────────────────────

	/// @brief index 指定で item を選択する。
	/// @param index item の index。
	/// @param addToSelection true かつ multiSelect 有効なら現在の選択に追加する。
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

	/// @brief index 指定で item の選択を解除する。
	/// @param index item の index。
	void deselectItem(int index)
	{
		m_selectedIndices.erase(index);
		syncNodeState();
	}

	/// @brief 全選択をクリアする。
	void clearSelection()
	{
		m_selectedIndices.clear();
		syncNodeState();
	}

	/// @brief item が選択されているか判定する。
	/// @param index item の index。
	[[nodiscard]] bool isSelected(int index) const
	{
		return m_selectedIndices.count(index) > 0;
	}

	// ── Navigation ───────────────────────────────────────────

	/// @brief focus を前の item へ移動する (Up key)。
	void focusPrevious()
	{
		if (m_items.empty()) { return; }
		m_focusedIndex = (m_focusedIndex <= 0)
			? static_cast<int>(m_items.size()) - 1
			: m_focusedIndex - 1;
		ensureFocusedVisible();
		syncNodeState();
	}

	/// @brief focus を次の item へ移動する (Down key)。
	void focusNext()
	{
		if (m_items.empty()) { return; }
		m_focusedIndex = (m_focusedIndex >= static_cast<int>(m_items.size()) - 1)
			? 0
			: m_focusedIndex + 1;
		ensureFocusedVisible();
		syncNodeState();
	}

	/// @brief 現在 focus 中の item を選択する (Enter key)。
	void confirmFocused()
	{
		if (m_focusedIndex >= 0 && m_focusedIndex < static_cast<int>(m_items.size()))
		{
			selectItem(m_focusedIndex);
		}
	}

	/// @brief 指定 item 数だけスクロールする。
	/// @param delta 正 = 下スクロール、負 = 上スクロール。
	void scroll(int delta)
	{
		const int maxScroll = std::max(0, static_cast<int>(m_items.size()) - m_visibleItems);
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxScroll);
		syncNodeState();
	}

private:
	/// @brief focus 中の item が visible なスクロール範囲に収まるようにする。
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

	/// @brief 状態を UINode の properties へ同期する。
	void syncNodeState()
	{
		m_node->setProperty("item_count", std::to_string(m_items.size()));
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
		m_node->setProperty("focused", std::to_string(m_focusedIndex));

		// 選択中の index をカンマ区切り文字列に encode する。
		std::string selStr;
		for (const auto idx : m_selectedIndices)
		{
			if (!selStr.empty()) { selStr += ","; }
			selStr += std::to_string(idx);
		}
		m_node->setProperty("selected", selStr);

		// renderer の便宜のため visible な item のテキストを encode する。
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
