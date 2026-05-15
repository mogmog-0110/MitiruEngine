#pragma once

/// @file UICarousel.hpp
/// @brief Swipeable card carousel widget for character select, gallery browse, etc.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Data for a single carousel item.
struct UICarouselItem
{
	std::string imageKey;       ///< Image key for the item content.
	std::string title;          ///< Item title text.
	std::string description;    ///< Item description text.
};

/// @brief Configuration for creating a UICarousel.
struct UICarouselConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::vector<UICarouselItem> items;
	int visibleItems = 3;                   ///< Number of visible items (1, 3, or 5 for preview).
	float itemWidth = 200.0f;               ///< Width of each item.
	float itemHeight = 250.0f;              ///< Height of each item.
	float spacing = 16.0f;                  ///< Horizontal spacing between items.
	float centerScale = 1.2f;               ///< Scale factor for the center (focused) item.
	float sideScale = 0.8f;                 ///< Scale factor for side items.
	float sideAlpha = 0.6f;                 ///< Alpha for side items.
	bool autoPlay = false;                  ///< Automatically advance items.
	float autoPlayInterval = 3.0f;          ///< Seconds between auto-advance.
	bool showDots = true;                   ///< Show dot indicators.
	bool showArrows = true;                 ///< Show left/right arrow buttons.
	std::string arrowLeftImageKey;          ///< Image key for left arrow.
	std::string arrowRightImageKey;         ///< Image key for right arrow.
	std::string dotImageKey;                ///< Image key for inactive dot.
	std::string dotActiveImageKey;          ///< Image key for active dot.
	std::string backgroundImageKey;         ///< Background image key.
	float animationDuration = 0.3f;         ///< Slide animation duration in seconds.
	float swipeThreshold = 50.0f;           ///< Minimum swipe distance to trigger transition.
	bool loopEnabled = true;                ///< Wrap around at ends.
};

/// @brief Swipeable carousel widget with center emphasis, dot indicators, and auto-play.
///
/// Displays a set of items as a horizontal carousel with the center item emphasized.
/// Supports touch/swipe gesture input, arrow buttons, auto-play with pause on interaction,
/// and smooth slide animations.
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
	float m_animProgress = 0.0f;     ///< 0 = at current, 1 = at target.
	float m_animDirection = 0.0f;    ///< -1 = sliding left, +1 = sliding right, 0 = idle.
	float m_animationDuration;
	float m_autoPlayInterval;
	float m_autoPlayTimer = 0.0f;
	bool m_autoPlay;
	bool m_autoPlayPaused = false;
	bool m_loopEnabled;
	float m_swipeThreshold;
	float m_swipeAccum = 0.0f;       ///< Accumulated swipe distance.
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
	/// @brief Construct a carousel from configuration.
	/// @param config Carousel configuration.
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

		// Create child nodes for each item.
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

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current (center) item index.
	[[nodiscard]] int currentIndex() const noexcept { return m_currentIndex; }

	/// @brief Get the number of items.
	[[nodiscard]] std::size_t itemCount() const noexcept { return m_items.size(); }

	/// @brief Check if a slide animation is in progress.
	[[nodiscard]] bool isAnimating() const noexcept { return m_animDirection != 0.0f; }

	// -- Configuration -------------------------------------------------------

	/// @brief Set the item-changed callback.
	/// @param callback Function invoked with the new current index.
	void setOnItemChanged(std::function<void(int)> callback)
	{
		m_onItemChanged = std::move(callback);
	}

	/// @brief Set auto-play enabled state.
	/// @param enabled True to enable auto-play.
	void setAutoPlay(bool enabled)
	{
		m_autoPlay = enabled;
		m_autoPlayTimer = 0.0f;
		m_node->setProperty("auto_play", enabled ? "true" : "false");
	}

	// -- Navigation ----------------------------------------------------------

	/// @brief Navigate to the next item.
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

	/// @brief Navigate to the previous item.
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

	/// @brief Navigate directly to a specific index.
	/// @param index Target item index.
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

	/// @brief Begin a swipe gesture.
	void onSwipeBegin()
	{
		m_swiping = true;
		m_swipeAccum = 0.0f;
		m_autoPlayPaused = true;
	}

	/// @brief Update swipe gesture with a horizontal delta.
	/// @param deltaX Horizontal swipe delta (positive = right).
	void onSwipeUpdate(float deltaX)
	{
		if (!m_swiping) { return; }
		m_swipeAccum += deltaX;
	}

	/// @brief End the swipe gesture and trigger navigation if threshold met.
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

	/// @brief Update animation and auto-play timers.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		// Slide animation.
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

		// Auto-play.
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
	/// @brief Reset auto-play timer (called on user interaction).
	void resetAutoPlayTimer()
	{
		m_autoPlayTimer = 0.0f;
		m_autoPlayPaused = false;
	}

	/// @brief Synchronize state to the UINode tree.
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

			// Compute scale and alpha based on distance from center.
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
