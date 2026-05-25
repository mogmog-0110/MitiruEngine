#pragma once

/// @file UIRadioGroup.hpp
/// @brief 排他選択グループ widget。キーボードナビゲーション対応。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief radio group の並び方向。
enum class RadioOrientation : std::uint8_t
{
	Vertical,
	Horizontal
};

/// @brief 1 つの radio option のデータ。
struct UIRadioOption
{
	std::string label;                ///< 表示ラベル。
	std::string value;                ///< プログラム用の値文字列。
	bool enabled = true;              ///< この option が選択可能か。
	std::string iconImageKey;         ///< 任意のアイコン image key。
};

/// @brief UIRadioGroup 生成用の設定。
struct UIRadioGroupConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIRadioOption> options;
	int selectedIndex = 0;
	RadioOrientation orientation = RadioOrientation::Vertical;
	float spacing = 8.0f;                  ///< option 間の間隔。
	float radioSize = 16.0f;               ///< radio インジケータのサイズ。
	float labelGap = 6.0f;                 ///< インジケータとラベルの間隔。
	std::string radioImageKey;             ///< 未チェック時の radio image。
	std::string radioCheckedImageKey;      ///< チェック時の radio image。
	std::string radioDisabledImageKey;     ///< 無効時の radio image。
	float fontSize = 14.0f;                ///< ラベルの font size。
};

/// @brief 排他選択用の radio group widget。
///
/// 同時にちょうど 1 つだけ選択できる radio option 群を管理する。
/// 上下キーで focus 移動、space で確定するキーボードナビゲーション付き。
///
/// @code
///   UIRadioGroupConfig cfg;
///   cfg.id = 100;
///   cfg.options = {{"Low", "low"}, {"Medium", "med"}, {"High", "high"}};
///   cfg.selectedIndex = 1;
///   UIRadioGroup group(cfg);
///
///   group.setOnSelectionChanged([](int idx, const std::string& val) {
///       // handle selection
///   });
///   group.select(2);
/// @endcode
class UIRadioGroup
{
	std::shared_ptr<UINode> m_node;
	std::vector<UIRadioOption> m_options;
	int m_selectedIndex;
	int m_focusedIndex = 0;
	RadioOrientation m_orientation;
	float m_spacing;
	float m_radioSize;
	float m_labelGap;
	float m_fontSize;
	std::string m_radioImageKey;
	std::string m_radioCheckedImageKey;
	std::string m_radioDisabledImageKey;
	std::function<void(int, const std::string&)> m_onSelectionChanged;

public:
	/// @brief 設定から radio group を構築する。
	/// @param config radio group の設定。
	explicit UIRadioGroup(const UIRadioGroupConfig& config)
		: m_options(config.options)
		, m_selectedIndex(config.selectedIndex)
		, m_orientation(config.orientation)
		, m_spacing(config.spacing)
		, m_radioSize(config.radioSize)
		, m_labelGap(config.labelGap)
		, m_fontSize(config.fontSize)
		, m_radioImageKey(config.radioImageKey)
		, m_radioCheckedImageKey(config.radioCheckedImageKey)
		, m_radioDisabledImageKey(config.radioDisabledImageKey)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Custom;
		data.properties["widget_type"] = "radio_group";
		data.properties["orientation"] = (m_orientation == RadioOrientation::Vertical) ? "vertical" : "horizontal";
		data.properties["spacing"] = std::to_string(m_spacing);
		data.properties["radio_size"] = std::to_string(m_radioSize);
		data.properties["label_gap"] = std::to_string(m_labelGap);
		data.properties["font_size"] = std::to_string(m_fontSize);
		data.properties["radio_image"] = m_radioImageKey;
		data.properties["radio_checked_image"] = m_radioCheckedImageKey;
		data.properties["radio_disabled_image"] = m_radioDisabledImageKey;

		m_node = std::make_shared<UINode>(std::move(data));

		// 各 option に対応する子 node を生成する。
		for (std::size_t i = 0; i < m_options.size(); ++i)
		{
			UINodeData optData;
			optData.id = config.id + static_cast<UINodeId>(i) + 1;
			optData.name = config.name + "_option_" + std::to_string(i);
			optData.role = UIRole::MenuItem;
			optData.text = m_options[i].label;
			optData.properties["value"] = m_options[i].value;
			optData.properties["enabled"] = m_options[i].enabled ? "true" : "false";
			optData.properties["icon_image"] = m_options[i].iconImageKey;
			m_node->addChild(std::make_shared<UINode>(std::move(optData)));
		}

		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			m_focusedIndex = m_selectedIndex;
		}
		syncNodeState();
	}

	/// @brief 基盤となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在選択中の index を取得する。
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief 現在選択中の値文字列を取得する。
	[[nodiscard]] const std::string& selectedValue() const
	{
		static const std::string empty;
		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			return m_options[static_cast<std::size_t>(m_selectedIndex)].value;
		}
		return empty;
	}

	/// @brief focus 中の index を取得する (キーボードナビゲーション)。
	[[nodiscard]] int focusedIndex() const noexcept { return m_focusedIndex; }

	/// @brief option 数を取得する。
	[[nodiscard]] std::size_t optionCount() const noexcept { return m_options.size(); }

	/// @brief option リストを取得する。
	[[nodiscard]] const std::vector<UIRadioOption>& options() const noexcept { return m_options; }

	// ── Configuration ────────────────────────────────────────

	/// @brief 選択変更時の callback を設定する。
	/// @param callback 選択が変わったとき (index, value) で呼ばれる関数。
	void setOnSelectionChanged(std::function<void(int, const std::string&)> callback)
	{
		m_onSelectionChanged = std::move(callback);
	}

	/// @brief option リストを差し替える。
	/// @param options 新しい option 群。
	void setOptions(std::vector<UIRadioOption> options)
	{
		m_options = std::move(options);
		if (m_selectedIndex >= static_cast<int>(m_options.size()))
		{
			m_selectedIndex = m_options.empty() ? -1 : 0;
		}
		m_focusedIndex = std::clamp(m_focusedIndex, 0, std::max(0, static_cast<int>(m_options.size()) - 1));
		syncNodeState();
	}

	// ── Interaction ──────────────────────────────────────────

	/// @brief index で option を選択する。
	/// @param index 選択する option の index。
	void select(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_options.size())) { return; }
		if (!m_options[static_cast<std::size_t>(index)].enabled) { return; }
		if (m_selectedIndex == index) { return; }

		m_selectedIndex = index;
		m_focusedIndex = index;
		syncNodeState();
		if (m_onSelectionChanged)
		{
			m_onSelectionChanged(m_selectedIndex, m_options[static_cast<std::size_t>(m_selectedIndex)].value);
		}
	}

	/// @brief 前の option へ focus を移す (上/左キー)。
	void focusPrevious()
	{
		if (m_options.empty()) { return; }
		const int count = static_cast<int>(m_options.size());
		int next = (m_focusedIndex - 1 + count) % count;

		// 無効な option は skip する (最大でも 1 周)。
		for (int i = 0; i < count; ++i)
		{
			if (m_options[static_cast<std::size_t>(next)].enabled) { break; }
			next = (next - 1 + count) % count;
		}
		m_focusedIndex = next;
		syncNodeState();
	}

	/// @brief 次の option へ focus を移す (下/右キー)。
	void focusNext()
	{
		if (m_options.empty()) { return; }
		const int count = static_cast<int>(m_options.size());
		int next = (m_focusedIndex + 1) % count;

		// 無効な option は skip する (最大でも 1 周)。
		for (int i = 0; i < count; ++i)
		{
			if (m_options[static_cast<std::size_t>(next)].enabled) { break; }
			next = (next + 1) % count;
		}
		m_focusedIndex = next;
		syncNodeState();
	}

	/// @brief 現在 focus 中の option を確定する (Space キー)。
	void confirmFocused()
	{
		select(m_focusedIndex);
	}

private:
	/// @brief 選択状態を UINode tree に同期する。
	void syncNodeState()
	{
		m_node->setProperty("selected", std::to_string(m_selectedIndex));
		m_node->setProperty("focused", std::to_string(m_focusedIndex));
		m_node->setProperty("option_count", std::to_string(m_options.size()));

		if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_options.size()))
		{
			m_node->setProperty("selected_value", m_options[static_cast<std::size_t>(m_selectedIndex)].value);
		}

		// 子 node に checked/unchecked を反映する。
		const auto& children = m_node->children();
		for (std::size_t i = 0; i < children.size() && i < m_options.size(); ++i)
		{
			const bool checked = (static_cast<int>(i) == m_selectedIndex);
			const bool focused = (static_cast<int>(i) == m_focusedIndex);
			children[i]->setProperty("checked", checked ? "true" : "false");
			children[i]->setProperty("focused", focused ? "true" : "false");
			children[i]->setProperty("enabled", m_options[i].enabled ? "true" : "false");
		}
	}
};

} // namespace mitiru::ui
