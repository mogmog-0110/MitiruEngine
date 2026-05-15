#pragma once

/// @file UITooltip.hpp
/// @brief Hover popup tooltip widget with auto-positioning, fade animation, and UINode attachment.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief Anchor position relative to the target element.
enum class TooltipPosition : std::uint8_t
{
	Above,
	Below,
	Left,
	Right,
	Auto ///< Automatically choose based on available screen space.
};

/// @brief Configuration for creating a UITooltip.
struct UITooltipConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string text;

	// ── Layout ────────────────────────────────────────────────
	float maxWidth = 300.0f;         ///< Maximum width before text wraps.
	float padding = 8.0f;            ///< Inner padding on all sides.
	float arrowSize = 6.0f;          ///< Size of the directional arrow.
	float anchorOffsetX = 0.0f;      ///< Offset from anchor point X.
	float anchorOffsetY = 0.0f;      ///< Offset from anchor point Y.
	TooltipPosition position = TooltipPosition::Auto;

	// ── Timing ────────────────────────────────────────────────
	float showDelay = 0.5f;          ///< Seconds before tooltip appears.
	float hideDelay = 0.1f;          ///< Seconds after pointer leaves before hiding.
	float fadeInDuration = 0.15f;    ///< Fade-in animation duration in seconds.
	float fadeOutDuration = 0.1f;    ///< Fade-out animation duration in seconds.

	// ── Behavior ──────────────────────────────────────────────
	bool followMouse = false;        ///< If true, tooltip tracks the cursor position.
	float screenMargin = 4.0f;       ///< Minimum margin from screen edges.

	// ── Screen bounds (for auto-positioning) ──────────────────
	float screenWidth = 1920.0f;     ///< Screen width for boundary clamping.
	float screenHeight = 1080.0f;    ///< Screen height for boundary clamping.

	// ── Image keys ────────────────────────────────────────────
	std::string backgroundImageKey;  ///< Image key for tooltip background.
	std::string borderImageKey;      ///< Image key for tooltip border/frame.
	std::string arrowImageKey;       ///< Image key for the directional arrow.
};

/// @brief Tooltip widget that displays contextual information near the cursor or an anchor element.
///
/// Manages show/hide delays, fade animation, auto-positioning within screen bounds,
/// and optional attachment to a UINode. Rendering is handled externally by UIRenderer.
///
/// @code
///   UITooltipConfig cfg;
///   cfg.id = 100;
///   cfg.text = "Click to confirm purchase";
///   cfg.showDelay = 0.3f;
///   cfg.position = TooltipPosition::Below;
///   UITooltip tooltip(cfg);
///
///   tooltip.show("Helpful info", mouseX, mouseY);
///   tooltip.update(deltaTime);
///   if (tooltip.isVisible()) { /* render at tooltip.currentBounds() */ }
/// @endcode
class UITooltip
{
	/// @brief Internal phase of tooltip lifecycle.
	enum class Phase : std::uint8_t
	{
		Hidden,
		WaitingToShow,
		FadingIn,
		Visible,
		WaitingToHide,
		FadingOut
	};

	std::shared_ptr<UINode> m_node;

	// ── Config copies ─────────────────────────────────────────
	float m_maxWidth;
	float m_padding;
	float m_arrowSize;
	float m_anchorOffsetX;
	float m_anchorOffsetY;
	TooltipPosition m_preferredPosition;
	float m_showDelay;
	float m_hideDelay;
	float m_fadeInDuration;
	float m_fadeOutDuration;
	bool m_followMouse;
	float m_screenMargin;
	float m_screenWidth;
	float m_screenHeight;
	std::string m_backgroundImageKey;
	std::string m_borderImageKey;
	std::string m_arrowImageKey;

	// ── Runtime state ─────────────────────────────────────────
	Phase m_phase = Phase::Hidden;
	float m_timer = 0.0f;
	float m_opacity = 0.0f;
	float m_anchorX = 0.0f;
	float m_anchorY = 0.0f;
	float m_contentWidth = 0.0f;
	float m_contentHeight = 0.0f;
	TooltipPosition m_resolvedPosition = TooltipPosition::Below;
	std::weak_ptr<UINode> m_attachedNode;

public:
	/// @brief Construct a tooltip from configuration.
	/// @param config Tooltip configuration.
	explicit UITooltip(const UITooltipConfig& config)
		: m_maxWidth(config.maxWidth)
		, m_padding(config.padding)
		, m_arrowSize(config.arrowSize)
		, m_anchorOffsetX(config.anchorOffsetX)
		, m_anchorOffsetY(config.anchorOffsetY)
		, m_preferredPosition(config.position)
		, m_showDelay(config.showDelay)
		, m_hideDelay(config.hideDelay)
		, m_fadeInDuration(config.fadeInDuration)
		, m_fadeOutDuration(config.fadeOutDuration)
		, m_followMouse(config.followMouse)
		, m_screenMargin(config.screenMargin)
		, m_screenWidth(config.screenWidth)
		, m_screenHeight(config.screenHeight)
		, m_backgroundImageKey(config.backgroundImageKey)
		, m_borderImageKey(config.borderImageKey)
		, m_arrowImageKey(config.arrowImageKey)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Tooltip;
		data.text = config.text;
		data.properties["widget_type"] = "tooltip";
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["border_image"] = config.borderImageKey;
		data.properties["arrow_image"] = config.arrowImageKey;

		m_node = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	// ── Accessors ─────────────────────────────────────────────

	/// @brief Get the underlying UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief Check if the tooltip is currently visible (including during fade).
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_phase == Phase::FadingIn
			|| m_phase == Phase::Visible
			|| m_phase == Phase::WaitingToHide
			|| m_phase == Phase::FadingOut;
	}

	/// @brief Get the current opacity (0.0 = fully transparent, 1.0 = fully opaque).
	[[nodiscard]] float opacity() const noexcept { return m_opacity; }

	/// @brief Get the current tooltip bounds in screen space.
	[[nodiscard]] sgc::Rectf currentBounds() const noexcept { return m_node->bounds(); }

	/// @brief Get the resolved anchor position (after auto-positioning).
	[[nodiscard]] TooltipPosition resolvedPosition() const noexcept { return m_resolvedPosition; }

	/// @brief Get the tooltip text.
	[[nodiscard]] const std::string& text() const noexcept { return m_node->text(); }

	/// @brief Get the background image key.
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief Get the border image key.
	[[nodiscard]] const std::string& borderImageKey() const noexcept { return m_borderImageKey; }

	/// @brief Get the arrow image key.
	[[nodiscard]] const std::string& arrowImageKey() const noexcept { return m_arrowImageKey; }

	// ── Actions ───────────────────────────────────────────────

	/// @brief Request to show the tooltip at the given anchor position.
	/// @param text Text to display (supports rich text pass-through).
	/// @param x Anchor X position in screen space.
	/// @param y Anchor Y position in screen space.
	void show(const std::string& text, float x, float y)
	{
		m_node->setText(text);
		m_anchorX = x;
		m_anchorY = y;

		if (m_phase == Phase::Hidden || m_phase == Phase::FadingOut || m_phase == Phase::WaitingToHide)
		{
			if (m_showDelay > 0.0f && m_phase == Phase::Hidden)
			{
				m_phase = Phase::WaitingToShow;
				m_timer = 0.0f;
			}
			else
			{
				beginFadeIn();
			}
		}
	}

	/// @brief Request to show the tooltip using the previously set text.
	/// @param x Anchor X position.
	/// @param y Anchor Y position.
	void show(float x, float y)
	{
		show(m_node->text(), x, y);
	}

	/// @brief Request to hide the tooltip (begins hide delay / fade-out).
	void hide()
	{
		if (m_phase == Phase::Hidden || m_phase == Phase::FadingOut)
		{
			return;
		}

		if (m_phase == Phase::WaitingToShow)
		{
			m_phase = Phase::Hidden;
			m_timer = 0.0f;
			m_opacity = 0.0f;
			syncNodeState();
			return;
		}

		if (m_hideDelay > 0.0f)
		{
			m_phase = Phase::WaitingToHide;
			m_timer = 0.0f;
		}
		else
		{
			beginFadeOut();
		}
	}

	/// @brief Immediately hide without any delay or animation.
	void hideImmediate()
	{
		m_phase = Phase::Hidden;
		m_timer = 0.0f;
		m_opacity = 0.0f;
		syncNodeState();
	}

	/// @brief Attach the tooltip to a UINode (auto-show on hover).
	/// @param target Node to attach to.
	void attachTo(std::shared_ptr<UINode> target)
	{
		m_attachedNode = target;
	}

	/// @brief Detach from the currently attached UINode.
	void detach()
	{
		m_attachedNode.reset();
	}

	/// @brief Update the cursor position when followMouse is enabled.
	/// @param x Current cursor X.
	/// @param y Current cursor Y.
	void updateCursorPosition(float x, float y) noexcept
	{
		if (m_followMouse && isVisible())
		{
			m_anchorX = x;
			m_anchorY = y;
			resolvePositionAndClamp();
		}
	}

	/// @brief Set estimated content dimensions (called by layout/renderer).
	/// @param width Content width.
	/// @param height Content height.
	void setContentSize(float width, float height) noexcept
	{
		m_contentWidth = std::min(width, m_maxWidth);
		m_contentHeight = height;
		resolvePositionAndClamp();
	}

	/// @brief Set screen bounds for auto-positioning.
	/// @param width Screen width.
	/// @param height Screen height.
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
	}

	// ── Update ────────────────────────────────────────────────

	/// @brief Advance tooltip animation and timing.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		switch (m_phase)
		{
		case Phase::WaitingToShow:
			m_timer += dt;
			if (m_timer >= m_showDelay)
			{
				beginFadeIn();
			}
			break;

		case Phase::FadingIn:
			m_timer += dt;
			if (m_fadeInDuration > 0.0f)
			{
				m_opacity = std::clamp(m_timer / m_fadeInDuration, 0.0f, 1.0f);
			}
			else
			{
				m_opacity = 1.0f;
			}
			if (m_opacity >= 1.0f)
			{
				m_phase = Phase::Visible;
				m_opacity = 1.0f;
			}
			syncNodeState();
			break;

		case Phase::Visible:
			// Steady state; nothing to update.
			break;

		case Phase::WaitingToHide:
			m_timer += dt;
			if (m_timer >= m_hideDelay)
			{
				beginFadeOut();
			}
			break;

		case Phase::FadingOut:
			m_timer += dt;
			if (m_fadeOutDuration > 0.0f)
			{
				m_opacity = std::clamp(1.0f - m_timer / m_fadeOutDuration, 0.0f, 1.0f);
			}
			else
			{
				m_opacity = 0.0f;
			}
			if (m_opacity <= 0.0f)
			{
				m_phase = Phase::Hidden;
				m_opacity = 0.0f;
			}
			syncNodeState();
			break;

		case Phase::Hidden:
			break;
		}
	}

private:
	void beginFadeIn()
	{
		m_phase = Phase::FadingIn;
		m_timer = 0.0f;
		resolvePositionAndClamp();
		syncNodeState();
	}

	void beginFadeOut()
	{
		m_phase = Phase::FadingOut;
		m_timer = 0.0f;
		syncNodeState();
	}

	/// @brief Resolve tooltip position and clamp within screen bounds.
	void resolvePositionAndClamp()
	{
		const float totalW = m_contentWidth + m_padding * 2.0f;
		const float totalH = m_contentHeight + m_padding * 2.0f;
		const float margin = m_screenMargin;

		m_resolvedPosition = m_preferredPosition;
		if (m_resolvedPosition == TooltipPosition::Auto)
		{
			m_resolvedPosition = chooseAutoPosition(totalW, totalH);
		}

		float x = 0.0f;
		float y = 0.0f;

		switch (m_resolvedPosition)
		{
		case TooltipPosition::Above:
			x = m_anchorX + m_anchorOffsetX - totalW * 0.5f;
			y = m_anchorY + m_anchorOffsetY - totalH - m_arrowSize;
			break;
		case TooltipPosition::Below:
			x = m_anchorX + m_anchorOffsetX - totalW * 0.5f;
			y = m_anchorY + m_anchorOffsetY + m_arrowSize;
			break;
		case TooltipPosition::Left:
			x = m_anchorX + m_anchorOffsetX - totalW - m_arrowSize;
			y = m_anchorY + m_anchorOffsetY - totalH * 0.5f;
			break;
		case TooltipPosition::Right:
			x = m_anchorX + m_anchorOffsetX + m_arrowSize;
			y = m_anchorY + m_anchorOffsetY - totalH * 0.5f;
			break;
		case TooltipPosition::Auto:
			break; // Already resolved above.
		}

		// Clamp within screen bounds.
		x = std::clamp(x, margin, m_screenWidth - totalW - margin);
		y = std::clamp(y, margin, m_screenHeight - totalH - margin);

		m_node->setBounds(sgc::Rectf(x, y, totalW, totalH));
	}

	/// @brief Choose the best position when Auto is selected.
	[[nodiscard]] TooltipPosition chooseAutoPosition(float totalW, float totalH) const noexcept
	{
		const float margin = m_screenMargin;
		const float spaceAbove = m_anchorY - margin;
		const float spaceBelow = m_screenHeight - m_anchorY - margin;
		const float spaceLeft = m_anchorX - margin;
		const float spaceRight = m_screenWidth - m_anchorX - margin;

		// Prefer below, then above, then right, then left.
		if (spaceBelow >= totalH + m_arrowSize) { return TooltipPosition::Below; }
		if (spaceAbove >= totalH + m_arrowSize) { return TooltipPosition::Above; }
		if (spaceRight >= totalW + m_arrowSize) { return TooltipPosition::Right; }
		if (spaceLeft >= totalW + m_arrowSize)  { return TooltipPosition::Left; }

		return TooltipPosition::Below; // Fallback.
	}

	void syncNodeState()
	{
		const char* phaseStr = "hidden";
		switch (m_phase)
		{
		case Phase::WaitingToShow: phaseStr = "waiting_show"; break;
		case Phase::FadingIn:      phaseStr = "fading_in";    break;
		case Phase::Visible:       phaseStr = "visible";      break;
		case Phase::WaitingToHide: phaseStr = "waiting_hide"; break;
		case Phase::FadingOut:     phaseStr = "fading_out";   break;
		default: break;
		}

		const char* posStr = "below";
		switch (m_resolvedPosition)
		{
		case TooltipPosition::Above: posStr = "above"; break;
		case TooltipPosition::Below: posStr = "below"; break;
		case TooltipPosition::Left:  posStr = "left";  break;
		case TooltipPosition::Right: posStr = "right"; break;
		case TooltipPosition::Auto:  posStr = "auto";  break;
		}

		m_node->setProperty("state", phaseStr);
		m_node->setProperty("opacity", std::to_string(m_opacity));
		m_node->setProperty("position", posStr);
		m_node->setProperty("follow_mouse", m_followMouse ? "true" : "false");
		m_node->setProperty("background_image", m_backgroundImageKey);
		m_node->setProperty("border_image", m_borderImageKey);
		m_node->setProperty("arrow_image", m_arrowImageKey);
	}
};

} // namespace mitiru::ui
