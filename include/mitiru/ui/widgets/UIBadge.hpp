#pragma once

/// @file UIBadge.hpp
/// @brief Small counter/notification badge overlay for UI elements.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <string>

namespace mitiru::ui {

/// @brief Badge anchor position relative to the parent element.
enum class BadgePosition : std::uint8_t
{
	TopRight,
	TopLeft,
	BottomRight,
	BottomLeft
};

/// @brief Configuration for creating a UIBadge.
struct UIBadgeConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	int maxCount = 99;                     ///< Counts above this display as "N+".
	float fontSize = 11.0f;                ///< Badge text font size.
	float minSize = 20.0f;                 ///< Minimum badge diameter.
	float padding = 4.0f;                  ///< Horizontal padding inside badge.
	std::string backgroundImageKey;        ///< Badge background image key.
	std::string textColor = "ffffff";       ///< Badge text color (hex, no #).
	std::string backgroundColor = "ff0000"; ///< Badge background color (hex, no #).
	BadgePosition position = BadgePosition::TopRight;  ///< Anchor position.
	float offsetX = 0.0f;                  ///< Horizontal offset from anchor.
	float offsetY = 0.0f;                  ///< Vertical offset from anchor.
	bool pulseAnimation = true;            ///< Pulse on count change.
	bool hideWhenZero = true;              ///< Hide badge when count is 0.
	float pulseDuration = 0.4f;            ///< Duration of pulse animation in seconds.
	float pulseScale = 1.3f;               ///< Maximum scale during pulse.
};

/// @brief Small counter badge overlay for notification counts.
///
/// Displays a circle or rounded-rect with a number, typically attached to
/// another UI element. Shows "N+" for counts exceeding maxCount. Provides
/// a pulse animation on count change.
///
/// @code
///   UIBadgeConfig cfg;
///   cfg.id = 400;
///   cfg.name = "inbox_badge";
///   cfg.maxCount = 99;
///   cfg.position = BadgePosition::TopRight;
///   UIBadge badge(cfg);
///
///   badge.setCount(5);
///   badge.update(dt);
/// @endcode
class UIBadge
{
	std::shared_ptr<UINode> m_node;
	int m_count = 0;
	int m_maxCount;
	float m_minSize;
	float m_padding;
	BadgePosition m_position;
	float m_offsetX;
	float m_offsetY;
	bool m_pulseAnimation;
	bool m_hideWhenZero;
	bool m_visible = true;

	// Pulse animation state.
	float m_pulseDuration;
	float m_pulseScale;
	float m_pulseTimer = 0.0f;
	bool m_pulsing = false;

public:
	/// @brief Construct a badge from configuration.
	/// @param config Badge configuration.
	explicit UIBadge(const UIBadgeConfig& config)
		: m_maxCount(config.maxCount)
		, m_minSize(config.minSize)
		, m_padding(config.padding)
		, m_position(config.position)
		, m_offsetX(config.offsetX)
		, m_offsetY(config.offsetY)
		, m_pulseAnimation(config.pulseAnimation)
		, m_hideWhenZero(config.hideWhenZero)
		, m_pulseDuration(config.pulseDuration)
		, m_pulseScale(config.pulseScale)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Label;
		data.bounds = sgc::Rectf(0.0f, 0.0f, config.minSize, config.minSize);
		data.properties["widget_type"] = "badge";
		data.properties["max_count"] = std::to_string(config.maxCount);
		data.properties["font_size"] = std::to_string(config.fontSize);
		data.properties["min_size"] = std::to_string(config.minSize);
		data.properties["padding"] = std::to_string(config.padding);
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["text_color"] = config.textColor;
		data.properties["background_color"] = config.backgroundColor;
		data.properties["position"] = positionToString(config.position);
		data.properties["offset_x"] = std::to_string(config.offsetX);
		data.properties["offset_y"] = std::to_string(config.offsetY);
		data.properties["pulse_animation"] = config.pulseAnimation ? "true" : "false";
		data.properties["hide_when_zero"] = config.hideWhenZero ? "true" : "false";
		data.properties["pulse_duration"] = std::to_string(config.pulseDuration);
		data.properties["pulse_scale"] = std::to_string(config.pulseScale);

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Get the current count.
	[[nodiscard]] int getCount() const noexcept { return m_count; }

	/// @brief Check if the badge is visible.
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	/// @brief Check if pulse animation is active.
	[[nodiscard]] bool isPulsing() const noexcept { return m_pulsing; }

	/// @brief Get the display text (e.g. "5" or "99+").
	[[nodiscard]] std::string displayText() const
	{
		if (m_count <= 0) { return "0"; }
		if (m_count > m_maxCount)
		{
			return std::to_string(m_maxCount) + "+";
		}
		return std::to_string(m_count);
	}

	// -- Setters -------------------------------------------------------------

	/// @brief Set the badge count.
	/// @param count New count value.
	void setCount(int count)
	{
		const int newCount = std::max(0, count);
		if (newCount == m_count) { return; }

		const bool increased = (newCount > m_count);
		m_count = newCount;

		// Trigger pulse on count increase.
		if (increased && m_pulseAnimation)
		{
			m_pulsing = true;
			m_pulseTimer = 0.0f;
		}

		// Update visibility.
		if (m_hideWhenZero)
		{
			m_visible = (m_count > 0);
		}

		syncNodeState();
	}

	/// @brief Show the badge.
	void show()
	{
		m_visible = true;
		syncNodeState();
	}

	/// @brief Hide the badge.
	void hide()
	{
		m_visible = false;
		syncNodeState();
	}

	// -- Update --------------------------------------------------------------

	/// @brief Update pulse animation.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		if (!m_pulsing) { return; }

		m_pulseTimer += dt;
		if (m_pulseTimer >= m_pulseDuration)
		{
			m_pulsing = false;
			m_pulseTimer = 0.0f;
			m_node->setProperty("pulse_progress", "0");
			m_node->setProperty("current_scale", "1");
			return;
		}

		const float progress = m_pulseTimer / m_pulseDuration;
		// Sine wave: scale up then back down.
		const float scaleFactor = 1.0f +
			(m_pulseScale - 1.0f) * std::sin(progress * std::numbers::pi_v<float>);

		m_node->setProperty("pulse_progress", std::to_string(progress));
		m_node->setProperty("current_scale", std::to_string(scaleFactor));
	}

private:
	/// @brief Convert position enum to string.
	[[nodiscard]] static const char* positionToString(BadgePosition pos) noexcept
	{
		switch (pos)
		{
		case BadgePosition::TopRight:    return "top_right";
		case BadgePosition::TopLeft:     return "top_left";
		case BadgePosition::BottomRight: return "bottom_right";
		case BadgePosition::BottomLeft:  return "bottom_left";
		}
		return "top_right";
	}

	/// @brief Synchronize state to the UINode.
	void syncNodeState()
	{
		m_node->setText(displayText());
		m_node->setVisible(m_visible);
		m_node->setProperty("count", std::to_string(m_count));
	}
};

} // namespace mitiru::ui
