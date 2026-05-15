#pragma once

/// @file ScrollView.hpp
/// @brief General-purpose scrollable container for UINode trees.
/// @details Provides vertical and horizontal scrolling with inertia, bounce,
///          virtual scrolling for large lists, and integrated UIEvent handling.
///          Built on the same physics model as vn::ScrollContainer but designed
///          to work with the UINode tree rather than raw SpriteBatch rendering.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief Scroll bar visual style.
struct ScrollBarStyle
{
	float width           = 6.0f;    ///< Bar width in pixels.
	float minThumbLength  = 20.0f;   ///< Minimum thumb size.
	float cornerRadius    = 3.0f;    ///< Rounded corner radius.
	bool autoHide         = true;    ///< Hide when idle.
	float fadeDelaySec    = 1.5f;    ///< Seconds before fade begins.
	float opacity         = 0.5f;    ///< Maximum thumb opacity.
};

/// @brief Configuration for ScrollView behaviour.
struct ScrollViewConfig
{
	sgc::Rectf viewport{};                ///< Visible area in screen space.
	float contentHeight   = 0.0f;         ///< Total content height.
	float contentWidth    = 0.0f;         ///< Total content width.

	bool verticalScroll   = true;         ///< Allow vertical scrolling.
	bool horizontalScroll = false;        ///< Allow horizontal scrolling.
	bool bounceEffect     = true;         ///< Overscroll rubber-band bounce.
	bool snapToItem       = false;        ///< Snap to item boundaries.
	float snapItemHeight  = 0.0f;         ///< Item height for snap (0 = disabled).
	float inertiaDamping  = 0.92f;        ///< Velocity damping per frame (0-1).
	float bounceDamping   = 0.6f;         ///< Bounce return speed factor.
	float wheelMultiplier = 40.0f;        ///< Pixels per mouse wheel notch.
	float keyboardScrollAmount = 40.0f;   ///< Pixels per keyboard scroll step.

	ScrollBarStyle scrollBar;             ///< Scroll bar appearance.

	/// @brief Enable virtual scrolling for large lists.
	bool virtualScrolling = false;
	/// @brief Item height for virtual scrolling (all items same height).
	float virtualItemHeight = 0.0f;
};

/// @brief Information about a visible range in virtual scrolling mode.
struct VirtualScrollRange
{
	std::size_t firstVisible = 0;   ///< Index of first visible item.
	std::size_t lastVisible  = 0;   ///< Index of last visible item (inclusive).
	float offsetY            = 0.0f; ///< Y offset for the first visible item.
};

/// @brief General-purpose scrollable container for UINode trees.
///
/// @code
/// mitiru::ui::ScrollViewConfig cfg;
/// cfg.viewport = {0, 0, 400, 600};
/// cfg.contentHeight = 2000.0f;
///
/// mitiru::ui::ScrollView scroll(cfg);
/// scroll.onMouseWheel(-3.0f);
/// scroll.update(dt);
///
/// // Apply scroll offset to child nodes.
/// float offsetY = scroll.scrollY();
/// @endcode
class ScrollView
{
public:
	/// @brief Construct with configuration.
	/// @param config Scroll behaviour configuration.
	explicit ScrollView(ScrollViewConfig config = {}) noexcept
		: m_config(config)
	{
	}

	// ── Accessors ────────────────────────────────────────────

	/// @brief Current viewport rectangle.
	[[nodiscard]] const sgc::Rectf& viewport() const noexcept { return m_config.viewport; }

	/// @brief Set the viewport rectangle.
	void setViewport(const sgc::Rectf& vp) noexcept { m_config.viewport = vp; }

	/// @brief Current vertical scroll offset (pixels from top).
	[[nodiscard]] float scrollY() const noexcept { return m_scrollY; }

	/// @brief Current horizontal scroll offset.
	[[nodiscard]] float scrollX() const noexcept { return m_scrollX; }

	/// @brief Total content height.
	[[nodiscard]] float contentHeight() const noexcept { return m_config.contentHeight; }

	/// @brief Total content width.
	[[nodiscard]] float contentWidth() const noexcept { return m_config.contentWidth; }

	/// @brief Set total content height (call when content changes).
	void setContentHeight(float h) noexcept { m_config.contentHeight = h; }

	/// @brief Set total content width.
	void setContentWidth(float w) noexcept { m_config.contentWidth = w; }

	/// @brief Access the configuration.
	[[nodiscard]] const ScrollViewConfig& config() const noexcept { return m_config; }

	/// @brief Replace configuration.
	void setConfig(const ScrollViewConfig& cfg) noexcept { m_config = cfg; }

	/// @brief Maximum valid vertical scroll offset.
	[[nodiscard]] float maxScrollY() const noexcept
	{
		return std::max(0.0f, m_config.contentHeight - m_config.viewport.height());
	}

	/// @brief Maximum valid horizontal scroll offset.
	[[nodiscard]] float maxScrollX() const noexcept
	{
		return std::max(0.0f, m_config.contentWidth - m_config.viewport.width());
	}

	/// @brief Whether content is taller than viewport.
	[[nodiscard]] bool canScrollVertically() const noexcept
	{
		return m_config.verticalScroll
			&& m_config.contentHeight > m_config.viewport.height();
	}

	/// @brief Whether content is wider than viewport.
	[[nodiscard]] bool canScrollHorizontally() const noexcept
	{
		return m_config.horizontalScroll
			&& m_config.contentWidth > m_config.viewport.width();
	}

	/// @brief Normalized vertical scroll position [0, 1].
	[[nodiscard]] float normalizedScrollY() const noexcept
	{
		const float maxY = maxScrollY();
		return (maxY > 0.0f) ? (m_scrollY / maxY) : 0.0f;
	}

	/// @brief Normalized horizontal scroll position [0, 1].
	[[nodiscard]] float normalizedScrollX() const noexcept
	{
		const float maxX = maxScrollX();
		return (maxX > 0.0f) ? (m_scrollX / maxX) : 0.0f;
	}

	/// @brief Whether a point in screen space is inside the viewport.
	[[nodiscard]] bool containsPoint(float x, float y) const noexcept
	{
		return x >= m_config.viewport.x()
			&& x < m_config.viewport.x() + m_config.viewport.width()
			&& y >= m_config.viewport.y()
			&& y < m_config.viewport.y() + m_config.viewport.height();
	}

	/// @brief Whether the scroll view is currently being dragged.
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief Whether the scroll view has any momentum.
	[[nodiscard]] bool isScrolling() const noexcept
	{
		return m_dragging
			|| std::abs(m_velocityX) > 0.5f
			|| std::abs(m_velocityY) > 0.5f;
	}

	// ── Programmatic scroll ──────────────────────────────────

	/// @brief Set vertical scroll position directly.
	void setScrollY(float y) noexcept
	{
		m_scrollY = clampScroll(y, maxScrollY());
		m_velocityY = 0.0f;
	}

	/// @brief Set horizontal scroll position directly.
	void setScrollX(float x) noexcept
	{
		m_scrollX = clampScroll(x, maxScrollX());
		m_velocityX = 0.0f;
	}

	/// @brief Scroll to the top.
	void scrollToTop() noexcept
	{
		m_scrollY = 0.0f;
		m_velocityY = 0.0f;
	}

	/// @brief Scroll to the bottom.
	void scrollToBottom() noexcept
	{
		m_scrollY = maxScrollY();
		m_velocityY = 0.0f;
	}

	/// @brief Ensure a vertical region is visible, scrolling minimally.
	/// @param itemTop Top of the item in content space.
	/// @param itemHeight Height of the item.
	void ensureVisible(float itemTop, float itemHeight) noexcept
	{
		const float itemBottom = itemTop + itemHeight;
		const float visibleTop = m_scrollY;
		const float visibleBottom = m_scrollY + m_config.viewport.height();

		if (itemTop < visibleTop)
		{
			setScrollY(itemTop);
		}
		else if (itemBottom > visibleBottom)
		{
			setScrollY(itemBottom - m_config.viewport.height());
		}
	}

	// ── Input handling ───────────────────────────────────────

	/// @brief Handle mouse wheel input.
	/// @param delta Wheel delta (negative = scroll down).
	void onMouseWheel(float delta)
	{
		if (m_config.verticalScroll)
		{
			m_velocityY -= delta * m_config.wheelMultiplier;
		}
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief Handle horizontal mouse wheel.
	/// @param delta Wheel delta (negative = scroll right).
	void onMouseWheelH(float delta)
	{
		if (m_config.horizontalScroll)
		{
			m_velocityX -= delta * m_config.wheelMultiplier;
		}
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief Handle keyboard scroll (e.g. arrow keys, Page Up/Down).
	/// @param deltaX Horizontal direction (-1, 0, +1).
	/// @param deltaY Vertical direction (-1, 0, +1).
	/// @param pageScroll Whether to scroll by a page instead of a step.
	void onKeyboardScroll(float deltaX, float deltaY, bool pageScroll = false)
	{
		const float amount = pageScroll
			? m_config.viewport.height() * 0.9f
			: m_config.keyboardScrollAmount;

		if (m_config.verticalScroll && deltaY != 0.0f)
		{
			m_velocityY += deltaY * amount;
		}
		if (m_config.horizontalScroll && deltaX != 0.0f)
		{
			m_velocityX += deltaX * amount;
		}
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief Begin a drag/touch scroll.
	/// @param screenX Screen X position.
	/// @param screenY Screen Y position.
	void onDragBegin(float screenX, float screenY)
	{
		m_dragging = true;
		m_dragStartX = screenX;
		m_dragStartY = screenY;
		m_dragScrollX = m_scrollX;
		m_dragScrollY = m_scrollY;
		m_velocityX = 0.0f;
		m_velocityY = 0.0f;
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief Update drag position.
	/// @param screenX Current screen X.
	/// @param screenY Current screen Y.
	void onDragMove(float screenX, float screenY)
	{
		if (!m_dragging) return;

		if (m_config.verticalScroll)
		{
			const float dy = screenY - m_dragStartY;
			float target = m_dragScrollY - dy;
			if (m_config.bounceEffect)
			{
				target = applyOverscroll(target, maxScrollY());
			}
			m_scrollY = target;
		}

		if (m_config.horizontalScroll)
		{
			const float dx = screenX - m_dragStartX;
			float target = m_dragScrollX - dx;
			if (m_config.bounceEffect)
			{
				target = applyOverscroll(target, maxScrollX());
			}
			m_scrollX = target;
		}

		m_timeSinceInteraction = 0.0f;
	}

	/// @brief End a drag/touch scroll with release velocity.
	/// @param velocityX Release velocity X (pixels/sec).
	/// @param velocityY Release velocity Y (pixels/sec).
	void onDragEnd(float velocityX = 0.0f, float velocityY = 0.0f)
	{
		m_dragging = false;
		m_velocityX = velocityX;
		m_velocityY = velocityY;
		m_timeSinceInteraction = 0.0f;
	}

	// ── Virtual scrolling ────────────────────────────────────

	/// @brief Compute the visible range for virtual scrolling.
	/// @param totalItems Total number of items in the list.
	/// @return Range of visible item indices and the Y offset.
	[[nodiscard]] VirtualScrollRange computeVisibleRange(
		std::size_t totalItems) const noexcept
	{
		if (!m_config.virtualScrolling
			|| m_config.virtualItemHeight <= 0.0f
			|| totalItems == 0)
		{
			return {0, totalItems > 0 ? totalItems - 1 : 0, 0.0f};
		}

		const float itemH = m_config.virtualItemHeight;
		const auto first = static_cast<std::size_t>(
			std::max(0.0f, m_scrollY / itemH));
		const auto visibleCount = static_cast<std::size_t>(
			std::ceil(m_config.viewport.height() / itemH)) + 1;
		const auto last = std::min(totalItems - 1, first + visibleCount);
		const float offsetY = -(m_scrollY - static_cast<float>(first) * itemH);

		return {first, last, offsetY};
	}

	// ── UINode tree integration ──────────────────────────────

	/// @brief Apply scroll offset to a UINode's child positions.
	/// @details Adjusts the Y position of each child by -scrollY and the X
	///          position by -scrollX. Children fully outside the viewport
	///          are marked invisible.
	/// @param parent The container UINode whose children should be scrolled.
	void applyToChildren(UINode& parent) const
	{
		for (std::size_t i = 0; i < parent.childCount(); ++i)
		{
			auto& child = parent.child(i);
			auto bounds = child.bounds();

			// Offset by scroll position.
			bounds = sgc::Rectf{
				bounds.x() - m_scrollX,
				bounds.y() - m_scrollY,
				bounds.width(),
				bounds.height()
			};
			child.setBounds(bounds);

			// Cull children outside viewport.
			const bool inView =
				(bounds.y() + bounds.height() > m_config.viewport.y())
				&& (bounds.y() < m_config.viewport.y() + m_config.viewport.height())
				&& (bounds.x() + bounds.width() > m_config.viewport.x())
				&& (bounds.x() < m_config.viewport.x() + m_config.viewport.width());
			child.setVisible(inView);
		}
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief Update scroll physics.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		m_timeSinceInteraction += dt;

		if (m_dragging) return;

		// Apply velocity with inertia.
		if (m_config.verticalScroll)
		{
			m_scrollY += m_velocityY * dt;
			m_velocityY *= m_config.inertiaDamping;
			if (std::abs(m_velocityY) < 0.5f)
			{
				m_velocityY = 0.0f;
			}
		}

		if (m_config.horizontalScroll)
		{
			m_scrollX += m_velocityX * dt;
			m_velocityX *= m_config.inertiaDamping;
			if (std::abs(m_velocityX) < 0.5f)
			{
				m_velocityX = 0.0f;
			}
		}

		// Bounce back from overscroll.
		if (m_config.bounceEffect)
		{
			m_scrollY = bounceBack(m_scrollY, maxScrollY(), m_velocityY, dt);
			m_scrollX = bounceBack(m_scrollX, maxScrollX(), m_velocityX, dt);
		}
		else
		{
			m_scrollY = clampScroll(m_scrollY, maxScrollY());
			m_scrollX = clampScroll(m_scrollX, maxScrollX());
		}

		// Snap to item boundary after momentum settles.
		if (m_config.snapToItem && m_config.snapItemHeight > 0.0f
			&& m_velocityY == 0.0f && !m_dragging)
		{
			const float itemH = m_config.snapItemHeight;
			const float snapped = std::round(m_scrollY / itemH) * itemH;
			m_scrollY += (snapped - m_scrollY) * std::min(1.0f, dt * 10.0f);
		}
	}

	// ── Scroll bar state ─────────────────────────────────────

	/// @brief Whether the vertical scroll bar should be visible.
	[[nodiscard]] bool isScrollBarVisible() const noexcept
	{
		if (!canScrollVertically()) return false;
		if (!m_config.scrollBar.autoHide) return true;
		return m_timeSinceInteraction < m_config.scrollBar.fadeDelaySec;
	}

	/// @brief Current scroll bar opacity (accounts for auto-hide fade).
	[[nodiscard]] float scrollBarOpacity() const noexcept
	{
		if (!canScrollVertically()) return 0.0f;

		const auto& style = m_config.scrollBar;
		if (!style.autoHide) return style.opacity;

		const float fadeStart = style.fadeDelaySec * 0.7f;
		if (m_timeSinceInteraction <= fadeStart) return style.opacity;
		if (m_timeSinceInteraction >= style.fadeDelaySec) return 0.0f;

		const float t = (m_timeSinceInteraction - fadeStart)
			/ (style.fadeDelaySec - fadeStart);
		return style.opacity * (1.0f - t);
	}

	/// @brief Compute the scroll bar thumb rectangle (vertical).
	/// @return Thumb rectangle in screen space.
	[[nodiscard]] sgc::Rectf scrollBarThumbRect() const noexcept
	{
		if (!canScrollVertically()) return {};

		const auto& vp = m_config.viewport;
		const auto& style = m_config.scrollBar;

		const float trackX = vp.x() + vp.width() - style.width;
		const float viewRatio = vp.height() / m_config.contentHeight;
		const float thumbLen = std::max(
			style.minThumbLength, vp.height() * viewRatio);
		const float scrollRange = vp.height() - thumbLen;
		const float thumbY = vp.y() + normalizedScrollY() * scrollRange;

		return sgc::Rectf{trackX, thumbY, style.width, thumbLen};
	}

private:
	/// @brief Clamp scroll offset to valid range.
	[[nodiscard]] static float clampScroll(float value, float maxVal) noexcept
	{
		if (value < 0.0f) return 0.0f;
		if (value > maxVal) return maxVal;
		return value;
	}

	/// @brief Apply rubber-band overscroll resistance.
	[[nodiscard]] static float applyOverscroll(float value, float maxVal) noexcept
	{
		if (value < 0.0f)
		{
			return value * 0.3f;
		}
		if (value > maxVal)
		{
			return maxVal + (value - maxVal) * 0.3f;
		}
		return value;
	}

	/// @brief Spring back from overscroll towards valid range.
	[[nodiscard]] static float bounceBack(
		float scroll, float maxVal, float& velocity, float dt) noexcept
	{
		if (scroll < 0.0f)
		{
			velocity = 0.0f;
			scroll += (-scroll) * std::min(1.0f, dt * 8.0f);
			if (scroll > -0.5f) scroll = 0.0f;
		}
		else if (scroll > maxVal)
		{
			velocity = 0.0f;
			scroll -= (scroll - maxVal) * std::min(1.0f, dt * 8.0f);
			if (scroll < maxVal + 0.5f) scroll = maxVal;
		}
		return scroll;
	}

	ScrollViewConfig m_config;

	float m_scrollX    = 0.0f;
	float m_scrollY    = 0.0f;
	float m_velocityX  = 0.0f;
	float m_velocityY  = 0.0f;
	float m_timeSinceInteraction = 0.0f;

	bool  m_dragging     = false;
	float m_dragStartX   = 0.0f;
	float m_dragStartY   = 0.0f;
	float m_dragScrollX  = 0.0f;
	float m_dragScrollY  = 0.0f;
};

} // namespace mitiru::ui
