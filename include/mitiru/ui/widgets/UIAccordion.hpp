#pragma once

/// @file UIAccordion.hpp
/// @brief Collapsible accordion widget for settings panels, FAQ sections, etc.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Data for a single accordion section.
struct UIAccordionSection
{
	std::string title;                  ///< Section header text.
	bool expanded = false;              ///< Whether the section is expanded.
	std::string headerImageKey;         ///< Image key for the header background.
	float contentHeight = 100.0f;       ///< Height of the content area when expanded.
	std::string iconImageKey;           ///< Icon image key displayed in the header.
};

/// @brief Configuration for creating a UIAccordion.
struct UIAccordionConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UIAccordionSection> sections;
	bool allowMultipleOpen = false;         ///< Allow multiple sections open simultaneously.
	float headerHeight = 40.0f;             ///< Height of each section header.
	std::string headerBackgroundImageKey;   ///< Default header background image key.
	std::string headerHoverImageKey;        ///< Header background on hover.
	std::string headerActiveImageKey;       ///< Header background when section is expanded.
	std::string expandIconImageKey;         ///< Icon shown when section is collapsed (expand arrow).
	std::string collapseIconImageKey;       ///< Icon shown when section is expanded (collapse arrow).
	std::string contentBackgroundImageKey;  ///< Background image key for content area.
	float animationDuration = 0.25f;        ///< Duration of expand/collapse animation in seconds.
	float spacing = 0.0f;                   ///< Vertical spacing between sections.
	float padding = 8.0f;                   ///< Internal padding for content areas.
	float fontSize = 14.0f;                 ///< Font size for content text.
	float headerFontSize = 16.0f;           ///< Font size for header text.
	float width = 300.0f;                   ///< Total width of the accordion.
};

/// @brief Collapsible accordion widget with animated expand/collapse transitions.
///
/// Each section has a clickable header that toggles visibility of its content.
/// Supports exclusive mode (only one section open) and smooth height animation.
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
	/// @brief Runtime state for a single section's animation.
	struct SectionState
	{
		UIAccordionSection section;
		float animProgress = 0.0f;   ///< 0 = collapsed, 1 = expanded.
		float targetProgress = 0.0f; ///< Animation target (0 or 1).
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
	/// @brief Construct an accordion from configuration.
	/// @param config Accordion configuration.
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the number of sections.
	[[nodiscard]] std::size_t sectionCount() const noexcept { return m_sections.size(); }

	/// @brief Check if a section is expanded.
	/// @param index Section index.
	/// @return True if expanded or animating toward expanded.
	[[nodiscard]] bool isExpanded(std::size_t index) const noexcept
	{
		if (index >= m_sections.size()) { return false; }
		return m_sections[index].section.expanded;
	}

	/// @brief Get the current animation progress for a section (0 = collapsed, 1 = expanded).
	/// @param index Section index.
	[[nodiscard]] float animProgress(std::size_t index) const noexcept
	{
		if (index >= m_sections.size()) { return 0.0f; }
		return m_sections[index].animProgress;
	}

	// -- Configuration -------------------------------------------------------

	/// @brief Set the section-toggled callback.
	/// @param callback Function invoked with (sectionIndex, expanded).
	void setOnSectionToggled(std::function<void(std::size_t, bool)> callback)
	{
		m_onSectionToggled = std::move(callback);
	}

	// -- Interaction ---------------------------------------------------------

	/// @brief Toggle a section's expanded state.
	/// @param index Section index.
	void toggle(std::size_t index)
	{
		if (index >= m_sections.size()) { return; }

		auto& target = m_sections[index];
		target.section.expanded = !target.section.expanded;
		target.targetProgress = target.section.expanded ? 1.0f : 0.0f;

		// Exclusive mode: collapse all others when opening.
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

	/// @brief Expand all sections (ignored if allowMultipleOpen is false).
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

	/// @brief Collapse all sections.
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

	/// @brief Update animation state.
	/// @param dt Delta time in seconds.
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
	/// @brief Synchronize all section states to the UINode tree.
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
