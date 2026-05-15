#pragma once

/// @file ScrollContainer.hpp
/// @brief Generic scrollable container with viewport clipping and inertia.
/// @details Provides vertical and horizontal scrolling with smooth momentum,
///          optional scroll bars, snap-to-item, and overscroll bounce.
///          Used by BacklogUI and other scrollable screens.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/SpriteBatch.hpp>

namespace mitiru::vn
{

/// @brief Scroll bar appearance configuration.
struct ScrollBarStyle
{
	sgc::Colorf trackColor{0.2f, 0.2f, 0.2f, 0.3f};   ///< Track background.
	sgc::Colorf thumbColor{0.6f, 0.6f, 0.6f, 0.5f};   ///< Thumb colour.
	float width       = 6.0f;                            ///< Bar width in pixels.
	float minThumbLen = 20.0f;                           ///< Minimum thumb length.
	bool autoHide     = true;                            ///< Hide when not scrolling.
	float fadeDelaySec = 1.5f;                           ///< Seconds before fade out.
};

/// @brief Configuration for ScrollContainer behaviour.
struct ScrollConfig
{
	bool verticalScroll    = true;     ///< Allow vertical scrolling.
	bool horizontalScroll  = false;    ///< Allow horizontal scrolling.
	bool showScrollBar     = true;     ///< Show vertical scroll bar.
	bool showHScrollBar    = false;    ///< Show horizontal scroll bar.
	bool bounceEffect      = true;     ///< Overscroll rubber-band bounce.
	bool snapToItem        = false;    ///< Snap to item boundaries after scroll.
	float snapItemHeight   = 0.0f;    ///< Item height for snap (0 = disabled).
	float inertiaDamping   = 0.92f;   ///< Velocity damping per frame (0-1).
	float bounceDamping    = 0.6f;    ///< Bounce return speed factor.
	float wheelMultiplier  = 40.0f;   ///< Pixels per mouse wheel notch.

	ScrollBarStyle scrollBar;          ///< Scroll bar appearance.
};

/// @brief Generic scrollable container with viewport clipping and momentum.
///
/// @code
/// mitiru::vn::ScrollContainer scroll(viewport, contentHeight);
/// scroll.onMouseWheel(-3.0f);
/// scroll.update(dt);
///
/// batch.begin();
/// scroll.drawScrollBar(batch);
/// batch.end();
///
/// // Render children offset by -scroll.scrollY().
/// @endcode
class ScrollContainer
{
	sgc::Rectf m_viewport{};           ///< Visible region in screen space.
	float m_contentW   = 0.0f;         ///< Total content width.
	float m_contentH   = 0.0f;         ///< Total content height.
	float m_scrollX    = 0.0f;         ///< Current horizontal scroll offset.
	float m_scrollY    = 0.0f;         ///< Current vertical scroll offset.
	float m_velocityX  = 0.0f;         ///< Horizontal scroll velocity.
	float m_velocityY  = 0.0f;         ///< Vertical scroll velocity.
	float m_timeSinceInteraction = 0.0f; ///< Timer for auto-hiding scroll bar.
	bool  m_dragging   = false;        ///< Touch/mouse drag active.
	float m_dragStartY = 0.0f;         ///< Drag start position (screen Y).
	float m_dragStartX = 0.0f;         ///< Drag start position (screen X).
	float m_dragScrollY = 0.0f;        ///< Scroll offset at drag start.
	float m_dragScrollX = 0.0f;        ///< Scroll offset at drag start.

	ScrollConfig m_config;

public:
	/// @brief Construct with viewport and content dimensions.
	/// @param viewport Visible area in screen space.
	/// @param contentW Total content width (0 for vertical-only).
	/// @param contentH Total content height.
	/// @param config Scroll behaviour configuration.
	explicit ScrollContainer(
		const sgc::Rectf& viewport = {},
		float contentW = 0.0f,
		float contentH = 0.0f,
		ScrollConfig config = {}) noexcept
		: m_viewport(viewport)
		, m_contentW(contentW)
		, m_contentH(contentH)
		, m_config(config)
	{
	}

	// ── Accessors ────────────────────────────────────────────

	/// @brief Current viewport rectangle.
	[[nodiscard]] const sgc::Rectf& viewport() const noexcept { return m_viewport; }

	/// @brief Set the viewport rectangle.
	void setViewport(const sgc::Rectf& vp) noexcept { m_viewport = vp; }

	/// @brief Current vertical scroll offset (pixels from top).
	[[nodiscard]] float scrollY() const noexcept { return m_scrollY; }

	/// @brief Current horizontal scroll offset.
	[[nodiscard]] float scrollX() const noexcept { return m_scrollX; }

	/// @brief Total content height.
	[[nodiscard]] float contentHeight() const noexcept { return m_contentH; }

	/// @brief Total content width.
	[[nodiscard]] float contentWidth() const noexcept { return m_contentW; }

	/// @brief Set total content height (call when content changes).
	void setContentHeight(float h) noexcept { m_contentH = h; }

	/// @brief Set total content width.
	void setContentWidth(float w) noexcept { m_contentW = w; }

	/// @brief Access the configuration.
	[[nodiscard]] const ScrollConfig& config() const noexcept { return m_config; }

	/// @brief Replace configuration.
	void setConfig(const ScrollConfig& cfg) noexcept { m_config = cfg; }

	/// @brief Maximum valid vertical scroll offset.
	[[nodiscard]] float maxScrollY() const noexcept
	{
		return std::max(0.0f, m_contentH - m_viewport.height());
	}

	/// @brief Maximum valid horizontal scroll offset.
	[[nodiscard]] float maxScrollX() const noexcept
	{
		return std::max(0.0f, m_contentW - m_viewport.width());
	}

	/// @brief Whether content is taller than viewport.
	[[nodiscard]] bool canScrollVertically() const noexcept
	{
		return m_config.verticalScroll && m_contentH > m_viewport.height();
	}

	/// @brief Whether content is wider than viewport.
	[[nodiscard]] bool canScrollHorizontally() const noexcept
	{
		return m_config.horizontalScroll && m_contentW > m_viewport.width();
	}

	/// @brief Normalized vertical scroll position [0, 1].
	[[nodiscard]] float normalizedScrollY() const noexcept
	{
		const float maxY = maxScrollY();
		return (maxY > 0.0f) ? (m_scrollY / maxY) : 0.0f;
	}

	/// @brief Whether a point in screen space is inside the viewport.
	[[nodiscard]] bool containsPoint(float x, float y) const noexcept
	{
		return x >= m_viewport.x() && x < m_viewport.x() + m_viewport.width()
		    && y >= m_viewport.y() && y < m_viewport.y() + m_viewport.height();
	}

	// ── Programmatic scroll ──────────────────────────────────

	/// @brief Set vertical scroll position directly.
	/// @param y Scroll offset in pixels.
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
		const float visibleBottom = m_scrollY + m_viewport.height();

		if (itemTop < visibleTop)
		{
			setScrollY(itemTop);
		}
		else if (itemBottom > visibleBottom)
		{
			setScrollY(itemBottom - m_viewport.height());
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

	/// @brief End a drag/touch scroll with a release velocity.
	/// @param velocityX Release velocity X (pixels/sec).
	/// @param velocityY Release velocity Y (pixels/sec).
	void onDragEnd(float velocityX = 0.0f, float velocityY = 0.0f)
	{
		m_dragging = false;
		m_velocityX = velocityX;
		m_velocityY = velocityY;
		m_timeSinceInteraction = 0.0f;
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

	// ── Rendering ────────────────────────────────────────────

	/// @brief Draw the vertical scroll bar.
	/// @param batch SpriteBatch to draw into (must be between begin/end).
	void drawScrollBar(render::SpriteBatch& batch) const
	{
		if (!m_config.showScrollBar || !canScrollVertically())
		{
			return;
		}

		const auto& style = m_config.scrollBar;

		// Auto-hide logic.
		if (style.autoHide && m_timeSinceInteraction > style.fadeDelaySec)
		{
			return;
		}

		float alpha = 1.0f;
		if (style.autoHide)
		{
			const float fadeStart = style.fadeDelaySec * 0.7f;
			if (m_timeSinceInteraction > fadeStart)
			{
				alpha = 1.0f - (m_timeSinceInteraction - fadeStart)
				             / (style.fadeDelaySec - fadeStart);
				alpha = std::max(0.0f, alpha);
			}
		}

		// Track.
		const float trackX = m_viewport.x() + m_viewport.width() - style.width;
		const sgc::Rectf trackRect{
			trackX, m_viewport.y(), style.width, m_viewport.height()};

		auto trackCol = style.trackColor;
		trackCol.a *= alpha;
		batch.drawRect(trackRect, trackCol);

		// Thumb.
		const float viewRatio = m_viewport.height() / m_contentH;
		const float thumbLen = std::max(
			style.minThumbLen, m_viewport.height() * viewRatio);
		const float scrollRange = m_viewport.height() - thumbLen;
		const float thumbY = m_viewport.y()
			+ normalizedScrollY() * scrollRange;

		auto thumbCol = style.thumbColor;
		thumbCol.a *= alpha;
		batch.drawRect(
			sgc::Rectf{trackX, thumbY, style.width, thumbLen}, thumbCol);
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
			return value * 0.3f;  // Resistance factor.
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
};

} // namespace mitiru::vn
