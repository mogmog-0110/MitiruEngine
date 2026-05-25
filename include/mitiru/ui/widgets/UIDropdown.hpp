#pragma once

/// @file UIDropdown.hpp
/// @brief スクロール可能な option リストを持つ dropdown 選択 widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief UIDropdown 生成用の構成設定。
struct UIDropdownConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<std::string> options;
	int selectedIndex = -1;     ///< -1 = 選択なし。
	float width = 180.0f;
	float itemHeight = 24.0f;
	int maxVisibleItems = 6;    ///< スクロール前に表示する最大 item 数。
};

/// @brief UINode を包み、開閉と選択ロジックを持つ dropdown 選択 widget。
///
/// option リスト、開閉状態、はみ出し用の scroll offset、キーボード操作を
/// 管理する。UINode tree は header と、item の子を持つ list container の子
/// から成る (表示中の item のみ生成される)。
///
/// @code
///   UIDropdownConfig cfg;
///   cfg.id = 50;
///   cfg.options = {"Low", "Medium", "High", "Ultra"};
///   cfg.selectedIndex = 1;
///   UIDropdown dropdown(cfg);
///
///   dropdown.setOnSelectionChanged([](int idx) { /* use idx */ });
///   dropdown.toggle();
///   dropdown.selectItem(2);
/// @endcode
class UIDropdown
{
	std::shared_ptr<UINode> m_node;
	std::vector<std::string> m_options;
	int m_selectedIndex;
	int m_highlightedIndex = -1;
	float m_width;
	float m_itemHeight;
	int m_maxVisibleItems;
	int m_scrollOffset = 0;
	bool m_open = false;
	std::function<void(int)> m_onSelectionChanged;

public:
	/// @brief 構成設定から dropdown を構築する。
	/// @param config dropdown の構成設定。
	explicit UIDropdown(const UIDropdownConfig& config)
		: m_options(config.options)
		, m_selectedIndex(config.selectedIndex)
		, m_width(config.width)
		, m_itemHeight(config.itemHeight)
		, m_maxVisibleItems(config.maxVisibleItems)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Dropdown;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.width, config.itemHeight);
		data.properties["widget_type"] = "dropdown";
		data.properties["item_count"] = std::to_string(m_options.size());
		data.properties["max_visible"] = std::to_string(m_maxVisibleItems);

		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			data.text = m_options[static_cast<std::size_t>(m_selectedIndex)];
		}

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief dropdown リストが開いているか確認する。
	[[nodiscard]] bool isOpen() const noexcept { return m_open; }

	/// @brief 現在選択中の index を取得する (なければ -1)。
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief 現在選択中の option のテキストを取得する。
	[[nodiscard]] std::string selectedText() const
	{
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			return m_options[static_cast<std::size_t>(m_selectedIndex)];
		}
		return {};
	}

	/// @brief option リストを取得する。
	[[nodiscard]] const std::vector<std::string>& options() const noexcept { return m_options; }

	/// @brief 現在の scroll offset を取得する。
	[[nodiscard]] int scrollOffset() const noexcept { return m_scrollOffset; }

	/// @brief highlight 中の item index を取得する。
	[[nodiscard]] int highlightedIndex() const noexcept { return m_highlightedIndex; }

	// ── 構成設定 ────────────────────────────────────────

	/// @brief 選択変更時の callback を設定する。
	void setOnSelectionChanged(std::function<void(int)> callback) { m_onSelectionChanged = std::move(callback); }

	/// @brief option リストを差し替える。
	/// @param options 新しい option 文字列。
	void setOptions(std::vector<std::string> options)
	{
		m_options = std::move(options);
		m_scrollOffset = 0;
		m_node->setProperty("item_count", std::to_string(m_options.size()));

		if (m_selectedIndex >= static_cast<int>(m_options.size()))
		{
			m_selectedIndex = m_options.empty() ? -1 : 0;
		}
		syncNodeState();
	}

	/// @brief 選択 index をプログラムから設定する。
	/// @param index 選択する index (-1 でクリア)。
	void setSelectedIndex(int index)
	{
		if (index < -1 || index >= static_cast<int>(m_options.size())) { return; }
		if (m_selectedIndex != index)
		{
			m_selectedIndex = index;
			syncNodeState();
			if (m_onSelectionChanged) { m_onSelectionChanged(m_selectedIndex); }
		}
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief dropdown の開閉を切り替える。
	void toggle()
	{
		m_open = !m_open;
		if (m_open)
		{
			m_highlightedIndex = m_selectedIndex;
			ensureHighlightedVisible();
		}
		syncNodeState();
	}

	/// @brief dropdown リストを開く。
	void open()
	{
		if (!m_open)
		{
			m_open = true;
			m_highlightedIndex = m_selectedIndex;
			ensureHighlightedVisible();
			syncNodeState();
		}
	}

	/// @brief dropdown リストを閉じる。
	void close()
	{
		if (m_open)
		{
			m_open = false;
			syncNodeState();
		}
	}

	/// @brief index で item を選択し dropdown を閉じる。
	/// @param index 選択する item の index。
	void selectItem(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_options.size())) { return; }
		m_selectedIndex = index;
		m_open = false;
		syncNodeState();
		if (m_onSelectionChanged) { m_onSelectionChanged(m_selectedIndex); }
	}

	/// @brief item を highlight する (例: マウス hover 時)。
	/// @param index highlight する item の index。
	void highlightItem(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_options.size())) { return; }
		m_highlightedIndex = index;
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
	}

	/// @brief highlight を上へ移動する (キーボード操作)。
	void highlightPrevious()
	{
		if (m_options.empty()) { return; }
		m_highlightedIndex = (m_highlightedIndex <= 0)
			? static_cast<int>(m_options.size()) - 1
			: m_highlightedIndex - 1;
		ensureHighlightedVisible();
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
	}

	/// @brief highlight を下へ移動する (キーボード操作)。
	void highlightNext()
	{
		if (m_options.empty()) { return; }
		m_highlightedIndex = (m_highlightedIndex >= static_cast<int>(m_options.size()) - 1)
			? 0
			: m_highlightedIndex + 1;
		ensureHighlightedVisible();
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
	}

	/// @brief 現在 highlight 中の item を確定する。
	void confirmHighlighted()
	{
		if (m_highlightedIndex >= 0 && m_highlightedIndex < static_cast<int>(m_options.size()))
		{
			selectItem(m_highlightedIndex);
		}
	}

	/// @brief 表示リストを delta だけスクロールする。
	/// @param delta 正 = 下へスクロール、負 = 上へスクロール。
	void scroll(int delta)
	{
		const int maxScroll = std::max(0, static_cast<int>(m_options.size()) - m_maxVisibleItems);
		m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxScroll);
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

private:
	/// @brief highlight 中の item が表示スクロール範囲内に収まるようにする。
	void ensureHighlightedVisible()
	{
		if (m_highlightedIndex < m_scrollOffset)
		{
			m_scrollOffset = m_highlightedIndex;
		}
		else if (m_highlightedIndex >= m_scrollOffset + m_maxVisibleItems)
		{
			m_scrollOffset = m_highlightedIndex - m_maxVisibleItems + 1;
		}
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}

	/// @brief 状態を UINode に同期する。
	void syncNodeState()
	{
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			m_node->setText(m_options[static_cast<std::size_t>(m_selectedIndex)]);
		}
		else
		{
			m_node->setText("");
		}
		m_node->setProperty("open", m_open ? "true" : "false");
		m_node->setProperty("selected", std::to_string(m_selectedIndex));
		m_node->setProperty("highlighted", std::to_string(m_highlightedIndex));
		m_node->setProperty("scroll_offset", std::to_string(m_scrollOffset));
	}
};

} // namespace mitiru::ui
