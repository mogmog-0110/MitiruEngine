#pragma once

/// @file UITabBar.hpp
/// @brief content パネルを切り替えるための tab bar widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief content に対する tab bar の配置位置。
enum class TabBarOrientation : std::uint8_t
{
	Top,
	Bottom,
	Left,
	Right
};

/// @brief UITabBar 生成用の設定。
struct UITabBarConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<std::string> tabs;
	int selectedIndex = 0;
	TabBarOrientation orientation = TabBarOrientation::Top;
	float tabWidth = 0.0f;    ///< 0 = テキストから自動サイズ。
	float tabHeight = 32.0f;
	float totalWidth = 400.0f; ///< bar の全体幅 (横) または高さ (縦)。
};

/// @brief UINode をラップし、tab 切り替えロジックを持つ tab bar widget。
///
/// どの tab がアクティブかを管理し、変更時に callback で通知する。
/// 個々の tab node は子として生成され、アクティブなものは renderer が
/// ハイライトできるよう mark される。
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
	/// @brief 設定から tab bar を構築する。
	/// @param config tab bar の設定。
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

		// 各 tab に対応する子 node を生成する。
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

	/// @brief 基盤となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在選択中の tab index を取得する。
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief 現在選択中の tab のテキストを取得する。
	[[nodiscard]] const std::string& selectedTabText() const
	{
		static const std::string empty;
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_tabs.size()))
		{
			return m_tabs[static_cast<std::size_t>(m_selectedIndex)];
		}
		return empty;
	}

	/// @brief tab ラベル群を取得する。
	[[nodiscard]] const std::vector<std::string>& tabs() const noexcept { return m_tabs; }

	/// @brief tab 数を取得する。
	[[nodiscard]] std::size_t tabCount() const noexcept { return m_tabs.size(); }

	// ── Configuration ────────────────────────────────────────

	/// @brief tab 変更時の callback を設定する。
	void setOnTabChanged(std::function<void(int)> callback) { m_onTabChanged = std::move(callback); }

	/// @brief tab リストを差し替える。
	/// @param tabs 新しい tab ラベル群。
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

	/// @brief index で tab を選択する。
	/// @param index 選択する tab の index。
	void selectTab(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_tabs.size())) { return; }
		if (m_selectedIndex == index) { return; }

		m_selectedIndex = index;
		syncNodeState();
		if (m_onTabChanged) { m_onTabChanged(m_selectedIndex); }
	}

	/// @brief 次の tab を選択する (端で折り返す)。
	void selectNext()
	{
		if (m_tabs.empty()) { return; }
		selectTab((m_selectedIndex + 1) % static_cast<int>(m_tabs.size()));
	}

	/// @brief 前の tab を選択する (端で折り返す)。
	void selectPrevious()
	{
		if (m_tabs.empty()) { return; }
		const int count = static_cast<int>(m_tabs.size());
		selectTab((m_selectedIndex - 1 + count) % count);
	}

private:
	/// @brief orientation enum を文字列に変換する。
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

	/// @brief アクティブな tab の state を UINode tree に同期する。
	void syncNodeState()
	{
		m_node->setProperty("selected", std::to_string(m_selectedIndex));
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_tabs.size()))
		{
			m_node->setText(m_tabs[static_cast<std::size_t>(m_selectedIndex)]);
		}

		// 子 tab node に active/inactive を反映する。
		const auto& children = m_node->children();
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const bool active = (static_cast<int>(i) == m_selectedIndex);
			children[i]->setProperty("active", active ? "true" : "false");
		}
	}
};

} // namespace mitiru::ui
