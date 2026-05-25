#pragma once

/// @file UICarousel.hpp
/// @brief キャラ選択やギャラリー閲覧などに使う swipe 可能な card carousel widget。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief carousel item 1 件分のデータ。
struct UICarouselItem
{
	std::string imageKey;       ///< item 内容の画像 key。
	std::string title;          ///< item タイトル文字列。
	std::string description;    ///< item 説明文字列。
};

/// @brief UICarousel 生成用の構成設定。
struct UICarouselConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UICarouselItem> items;
	int visibleItems = 3;                   ///< 表示 item 数 (preview 用に 1、3、5)。
	float itemWidth = 200.0f;               ///< 各 item の幅。
	float itemHeight = 250.0f;              ///< 各 item の高さ。
	float spacing = 16.0f;                  ///< item 間の水平方向の間隔。
	float centerScale = 1.2f;               ///< 中央 (focus 中) item の scale 係数。
	float sideScale = 0.8f;                 ///< 両脇 item の scale 係数。
	float sideAlpha = 0.6f;                 ///< 両脇 item の alpha。
	bool autoPlay = false;                  ///< item を自動送りする。
	float autoPlayInterval = 3.0f;          ///< 自動送りの間隔 (秒)。
	bool showDots = true;                   ///< dot インジケータを表示する。
	bool showArrows = true;                 ///< 左右の矢印ボタンを表示する。
	std::string arrowLeftImageKey;          ///< 左矢印の画像 key。
	std::string arrowRightImageKey;         ///< 右矢印の画像 key。
	std::string dotImageKey;                ///< 非アクティブな dot の画像 key。
	std::string dotActiveImageKey;          ///< アクティブな dot の画像 key。
	std::string backgroundImageKey;         ///< 背景画像 key。
	float animationDuration = 0.3f;         ///< スライド animation の長さ (秒)。
	float swipeThreshold = 50.0f;           ///< 遷移を発火させる最小 swipe 距離。
	bool loopEnabled = true;                ///< 端で折り返す。
};

/// @brief 中央強調・dot インジケータ・auto-play を備えた swipe 可能な carousel widget。
///
/// 複数の item を水平 carousel として表示し、中央の item を強調する。
/// touch / swipe ジェスチャ入力、矢印ボタン、操作時に一時停止する auto-play、
/// 滑らかなスライド animation に対応する。
///
/// @code
///   UICarouselConfig cfg;
///   cfg.id = 200;
///   cfg.name = "character_select";
///   cfg.items = {
///       {"warrior_img", "Warrior", "Melee fighter"},
///       {"mage_img",    "Mage",    "Spell caster"},
///       {"rogue_img",   "Rogue",   "Stealth assassin"},
///   };
///   UICarousel carousel(cfg);
///
///   carousel.setOnItemChanged([](int idx) { /* selection changed */ });
///   carousel.next();
/// @endcode
class UICarousel
{
	std::shared_ptr<UINode> m_node;
	std::vector<UICarouselItem> m_items;
	int m_currentIndex = 0;
	int m_targetIndex = 0;
	float m_animProgress = 0.0f;     ///< 0 = current 位置、1 = target 位置。
	float m_animDirection = 0.0f;    ///< -1 = 左へスライド、+1 = 右へスライド、0 = 停止中。
	float m_animationDuration;
	float m_autoPlayInterval;
	float m_autoPlayTimer = 0.0f;
	bool m_autoPlay;
	bool m_autoPlayPaused = false;
	bool m_loopEnabled;
	float m_swipeThreshold;
	float m_swipeAccum = 0.0f;       ///< 累積した swipe 距離。
	bool m_swiping = false;
	int m_visibleItems;
	float m_itemWidth;
	float m_itemHeight;
	float m_spacing;
	float m_centerScale;
	float m_sideScale;
	float m_sideAlpha;
	std::function<void(int)> m_onItemChanged;

public:
	/// @brief 構成設定から carousel を構築する。
	/// @param config carousel の構成設定。
	explicit UICarousel(const UICarouselConfig& config)
		: m_items(config.items)
		, m_animationDuration(config.animationDuration)
		, m_autoPlayInterval(config.autoPlayInterval)
		, m_autoPlay(config.autoPlay)
		, m_loopEnabled(config.loopEnabled)
		, m_swipeThreshold(config.swipeThreshold)
		, m_visibleItems(config.visibleItems)
		, m_itemWidth(config.itemWidth)
		, m_itemHeight(config.itemHeight)
		, m_spacing(config.spacing)
		, m_centerScale(config.centerScale)
		, m_sideScale(config.sideScale)
		, m_sideAlpha(config.sideAlpha)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Container;
		data.properties["widget_type"] = "carousel";
		data.properties["item_count"] = std::to_string(m_items.size());
		data.properties["visible_items"] = std::to_string(config.visibleItems);
		data.properties["item_width"] = std::to_string(config.itemWidth);
		data.properties["item_height"] = std::to_string(config.itemHeight);
		data.properties["spacing"] = std::to_string(config.spacing);
		data.properties["center_scale"] = std::to_string(config.centerScale);
		data.properties["side_scale"] = std::to_string(config.sideScale);
		data.properties["side_alpha"] = std::to_string(config.sideAlpha);
		data.properties["show_dots"] = config.showDots ? "true" : "false";
		data.properties["show_arrows"] = config.showArrows ? "true" : "false";
		data.properties["arrow_left_image"] = config.arrowLeftImageKey;
		data.properties["arrow_right_image"] = config.arrowRightImageKey;
		data.properties["dot_image"] = config.dotImageKey;
		data.properties["dot_active_image"] = config.dotActiveImageKey;
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["loop_enabled"] = config.loopEnabled ? "true" : "false";

		const float totalWidth = config.itemWidth * static_cast<float>(config.visibleItems)
		                       + config.spacing * static_cast<float>(config.visibleItems - 1);
		data.bounds = sgc::Rectf(0.0f, 0.0f, totalWidth, config.itemHeight * config.centerScale);

		m_node = std::make_shared<UINode>(std::move(data));

		// 各 item の子 node を生成する。
		for (std::size_t i = 0; i < m_items.size(); ++i)
		{
			UINodeData itemData;
			itemData.id = config.id + static_cast<UINodeId>(i) + 1;
			itemData.name = config.name + "_item_" + std::to_string(i);
			itemData.role = UIRole::Image;
			itemData.text = m_items[i].title;
			itemData.properties["item_index"] = std::to_string(i);
			itemData.properties["image_key"] = m_items[i].imageKey;
			itemData.properties["description"] = m_items[i].description;
			m_node->addChild(std::make_shared<UINode>(std::move(itemData)));
		}

		syncNodeState();
	}

	/// @brief 内部の UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief 現在 (中央) の item index を取得する。
	[[nodiscard]] int currentIndex() const noexcept { return m_currentIndex; }

	/// @brief item 数を取得する。
	[[nodiscard]] std::size_t itemCount() const noexcept { return m_items.size(); }

	/// @brief スライド animation 進行中か確認する。
	[[nodiscard]] bool isAnimating() const noexcept { return m_animDirection != 0.0f; }

	// -- Configuration -------------------------------------------------------

	/// @brief item 変更時の callback を設定する。
	/// @param callback 新しい current index を渡して呼ばれる関数。
	void setOnItemChanged(std::function<void(int)> callback)
	{
		m_onItemChanged = std::move(callback);
	}

	/// @brief auto-play の有効状態を設定する。
	/// @param enabled true で auto-play を有効化する。
	void setAutoPlay(bool enabled)
	{
		m_autoPlay = enabled;
		m_autoPlayTimer = 0.0f;
		m_node->setProperty("auto_play", enabled ? "true" : "false");
	}

	// -- Navigation ----------------------------------------------------------

	/// @brief 次の item へ移動する。
	void next()
	{
		if (m_items.empty() || isAnimating()) { return; }

		const int count = static_cast<int>(m_items.size());
		if (!m_loopEnabled && m_currentIndex >= count - 1) { return; }

		m_targetIndex = (m_currentIndex + 1) % count;
		m_animDirection = 1.0f;
		m_animProgress = 0.0f;
		resetAutoPlayTimer();
	}

	/// @brief 前の item へ移動する。
	void previous()
	{
		if (m_items.empty() || isAnimating()) { return; }

		const int count = static_cast<int>(m_items.size());
		if (!m_loopEnabled && m_currentIndex <= 0) { return; }

		m_targetIndex = (m_currentIndex - 1 + count) % count;
		m_animDirection = -1.0f;
		m_animProgress = 0.0f;
		resetAutoPlayTimer();
	}

	/// @brief 指定 index へ直接移動する。
	/// @param index 移動先の item index。
	void goTo(int index)
	{
		if (m_items.empty() || isAnimating()) { return; }

		const int count = static_cast<int>(m_items.size());
		const int clamped = std::clamp(index, 0, count - 1);
		if (clamped == m_currentIndex) { return; }

		m_targetIndex = clamped;
		m_animDirection = (clamped > m_currentIndex) ? 1.0f : -1.0f;
		m_animProgress = 0.0f;
		resetAutoPlayTimer();
	}

	// -- Swipe Input ---------------------------------------------------------

	/// @brief swipe ジェスチャを開始する。
	void onSwipeBegin()
	{
		m_swiping = true;
		m_swipeAccum = 0.0f;
		m_autoPlayPaused = true;
	}

	/// @brief 水平 delta で swipe ジェスチャを更新する。
	/// @param deltaX 水平方向の swipe 差分 (正 = 右)。
	void onSwipeUpdate(float deltaX)
	{
		if (!m_swiping) { return; }
		m_swipeAccum += deltaX;
	}

	/// @brief swipe ジェスチャを終了し、閾値を超えていれば移動を発火する。
	void onSwipeEnd()
	{
		if (!m_swiping) { return; }
		m_swiping = false;
		m_autoPlayPaused = false;

		if (m_swipeAccum < -m_swipeThreshold)
		{
			next();
		}
		else if (m_swipeAccum > m_swipeThreshold)
		{
			previous();
		}
		m_swipeAccum = 0.0f;
	}

	// -- Update --------------------------------------------------------------

	/// @brief animation と auto-play の timer を更新する。
	/// @param dt delta time (秒)。
	void update(float dt)
	{
		// スライド animation。
		if (isAnimating())
		{
			if (m_animationDuration <= 0.0f)
			{
				m_animProgress = 1.0f;
			}
			else
			{
				m_animProgress += dt / m_animationDuration;
			}

			if (m_animProgress >= 1.0f)
			{
				m_animProgress = 0.0f;
				m_animDirection = 0.0f;
				m_currentIndex = m_targetIndex;

				if (m_onItemChanged)
				{
					m_onItemChanged(m_currentIndex);
				}
			}
			syncNodeState();
		}

		// auto-play。
		if (m_autoPlay && !m_autoPlayPaused && !isAnimating() && !m_items.empty())
		{
			m_autoPlayTimer += dt;
			if (m_autoPlayTimer >= m_autoPlayInterval)
			{
				m_autoPlayTimer = 0.0f;
				next();
			}
		}
	}

private:
	/// @brief auto-play timer を reset する (ユーザー操作時に呼ばれる)。
	void resetAutoPlayTimer()
	{
		m_autoPlayTimer = 0.0f;
		m_autoPlayPaused = false;
	}

	/// @brief 状態を UINode tree に同期する。
	void syncNodeState()
	{
		m_node->setProperty("current_index", std::to_string(m_currentIndex));
		m_node->setProperty("target_index", std::to_string(m_targetIndex));
		m_node->setProperty("anim_progress", std::to_string(m_animProgress));
		m_node->setProperty("anim_direction", std::to_string(m_animDirection));

		const auto& children = m_node->children();
		for (std::size_t i = 0; i < children.size(); ++i)
		{
			const int idx = static_cast<int>(i);
			const int offset = idx - m_currentIndex;

			// 中央からの距離に基づき scale と alpha を計算する。
			const float absOffset = static_cast<float>(std::abs(offset));
			const float scale = (offset == 0) ? m_centerScale : m_sideScale;
			const float alpha = (offset == 0) ? 1.0f : m_sideAlpha;
			const bool visible = absOffset <= static_cast<float>(m_visibleItems / 2);

			children[i]->setProperty("offset", std::to_string(offset));
			children[i]->setProperty("scale", std::to_string(scale));
			children[i]->setProperty("alpha", std::to_string(alpha));
			children[i]->setVisible(visible);

			const float x = static_cast<float>(offset) * (m_itemWidth + m_spacing);
			children[i]->setBounds(sgc::Rectf(x, 0.0f, m_itemWidth * scale, m_itemHeight * scale));
		}
	}
};

} // namespace mitiru::ui
