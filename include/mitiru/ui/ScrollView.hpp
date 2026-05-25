#pragma once

/// @file ScrollView.hpp
/// @brief UINode tree 用の汎用 scroll コンテナ。
/// @details 慣性 / bounce 付きの垂直・水平 scroll、大量リスト向けの virtual
///          scroll、UIEvent 処理の統合を提供する。
///          vn::ScrollContainer と同じ物理モデルだが、生の SpriteBatch 描画では
///          なく UINode tree と協調する設計。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <mitiru/ui/UINode.hpp>

namespace mitiru::ui
{

/// @brief Scroll bar の見た目スタイル。
struct ScrollBarStyle
{
	float width           = 6.0f;    ///< bar 幅 (px)。
	float minThumbLength  = 20.0f;   ///< thumb の最小サイズ。
	float cornerRadius    = 3.0f;    ///< 角丸半径。
	bool autoHide         = true;    ///< idle 時に隠す。
	float fadeDelaySec    = 1.5f;    ///< fade 開始までの秒数。
	float opacity         = 0.5f;    ///< thumb の最大不透明度。
};

/// @brief ScrollView の挙動設定。
struct ScrollViewConfig
{
	sgc::Rectf viewport{};                ///< screen space 上の可視領域。
	float contentHeight   = 0.0f;         ///< content 全体の高さ。
	float contentWidth    = 0.0f;         ///< content 全体の幅。

	bool verticalScroll   = true;         ///< 垂直 scroll を許可。
	bool horizontalScroll = false;        ///< 水平 scroll を許可。
	bool bounceEffect     = true;         ///< overscroll 時のラバーバンド bounce。
	bool snapToItem       = false;        ///< item 境界に snap する。
	float snapItemHeight  = 0.0f;         ///< snap 用の item 高さ (0 = 無効)。
	float inertiaDamping  = 0.92f;        ///< frame ごとの velocity 減衰 (0-1)。
	float bounceDamping   = 0.6f;         ///< bounce 復帰速度の係数。
	float wheelMultiplier = 40.0f;        ///< mouse wheel 1 ノッチあたりの px。
	float keyboardScrollAmount = 40.0f;   ///< keyboard scroll 1 ステップあたりの px。

	ScrollBarStyle scrollBar;             ///< scroll bar の外観。

	/// @brief 大量リスト向けに virtual scroll を有効化。
	bool virtualScrolling = false;
	/// @brief virtual scroll 用の item 高さ (全 item 同一高さ)。
	float virtualItemHeight = 0.0f;
};

/// @brief virtual scroll モードでの可視範囲の情報。
struct VirtualScrollRange
{
	std::size_t firstVisible = 0;   ///< 最初に見える item の index。
	std::size_t lastVisible  = 0;   ///< 最後に見える item の index (inclusive)。
	float offsetY            = 0.0f; ///< 最初に見える item の Y offset。
};

/// @brief UINode tree 用の汎用 scroll コンテナ。
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
	/// @brief 設定を与えて構築する。
	/// @param config scroll 挙動の設定。
	explicit ScrollView(ScrollViewConfig config = {}) noexcept
		: m_config(config)
	{
	}

	// ── Accessors ────────────────────────────────────────────

	/// @brief 現在の viewport 矩形。
	[[nodiscard]] const sgc::Rectf& viewport() const noexcept { return m_config.viewport; }

	/// @brief viewport 矩形を設定する。
	void setViewport(const sgc::Rectf& vp) noexcept { m_config.viewport = vp; }

	/// @brief 現在の垂直 scroll offset (上端からの px)。
	[[nodiscard]] float scrollY() const noexcept { return m_scrollY; }

	/// @brief 現在の水平 scroll offset。
	[[nodiscard]] float scrollX() const noexcept { return m_scrollX; }

	/// @brief content 全体の高さ。
	[[nodiscard]] float contentHeight() const noexcept { return m_config.contentHeight; }

	/// @brief content 全体の幅。
	[[nodiscard]] float contentWidth() const noexcept { return m_config.contentWidth; }

	/// @brief content 全体の高さを設定 (content 変更時に呼ぶ)。
	void setContentHeight(float h) noexcept { m_config.contentHeight = h; }

	/// @brief content 全体の幅を設定する。
	void setContentWidth(float w) noexcept { m_config.contentWidth = w; }

	/// @brief 設定にアクセスする。
	[[nodiscard]] const ScrollViewConfig& config() const noexcept { return m_config; }

	/// @brief 設定を差し替える。
	void setConfig(const ScrollViewConfig& cfg) noexcept { m_config = cfg; }

	/// @brief 有効な垂直 scroll offset の最大値。
	[[nodiscard]] float maxScrollY() const noexcept
	{
		return std::max(0.0f, m_config.contentHeight - m_config.viewport.height());
	}

	/// @brief 有効な水平 scroll offset の最大値。
	[[nodiscard]] float maxScrollX() const noexcept
	{
		return std::max(0.0f, m_config.contentWidth - m_config.viewport.width());
	}

	/// @brief content が viewport より高いか。
	[[nodiscard]] bool canScrollVertically() const noexcept
	{
		return m_config.verticalScroll
			&& m_config.contentHeight > m_config.viewport.height();
	}

	/// @brief content が viewport より幅広いか。
	[[nodiscard]] bool canScrollHorizontally() const noexcept
	{
		return m_config.horizontalScroll
			&& m_config.contentWidth > m_config.viewport.width();
	}

	/// @brief 正規化された垂直 scroll 位置 [0, 1]。
	[[nodiscard]] float normalizedScrollY() const noexcept
	{
		const float maxY = maxScrollY();
		return (maxY > 0.0f) ? (m_scrollY / maxY) : 0.0f;
	}

	/// @brief 正規化された水平 scroll 位置 [0, 1]。
	[[nodiscard]] float normalizedScrollX() const noexcept
	{
		const float maxX = maxScrollX();
		return (maxX > 0.0f) ? (m_scrollX / maxX) : 0.0f;
	}

	/// @brief screen space 上の点が viewport 内かどうか。
	[[nodiscard]] bool containsPoint(float x, float y) const noexcept
	{
		return x >= m_config.viewport.x()
			&& x < m_config.viewport.x() + m_config.viewport.width()
			&& y >= m_config.viewport.y()
			&& y < m_config.viewport.y() + m_config.viewport.height();
	}

	/// @brief scroll view が現在 drag 中かどうか。
	[[nodiscard]] bool isDragging() const noexcept { return m_dragging; }

	/// @brief scroll view に何らかの momentum があるか。
	[[nodiscard]] bool isScrolling() const noexcept
	{
		return m_dragging
			|| std::abs(m_velocityX) > 0.5f
			|| std::abs(m_velocityY) > 0.5f;
	}

	// ── Programmatic scroll ──────────────────────────────────

	/// @brief 垂直 scroll 位置を直接設定する。
	void setScrollY(float y) noexcept
	{
		m_scrollY = clampScroll(y, maxScrollY());
		m_velocityY = 0.0f;
	}

	/// @brief 水平 scroll 位置を直接設定する。
	void setScrollX(float x) noexcept
	{
		m_scrollX = clampScroll(x, maxScrollX());
		m_velocityX = 0.0f;
	}

	/// @brief 先頭まで scroll する。
	void scrollToTop() noexcept
	{
		m_scrollY = 0.0f;
		m_velocityY = 0.0f;
	}

	/// @brief 末尾まで scroll する。
	void scrollToBottom() noexcept
	{
		m_scrollY = maxScrollY();
		m_velocityY = 0.0f;
	}

	/// @brief 垂直方向の領域が見えるように最小限の scroll を行う。
	/// @param itemTop content space 上の item 上端。
	/// @param itemHeight item の高さ。
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

	/// @brief mouse wheel 入力を処理する。
	/// @param delta wheel delta (負 = 下方向 scroll)。
	void onMouseWheel(float delta)
	{
		if (m_config.verticalScroll)
		{
			m_velocityY -= delta * m_config.wheelMultiplier;
		}
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief 水平 mouse wheel を処理する。
	/// @param delta wheel delta (負 = 右方向 scroll)。
	void onMouseWheelH(float delta)
	{
		if (m_config.horizontalScroll)
		{
			m_velocityX -= delta * m_config.wheelMultiplier;
		}
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief keyboard scroll を処理する (矢印キー, Page Up/Down 等)。
	/// @param deltaX 水平方向 (-1, 0, +1)。
	/// @param deltaY 垂直方向 (-1, 0, +1)。
	/// @param pageScroll 1 ステップでなく 1 ページ分 scroll するか。
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

	/// @brief drag / touch scroll を開始する。
	/// @param screenX screen X 位置。
	/// @param screenY screen Y 位置。
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

	/// @brief drag 位置を更新する。
	/// @param screenX 現在の screen X。
	/// @param screenY 現在の screen Y。
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

	/// @brief release velocity を与えて drag / touch scroll を終了する。
	/// @param velocityX release 時の velocity X (px/sec)。
	/// @param velocityY release 時の velocity Y (px/sec)。
	void onDragEnd(float velocityX = 0.0f, float velocityY = 0.0f)
	{
		m_dragging = false;
		m_velocityX = velocityX;
		m_velocityY = velocityY;
		m_timeSinceInteraction = 0.0f;
	}

	// ── Virtual scrolling ────────────────────────────────────

	/// @brief virtual scroll での可視範囲を計算する。
	/// @param totalItems リスト内の item 総数。
	/// @return 可視 item index の範囲と Y offset。
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

	/// @brief scroll offset を UINode の子要素位置に適用する。
	/// @details 各子要素の Y 位置を -scrollY、X 位置を -scrollX だけずらす。
	///          viewport から完全にはみ出た子要素は invisible にする。
	/// @param parent scroll 対象の子要素を持つコンテナ UINode。
	void applyToChildren(UINode& parent) const
	{
		for (std::size_t i = 0; i < parent.childCount(); ++i)
		{
			auto& child = parent.child(i);
			auto bounds = child.bounds();

			// scroll 位置の分だけ offset。
			bounds = sgc::Rectf{
				bounds.x() - m_scrollX,
				bounds.y() - m_scrollY,
				bounds.width(),
				bounds.height()
			};
			child.setBounds(bounds);

			// viewport 外の子要素を cull する。
			const bool inView =
				(bounds.y() + bounds.height() > m_config.viewport.y())
				&& (bounds.y() < m_config.viewport.y() + m_config.viewport.height())
				&& (bounds.x() + bounds.width() > m_config.viewport.x())
				&& (bounds.x() < m_config.viewport.x() + m_config.viewport.width());
			child.setVisible(inView);
		}
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief scroll の物理を更新する。
	/// @param dt delta time (秒)。
	void update(float dt)
	{
		m_timeSinceInteraction += dt;

		if (m_dragging) return;

		// 慣性付きで velocity を適用。
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

		// overscroll から bounce back する。
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

		// momentum が収まったら item 境界に snap する。
		if (m_config.snapToItem && m_config.snapItemHeight > 0.0f
			&& m_velocityY == 0.0f && !m_dragging)
		{
			const float itemH = m_config.snapItemHeight;
			const float snapped = std::round(m_scrollY / itemH) * itemH;
			m_scrollY += (snapped - m_scrollY) * std::min(1.0f, dt * 10.0f);
		}
	}

	// ── Scroll bar state ─────────────────────────────────────

	/// @brief 垂直 scroll bar を表示すべきかどうか。
	[[nodiscard]] bool isScrollBarVisible() const noexcept
	{
		if (!canScrollVertically()) return false;
		if (!m_config.scrollBar.autoHide) return true;
		return m_timeSinceInteraction < m_config.scrollBar.fadeDelaySec;
	}

	/// @brief 現在の scroll bar の不透明度 (auto-hide の fade を加味)。
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

	/// @brief scroll bar の thumb 矩形を計算する (垂直)。
	/// @return screen space 上の thumb 矩形。
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
	/// @brief scroll offset を有効範囲に clamp する。
	[[nodiscard]] static float clampScroll(float value, float maxVal) noexcept
	{
		if (value < 0.0f) return 0.0f;
		if (value > maxVal) return maxVal;
		return value;
	}

	/// @brief ラバーバンド式の overscroll 抵抗を適用する。
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

	/// @brief overscroll から有効範囲へ spring back させる。
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
