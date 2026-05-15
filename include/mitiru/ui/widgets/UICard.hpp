#pragma once

/// @file UICard.hpp
/// @brief Composite card container widget with image, title, description, tags, and action buttons.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Content data for a UICard.
struct UICardContent
{
	std::string title;                     ///< Card title text.
	std::string description;               ///< Card description text.
	std::string imageKey;                  ///< Header image key.
	std::vector<std::string> tags;         ///< Tag labels.
	std::vector<std::string> actionLabels; ///< Action button labels.
};

/// @brief Configuration for creating a UICard.
struct UICardConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float width = 280.0f;                  ///< Card width.
	float imageHeight = 160.0f;            ///< Height of the header image area.
	float padding = 12.0f;                 ///< Internal padding.
	float titleFontSize = 18.0f;           ///< Title font size.
	float descFontSize = 13.0f;            ///< Description font size.
	std::string backgroundImageKey;        ///< Card background image key.
	std::string imageKey;                  ///< Default header image key.
	std::string hoverImageKey;             ///< Background image on hover.
	float borderRadius = 8.0f;             ///< Corner radius.
	float shadowOffsetX = 0.0f;            ///< Shadow X offset.
	float shadowOffsetY = 2.0f;            ///< Shadow Y offset.
	float shadowBlur = 8.0f;               ///< Shadow blur radius.
	std::string shadowColor = "00000040";  ///< Shadow color (hex with alpha).
	float elevation = 1.0f;                ///< Elevation level for shadow depth.
	float hoverElevation = 3.0f;           ///< Elevation when hovered.
	float actionBarHeight = 36.0f;         ///< Height of the action button bar.
	std::string actionBarImageKey;         ///< Action bar background image key.
	float tagFontSize = 11.0f;             ///< Tag label font size.
	float tagHeight = 20.0f;              ///< Height of tag labels.
	float tagSpacing = 4.0f;               ///< Spacing between tags.
};

/// @brief Composite card widget with image header, title, description, tags, and action bar.
///
/// Cards are commonly used for item displays, gallery views, and content summaries.
/// The card consists of: header image, title, description, optional tags, and action buttons.
/// Hover state increases shadow/elevation for a lift effect.
///
/// @code
///   UICardConfig cfg;
///   cfg.id = 600;
///   cfg.name = "item_card";
///   cfg.width = 300.0f;
///   UICard card(cfg);
///
///   UICardContent content;
///   content.title = "Enchanted Sword";
///   content.description = "A blade imbued with ancient magic.";
///   content.imageKey = "sword_img";
///   content.tags = {"Rare", "Weapon"};
///   content.actionLabels = {"Equip", "Sell"};
///   card.setContent(content);
///
///   card.setOnClicked([] { /* card clicked */ });
///   card.setOnActionClicked([](std::size_t idx) { /* action at idx */ });
/// @endcode
class UICard
{
	std::shared_ptr<UINode> m_node;
	UICardContent m_content;
	float m_width;
	float m_imageHeight;
	float m_padding;
	float m_elevation;
	float m_hoverElevation;
	float m_actionBarHeight;
	float m_tagHeight;
	float m_tagSpacing;
	bool m_hovered = false;
	std::function<void()> m_onClicked;
	std::function<void(std::size_t)> m_onActionClicked;

public:
	/// @brief Construct a card from configuration.
	/// @param config Card configuration.
	explicit UICard(const UICardConfig& config)
		: m_width(config.width)
		, m_imageHeight(config.imageHeight)
		, m_padding(config.padding)
		, m_elevation(config.elevation)
		, m_hoverElevation(config.hoverElevation)
		, m_actionBarHeight(config.actionBarHeight)
		, m_tagHeight(config.tagHeight)
		, m_tagSpacing(config.tagSpacing)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Panel;
		data.properties["widget_type"] = "card";
		data.properties["width"] = std::to_string(config.width);
		data.properties["image_height"] = std::to_string(config.imageHeight);
		data.properties["padding"] = std::to_string(config.padding);
		data.properties["title_font_size"] = std::to_string(config.titleFontSize);
		data.properties["desc_font_size"] = std::to_string(config.descFontSize);
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["image_key"] = config.imageKey;
		data.properties["hover_image"] = config.hoverImageKey;
		data.properties["border_radius"] = std::to_string(config.borderRadius);
		data.properties["shadow_offset_x"] = std::to_string(config.shadowOffsetX);
		data.properties["shadow_offset_y"] = std::to_string(config.shadowOffsetY);
		data.properties["shadow_blur"] = std::to_string(config.shadowBlur);
		data.properties["shadow_color"] = config.shadowColor;
		data.properties["elevation"] = std::to_string(config.elevation);
		data.properties["hover_elevation"] = std::to_string(config.hoverElevation);
		data.properties["action_bar_height"] = std::to_string(config.actionBarHeight);
		data.properties["action_bar_image"] = config.actionBarImageKey;
		data.properties["tag_font_size"] = std::to_string(config.tagFontSize);
		data.properties["tag_height"] = std::to_string(config.tagHeight);
		data.properties["tag_spacing"] = std::to_string(config.tagSpacing);

		m_node = std::make_shared<UINode>(std::move(data));
		updateLayout();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current card content.
	[[nodiscard]] const UICardContent& content() const noexcept { return m_content; }

	/// @brief Check if the card is hovered.
	[[nodiscard]] bool isHovered() const noexcept { return m_hovered; }

	// -- Configuration -------------------------------------------------------

	/// @brief Set the card-clicked callback.
	/// @param callback Function invoked on card click.
	void setOnClicked(std::function<void()> callback) { m_onClicked = std::move(callback); }

	/// @brief Set the action-clicked callback.
	/// @param callback Function invoked with the action button index.
	void setOnActionClicked(std::function<void(std::size_t)> callback)
	{
		m_onActionClicked = std::move(callback);
	}

	// -- Content -------------------------------------------------------------

	/// @brief Set the card content.
	/// @param newContent New content data.
	void setContent(const UICardContent& newContent)
	{
		m_content = newContent;
		rebuildChildren();
		updateLayout();
	}

	// -- Interaction ---------------------------------------------------------

	/// @brief Process input for hover and click detection.
	/// @param pointerInside Whether the pointer is within card bounds.
	/// @param pointerPressed Whether the pointer is pressed this frame.
	void update(bool pointerInside, bool pointerPressed)
	{
		const bool wasHovered = m_hovered;
		m_hovered = pointerInside;

		if (wasHovered != m_hovered)
		{
			const float currentElevation = m_hovered ? m_hoverElevation : m_elevation;
			m_node->setProperty("current_elevation", std::to_string(currentElevation));
			m_node->setProperty("hovered", m_hovered ? "true" : "false");
		}

		if (pointerInside && pointerPressed)
		{
			if (m_onClicked)
			{
				m_onClicked();
			}
		}
	}

	/// @brief Called when an action button is clicked.
	/// @param actionIndex Index of the action button.
	void onActionClick(std::size_t actionIndex)
	{
		if (actionIndex < m_content.actionLabels.size() && m_onActionClicked)
		{
			m_onActionClicked(actionIndex);
		}
	}

	/// @brief Called when the pointer enters the card area.
	void onPointerEnter()
	{
		m_hovered = true;
		m_node->setProperty("hovered", "true");
		m_node->setProperty("current_elevation", std::to_string(m_hoverElevation));
	}

	/// @brief Called when the pointer leaves the card area.
	void onPointerLeave()
	{
		m_hovered = false;
		m_node->setProperty("hovered", "false");
		m_node->setProperty("current_elevation", std::to_string(m_elevation));
	}

	/// @brief Called when the card is clicked.
	void onPointerUp()
	{
		if (m_hovered && m_onClicked)
		{
			m_onClicked();
		}
	}

private:
	/// @brief Rebuild child nodes from current content.
	void rebuildChildren()
	{
		// Remove existing children by building a new node tree approach.
		// Since UINode doesn't have removeAllChildren, we rebuild children manually.
		// Store current node data and recreate.
		const auto baseId = m_node->id();
		const auto baseName = m_node->name();
		UINodeId childId = baseId + 1;

		// Clear old children by removing them individually.
		while (m_node->childCount() > 0)
		{
			const auto& children = m_node->children();
			if (!children.empty())
			{
				m_node->removeChild(children[0]->id());
			}
		}

		// Image child.
		if (!m_content.imageKey.empty())
		{
			UINodeData imgData;
			imgData.id = childId++;
			imgData.name = baseName + "_image";
			imgData.role = UIRole::Image;
			imgData.properties["image_key"] = m_content.imageKey;
			imgData.properties["part"] = "image";
			m_node->addChild(std::make_shared<UINode>(std::move(imgData)));
		}

		// Title child.
		if (!m_content.title.empty())
		{
			UINodeData titleData;
			titleData.id = childId++;
			titleData.name = baseName + "_title";
			titleData.role = UIRole::Label;
			titleData.text = m_content.title;
			titleData.properties["part"] = "title";
			m_node->addChild(std::make_shared<UINode>(std::move(titleData)));
		}

		// Description child.
		if (!m_content.description.empty())
		{
			UINodeData descData;
			descData.id = childId++;
			descData.name = baseName + "_description";
			descData.role = UIRole::Label;
			descData.text = m_content.description;
			descData.properties["part"] = "description";
			m_node->addChild(std::make_shared<UINode>(std::move(descData)));
		}

		// Tag children.
		for (std::size_t i = 0; i < m_content.tags.size(); ++i)
		{
			UINodeData tagData;
			tagData.id = childId++;
			tagData.name = baseName + "_tag_" + std::to_string(i);
			tagData.role = UIRole::Label;
			tagData.text = m_content.tags[i];
			tagData.properties["part"] = "tag";
			tagData.properties["tag_index"] = std::to_string(i);
			m_node->addChild(std::make_shared<UINode>(std::move(tagData)));
		}

		// Action button children.
		for (std::size_t i = 0; i < m_content.actionLabels.size(); ++i)
		{
			UINodeData actionData;
			actionData.id = childId++;
			actionData.name = baseName + "_action_" + std::to_string(i);
			actionData.role = UIRole::Button;
			actionData.text = m_content.actionLabels[i];
			actionData.properties["part"] = "action";
			actionData.properties["action_index"] = std::to_string(i);
			m_node->addChild(std::make_shared<UINode>(std::move(actionData)));
		}
	}

	/// @brief Update layout and bounds based on current content.
	void updateLayout()
	{
		float totalHeight = 0.0f;

		// Image area.
		if (!m_content.imageKey.empty())
		{
			totalHeight += m_imageHeight;
		}

		// Title + description + padding.
		totalHeight += m_padding; // Top padding for text area.
		if (!m_content.title.empty())
		{
			totalHeight += 24.0f; // Estimated title line height (renderer uses titleFontSize).
		}
		if (!m_content.description.empty())
		{
			totalHeight += 40.0f; // Estimated description area.
		}

		// Tags.
		if (!m_content.tags.empty())
		{
			totalHeight += m_tagHeight + m_tagSpacing;
		}

		// Action bar.
		if (!m_content.actionLabels.empty())
		{
			totalHeight += m_actionBarHeight;
		}

		totalHeight += m_padding; // Bottom padding.

		m_node->setBounds(sgc::Rectf(0.0f, 0.0f, m_width, totalHeight));
		m_node->setProperty("current_elevation", std::to_string(m_elevation));
		m_node->setProperty("hovered", "false");
		m_node->setProperty("title", m_content.title);
		m_node->setProperty("description", m_content.description);
		m_node->setProperty("content_image_key", m_content.imageKey);
		m_node->setProperty("tag_count", std::to_string(m_content.tags.size()));
		m_node->setProperty("action_count", std::to_string(m_content.actionLabels.size()));
		m_node->setText(m_content.title);
	}
};

} // namespace mitiru::ui
