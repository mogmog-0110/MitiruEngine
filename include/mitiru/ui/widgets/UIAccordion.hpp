#pragma once

/// @file UIAccordion.hpp
/// @brief 折り畳み可能な accordion widget。設定パネルや FAQ セクション等向け。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief 1 つの accordion section のデータ。
struct UIAccordionSection
{
	std::string title;                  ///< section ヘッダのテキスト。
	bool expanded = false;              ///< section が展開されているか。
	std::string headerImageKey;         ///< ヘッダ背景の image key。
	float contentHeight = 100.0f;       ///< 展開時の content 領域の高さ。
	std::string iconImageKey;           ///< ヘッダに表示するアイコンの image key。
};

/// @brief UIAccordion 生成用の設定。
struct UIAccordionConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIAccordionSection> sections;
	bool allowMultipleOpen = false;         ///< 複数 section の同時展開を許可する。
	float headerHeight = 40.0f;             ///< 各 section ヘッダの高さ。
	std::string headerBackgroundImageKey;   ///< デフォルトのヘッダ背景 image key。
	std::string headerHoverImageKey;        ///< hover 時のヘッダ背景。
	std::string headerActiveImageKey;       ///< section 展開時のヘッダ背景。
	std::string expandIconImageKey;         ///< section が折り畳み時に表示するアイコン (展開矢印)。
	std::string collapseIconImageKey;       ///< section が展開時に表示するアイコン (折り畳み矢印)。
	std::string contentBackgroundImageKey;  ///< content 領域の背景 image key。
	float animationDuration = 0.25f;        ///< 展開/折り畳みアニメーションの秒数。
	float spacing = 0.0f;                   ///< section 間の縦方向の間隔。
	float padding = 8.0f;                   ///< content 領域の内側 padding。
	float fontSize = 14.0f;                 ///< content テキストの font size。
	float headerFontSize = 16.0f;           ///< ヘッダテキストの font size。
	float width = 300.0f;                   ///< accordion の全体幅。
};

/// @brief 展開/折り畳みアニメーション付きの折り畳み可能な accordion widget。
///
/// 各 section にはクリック可能なヘッダがあり、その content の表示を切り替える。
/// 排他 mode (1 section のみ展開) と滑らかな高さアニメーションに対応する。
///
/// @code
///   UIAccordionConfig cfg;
///   cfg.id = 100;
///   cfg.name = "settings_accordion";
///   cfg.sections = {
///       {"Graphics", false, "", 120.0f, ""},
///       {"Audio",    false, "", 80.0f,  ""},
///       {"Controls", false, "", 100.0f, ""},
///   };
///   cfg.allowMultipleOpen = false;
///   UIAccordion accordion(cfg);
///
///   accordion.setOnSectionToggled([](std::size_t idx, bool expanded) {
///       // handle section toggle
///   });
///   accordion.toggle(0);
/// @endcode
class UIAccordion
{
	/// @brief 1 section のアニメーションの実行時 state。
	struct SectionState
	{
		UIAccordionSection section;
		float animProgress = 0.0f;   ///< 0 = 折り畳み、1 = 展開。
		float targetProgress = 0.0f; ///< アニメーションの target (0 または 1)。
	};

	std::shared_ptr<UINode> m_node;
	std::vector<SectionState> m_sections;
	bool m_allowMultipleOpen;
	float m_headerHeight;
	float m_animationDuration;
	float m_spacing;
	float m_width;
	std::function<void(std::size_t, bool)> m_onSectionToggled;

public:
	/// @brief 設定から accordion を構築する。
	/// @param config accordion の設定。
	explicit UIAccordion(const UIAccordionConfig& config)
		: m_allowMultipleOpen(config.allowMultipleOpen)
		, m_headerHeight(config.headerHeight)
		, m_animationDuration(config.animationDuration)
		, m_spacing(config.spacing)
		, m_width(config.width)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Container;
		data.properties["widget_type"] = "accordion";
		data.properties["allow_multiple_open"] = config.allowMultipleOpen ? "true" : "false";
		data.properties["header_height"] = std::to_string(config.headerHeight);
		data.properties["animation_duration"] = std::to_string(config.animationDuration);
		data.properties["spacing"] = std::to_string(config.spacing);
		data.properties["padding"] = std::to_string(config.padding);
		data.properties["font_size"] = std::to_string(config.fontSize);
		data.properties["header_font_size"] = std::to_string(config.headerFontSize);
		data.properties["header_bg_image"] = config.headerBackgroundImageKey;
		data.properties["header_hover_image"] = config.headerHoverImageKey;
		data.properties["header_active_image"] = config.headerActiveImageKey;
		data.properties["expand_icon_image"] = config.expandIconImageKey;
		data.properties["collapse_icon_image"] = config.collapseIconImageKey;
		data.properties["content_bg_image"] = config.contentBackgroundImageKey;

		m_node = std::make_shared<UINode>(std::move(data));

		m_sections.reserve(config.sections.size());
		for (std::size_t i = 0; i < config.sections.size(); ++i)
		{
			SectionState state;
			state.section = config.sections[i];
			state.animProgress = state.section.expanded ? 1.0f : 0.0f;
			state.targetProgress = state.animProgress;
			m_sections.push_back(std::move(state));

			UINodeData sectionData;
			sectionData.id = config.id + static_cast<UINodeId>(i) + 1;
			sectionData.name = config.name + "_section_" + std::to_string(i);
			sectionData.role = UIRole::Panel;
			sectionData.text = config.sections[i].title;
			sectionData.properties["section_index"] = std::to_string(i);
			sectionData.properties["icon_image"] = config.sections[i].iconImageKey;
			sectionData.properties["header_image"] = config.sections[i].headerImageKey;
			sectionData.properties["content_height"] = std::to_string(config.sections[i].contentHeight);
			m_node->addChild(std::make_shared<UINode>(std::move(sectionData)));
		}

		syncNodeState();
	}

	/// @brief 基盤となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief section 数を取得する。
	[[nodiscard]] std::size_t sectionCount() const noexcept { return m_sections.size(); }

	/// @brief section が展開されているか確認する。
	/// @param index section の index。
	/// @return 展開済み、または展開方向にアニメーション中なら true。
	[[nodiscard]] bool isExpanded(std::size_t index) const noexcept
	{
		if (index >= m_sections.size()) { return false; }
		return m_sections[index].section.expanded;
	}

	/// @brief section の現在のアニメーション進捗を取得する (0 = 折り畳み、1 = 展開)。
	/// @param index section の index。
	[[nodiscard]] float animProgress(std::size_t index) const noexcept
	{
		if (index >= m_sections.size()) { return 0.0f; }
		return m_sections[index].animProgress;
	}

	// -- Configuration -------------------------------------------------------

	/// @brief section 切り替え時の callback を設定する。
	/// @param callback (sectionIndex, expanded) で呼ばれる関数。
	void setOnSectionToggled(std::function<void(std::size_t, bool)> callback)
	{
		m_onSectionToggled = std::move(callback);
	}

	// -- Interaction ---------------------------------------------------------

	/// @brief section の展開状態を切り替える。
	/// @param index section の index。
	void toggle(std::size_t index)
	{
		if (index >= m_sections.size()) { return; }

		auto& target = m_sections[index];
		target.section.expanded = !target.section.expanded;
		target.targetProgress = target.section.expanded ? 1.0f : 0.0f;

		// 排他 mode: 開くとき他の section をすべて折り畳む。
		if (target.section.expanded && !m_allowMultipleOpen)
		{
			for (std::size_t i = 0; i < m_sections.size(); ++i)
			{
				if (i != index && m_sections[i].section.expanded)
				{
					m_sections[i].section.expanded = false;
					m_sections[i].targetProgress = 0.0f;

					if (m_onSectionToggled)
					{
						m_onSectionToggled(i, false);
					}
				}
			}
		}

		syncNodeState();

		if (m_onSectionToggled)
		{
			m_onSectionToggled(index, target.section.expanded);
		}
	}

	/// @brief 全 section を展開する (allowMultipleOpen が false なら無視)。
	void expandAll()
	{
		if (!m_allowMultipleOpen) { return; }

		for (std::size_t i = 0; i < m_sections.size(); ++i)
		{
			if (!m_sections[i].section.expanded)
			{
				m_sections[i].section.expanded = true;
				m_sections[i].targetProgress = 1.0f;

				if (m_onSectionToggled)
				{
					m_onSectionToggled(i, true);
				}
			}
		}
		syncNodeState();
	}

	/// @brief 全 section を折り畳む。
	void collapseAll()
	{
		for (std::size_t i = 0; i < m_sections.size(); ++i)
		{
			if (m_sections[i].section.expanded)
			{
				m_sections[i].section.expanded = false;
				m_sections[i].targetProgress = 0.0f;

				if (m_onSectionToggled)
				{
					m_onSectionToggled(i, false);
				}
			}
		}
		syncNodeState();
	}

	/// @brief アニメーション state を更新する。
	/// @param dt 経過秒数 (delta time)。
	void update(float dt)
	{
		if (m_animationDuration <= 0.0f)
		{
			for (auto& s : m_sections)
			{
				s.animProgress = s.targetProgress;
			}
			syncNodeState();
			return;
		}

		bool changed = false;
		const float step = dt / m_animationDuration;

		for (auto& s : m_sections)
		{
			if (std::abs(s.animProgress - s.targetProgress) > 0.001f)
			{
				if (s.animProgress < s.targetProgress)
				{
					s.animProgress = std::min(s.animProgress + step, s.targetProgress);
				}
				else
				{
					s.animProgress = std::max(s.animProgress - step, s.targetProgress);
				}
				changed = true;
			}
			else
			{
				s.animProgress = s.targetProgress;
			}
		}

		if (changed)
		{
			syncNodeState();
		}
	}

private:
	/// @brief 全 section の state を UINode tree に同期する。
	void syncNodeState()
	{
		m_node->setProperty("section_count", std::to_string(m_sections.size()));

		float totalHeight = 0.0f;
		const auto& children = m_node->children();

		for (std::size_t i = 0; i < m_sections.size() && i < children.size(); ++i)
		{
			const auto& s = m_sections[i];
			auto& child = children[i];

			child->setProperty("expanded", s.section.expanded ? "true" : "false");
			child->setProperty("anim_progress", std::to_string(s.animProgress));

			const float visibleContentHeight = s.section.contentHeight * s.animProgress;
			const float sectionHeight = m_headerHeight + visibleContentHeight;
			child->setBounds(sgc::Rectf(0.0f, totalHeight, m_width, sectionHeight));

			totalHeight += sectionHeight;
			if (i + 1 < m_sections.size())
			{
				totalHeight += m_spacing;
			}
		}

		m_node->setBounds(sgc::Rectf(0.0f, 0.0f, m_width, totalHeight));
	}
};

} // namespace mitiru::ui
