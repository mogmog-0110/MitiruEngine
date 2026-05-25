#pragma once

/// @file UICard.hpp
/// @brief 画像 / タイトル / 説明 / タグ / アクションボタンを束ねる複合 card container widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief UICard が表示する content data。
struct UICardContent
{
	std::string title;                     ///< card タイトル文字列。
	std::string description;               ///< card 説明文字列。
	std::string imageKey;                  ///< ヘッダー画像 key。
	std::vector<std::string> tags;         ///< タグラベル。
	std::vector<std::string> actionLabels; ///< アクションボタンのラベル。
};

/// @brief UICard 生成用の構成設定。
struct UICardConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	float width = 280.0f;                  ///< card の幅。
	float imageHeight = 160.0f;            ///< ヘッダー画像領域の高さ。
	float padding = 12.0f;                 ///< 内側 padding。
	float titleFontSize = 18.0f;           ///< タイトルの font size。
	float descFontSize = 13.0f;            ///< 説明文の font size。
	std::string backgroundImageKey;        ///< card 背景画像 key。
	std::string imageKey;                  ///< 既定のヘッダー画像 key。
	std::string hoverImageKey;             ///< hover 時の背景画像。
	float borderRadius = 8.0f;             ///< 角丸半径。
	float shadowOffsetX = 0.0f;            ///< 影の X offset。
	float shadowOffsetY = 2.0f;            ///< 影の Y offset。
	float shadowBlur = 8.0f;               ///< 影のぼかし半径。
	std::string shadowColor = "00000040";  ///< 影の色 (alpha 付き hex)。
	float elevation = 1.0f;                ///< 影の深さを決める elevation レベル。
	float hoverElevation = 3.0f;           ///< hover 時の elevation。
	float actionBarHeight = 36.0f;         ///< アクションボタンバーの高さ。
	std::string actionBarImageKey;         ///< アクションバー背景画像 key。
	float tagFontSize = 11.0f;             ///< タグラベルの font size。
	float tagHeight = 20.0f;              ///< タグラベルの高さ。
	float tagSpacing = 4.0f;               ///< タグ間の間隔。
};

/// @brief 画像ヘッダー / タイトル / 説明 / タグ / アクションバーを持つ複合 card widget。
///
/// card はアイテム表示、ギャラリー閲覧、コンテンツ要約によく使う。
/// 構成要素: ヘッダー画像、タイトル、説明、任意のタグ、アクションボタン。
/// hover 状態では影 / elevation を強めて浮き上がる効果を出す。
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
	/// @brief 構成設定から card を構築する。
	/// @param config card の構成設定。
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

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在の card content を取得する。
	[[nodiscard]] const UICardContent& content() const noexcept { return m_content; }

	/// @brief card が hover 中か確認する。
	[[nodiscard]] bool isHovered() const noexcept { return m_hovered; }

	// -- 構成設定 -------------------------------------------------------

	/// @brief card クリック時の callback を設定する。
	/// @param callback card クリック時に呼ばれる関数。
	void setOnClicked(std::function<void()> callback) { m_onClicked = std::move(callback); }

	/// @brief アクションクリック時の callback を設定する。
	/// @param callback アクションボタンの index を渡して呼ばれる関数。
	void setOnActionClicked(std::function<void(std::size_t)> callback)
	{
		m_onActionClicked = std::move(callback);
	}

	// -- Content -------------------------------------------------------------

	/// @brief card content を設定する。
	/// @param newContent 新しい content data。
	void setContent(const UICardContent& newContent)
	{
		m_content = newContent;
		rebuildChildren();
		updateLayout();
	}

	// -- Interaction ---------------------------------------------------------

	/// @brief hover とクリック検出のため入力を処理する。
	/// @param pointerInside pointer が card 範囲内にあるか。
	/// @param pointerPressed このフレームで pointer が押されたか。
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

	/// @brief アクションボタンがクリックされたときに呼ばれる。
	/// @param actionIndex アクションボタンの index。
	void onActionClick(std::size_t actionIndex)
	{
		if (actionIndex < m_content.actionLabels.size() && m_onActionClicked)
		{
			m_onActionClicked(actionIndex);
		}
	}

	/// @brief pointer が card 領域に入ったときに呼ばれる。
	void onPointerEnter()
	{
		m_hovered = true;
		m_node->setProperty("hovered", "true");
		m_node->setProperty("current_elevation", std::to_string(m_hoverElevation));
	}

	/// @brief pointer が card 領域から出たときに呼ばれる。
	void onPointerLeave()
	{
		m_hovered = false;
		m_node->setProperty("hovered", "false");
		m_node->setProperty("current_elevation", std::to_string(m_elevation));
	}

	/// @brief card がクリックされたときに呼ばれる。
	void onPointerUp()
	{
		if (m_hovered && m_onClicked)
		{
			m_onClicked();
		}
	}

private:
	/// @brief 現在の content から子 node を再構築する。
	void rebuildChildren()
	{
		// 新しい node tree を組む方針で既存の子を取り除く。
		// UINode に removeAllChildren が無いので、子を手動で再構築する。
		// 現在の node data を保持して作り直す。
		const auto baseId = m_node->id();
		const auto baseName = m_node->name();
		UINodeId childId = baseId + 1;

		// 古い子を 1 つずつ取り除いてクリアする。
		while (m_node->childCount() > 0)
		{
			const auto& children = m_node->children();
			if (!children.empty())
			{
				m_node->removeChild(children[0]->id());
			}
		}

		// 画像の子。
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

		// タイトルの子。
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

		// 説明の子。
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

		// タグの子。
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

		// アクションボタンの子。
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

	/// @brief 現在の content に基づき layout と bounds を更新する。
	void updateLayout()
	{
		float totalHeight = 0.0f;

		// 画像領域。
		if (!m_content.imageKey.empty())
		{
			totalHeight += m_imageHeight;
		}

		// タイトル + 説明 + padding。
		totalHeight += m_padding; // テキスト領域の上 padding。
		if (!m_content.title.empty())
		{
			totalHeight += 24.0f; // タイトル行高の見積もり (renderer は titleFontSize を使う)。
		}
		if (!m_content.description.empty())
		{
			totalHeight += 40.0f; // 説明領域の見積もり。
		}

		// タグ。
		if (!m_content.tags.empty())
		{
			totalHeight += m_tagHeight + m_tagSpacing;
		}

		// アクションバー。
		if (!m_content.actionLabels.empty())
		{
			totalHeight += m_actionBarHeight;
		}

		totalHeight += m_padding; // 下 padding。

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
