#pragma once

/// @file ScrollContainer.hpp
/// @brief viewport clipping と慣性を備えた汎用 scrollable container
/// @details 滑らかな momentum を伴う縦横 scroll、optional な scroll bar、
///          snap-to-item、overscroll bounce を提供する。
///          BacklogUI など scroll が必要な画面で使用する。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/SpriteBatch.hpp>

namespace mitiru::vn
{

/// @brief scroll bar の外観設定
struct ScrollBarStyle
{
	sgc::Colorf trackColor{0.2f, 0.2f, 0.2f, 0.3f};   ///< track の背景色
	sgc::Colorf thumbColor{0.6f, 0.6f, 0.6f, 0.5f};   ///< thumb の色
	float width       = 6.0f;                            ///< bar の幅（ピクセル）
	float minThumbLen = 20.0f;                           ///< thumb の最小長
	bool autoHide     = true;                            ///< scroll していない時に隠すか
	float fadeDelaySec = 1.5f;                           ///< fade out までの秒数
};

/// @brief ScrollContainer の挙動設定
struct ScrollConfig
{
	bool verticalScroll    = true;     ///< 縦 scroll を許可するか
	bool horizontalScroll  = false;    ///< 横 scroll を許可するか
	bool showScrollBar     = true;     ///< 縦 scroll bar を表示するか
	bool showHScrollBar    = false;    ///< 横 scroll bar を表示するか
	bool bounceEffect      = true;     ///< overscroll の rubber-band bounce
	bool snapToItem        = false;    ///< scroll 後に item 境界へ snap するか
	float snapItemHeight   = 0.0f;    ///< snap 用の item 高さ（0 = 無効）
	float inertiaDamping   = 0.92f;   ///< フレームごとの velocity 減衰（0-1）
	float bounceDamping    = 0.6f;    ///< bounce の戻り速度係数
	float wheelMultiplier  = 40.0f;   ///< マウスホイール 1 ノッチあたりのピクセル数

	ScrollBarStyle scrollBar;          ///< scroll bar の外観
};

/// @brief viewport clipping と momentum を備えた汎用 scrollable container
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
	sgc::Rectf m_viewport{};           ///< screen 空間での可視領域
	float m_contentW   = 0.0f;         ///< コンテンツ全体の幅
	float m_contentH   = 0.0f;         ///< コンテンツ全体の高さ
	float m_scrollX    = 0.0f;         ///< 現在の横 scroll offset
	float m_scrollY    = 0.0f;         ///< 現在の縦 scroll offset
	float m_velocityX  = 0.0f;         ///< 横 scroll の velocity
	float m_velocityY  = 0.0f;         ///< 縦 scroll の velocity
	float m_timeSinceInteraction = 0.0f; ///< scroll bar auto-hide 用タイマー
	bool  m_dragging   = false;        ///< タッチ/マウス drag 中か
	float m_dragStartY = 0.0f;         ///< drag 開始位置（screen Y）
	float m_dragStartX = 0.0f;         ///< drag 開始位置（screen X）
	float m_dragScrollY = 0.0f;        ///< drag 開始時の scroll offset
	float m_dragScrollX = 0.0f;        ///< drag 開始時の scroll offset

	ScrollConfig m_config;

public:
	/// @brief viewport とコンテンツ寸法を指定して構築する
	/// @param viewport screen 空間での可視領域
	/// @param contentW コンテンツ全体の幅（縦のみの場合は 0）
	/// @param contentH コンテンツ全体の高さ
	/// @param config scroll の挙動設定
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

	// ── アクセサ ────────────────────────────────────────────

	/// @brief 現在の viewport 矩形
	[[nodiscard]] const sgc::Rectf& viewport() const noexcept { return m_viewport; }

	/// @brief viewport 矩形を設定する
	void setViewport(const sgc::Rectf& vp) noexcept { m_viewport = vp; }

	/// @brief 現在の縦 scroll offset（上端からのピクセル数）
	[[nodiscard]] float scrollY() const noexcept { return m_scrollY; }

	/// @brief 現在の横 scroll offset
	[[nodiscard]] float scrollX() const noexcept { return m_scrollX; }

	/// @brief コンテンツ全体の高さ
	[[nodiscard]] float contentHeight() const noexcept { return m_contentH; }

	/// @brief コンテンツ全体の幅
	[[nodiscard]] float contentWidth() const noexcept { return m_contentW; }

	/// @brief コンテンツ全体の高さを設定する（内容変更時に呼ぶ）
	void setContentHeight(float h) noexcept { m_contentH = h; }

	/// @brief コンテンツ全体の幅を設定する
	void setContentWidth(float w) noexcept { m_contentW = w; }

	/// @brief 設定にアクセスする
	[[nodiscard]] const ScrollConfig& config() const noexcept { return m_config; }

	/// @brief 設定を置き換える
	void setConfig(const ScrollConfig& cfg) noexcept { m_config = cfg; }

	/// @brief 有効な縦 scroll offset の最大値
	[[nodiscard]] float maxScrollY() const noexcept
	{
		return std::max(0.0f, m_contentH - m_viewport.height());
	}

	/// @brief 有効な横 scroll offset の最大値
	[[nodiscard]] float maxScrollX() const noexcept
	{
		return std::max(0.0f, m_contentW - m_viewport.width());
	}

	/// @brief コンテンツが viewport より高いか
	[[nodiscard]] bool canScrollVertically() const noexcept
	{
		return m_config.verticalScroll && m_contentH > m_viewport.height();
	}

	/// @brief コンテンツが viewport より広いか
	[[nodiscard]] bool canScrollHorizontally() const noexcept
	{
		return m_config.horizontalScroll && m_contentW > m_viewport.width();
	}

	/// @brief 正規化した縦 scroll 位置 [0, 1]
	[[nodiscard]] float normalizedScrollY() const noexcept
	{
		const float maxY = maxScrollY();
		return (maxY > 0.0f) ? (m_scrollY / maxY) : 0.0f;
	}

	/// @brief screen 空間の点が viewport 内にあるか
	[[nodiscard]] bool containsPoint(float x, float y) const noexcept
	{
		return x >= m_viewport.x() && x < m_viewport.x() + m_viewport.width()
		    && y >= m_viewport.y() && y < m_viewport.y() + m_viewport.height();
	}

	// ── プログラム制御の scroll ──────────────────────────────────

	/// @brief 縦 scroll 位置を直接設定する
	/// @param y scroll offset（ピクセル）
	void setScrollY(float y) noexcept
	{
		m_scrollY = clampScroll(y, maxScrollY());
		m_velocityY = 0.0f;
	}

	/// @brief 横 scroll 位置を直接設定する
	void setScrollX(float x) noexcept
	{
		m_scrollX = clampScroll(x, maxScrollX());
		m_velocityX = 0.0f;
	}

	/// @brief 先頭まで scroll する
	void scrollToTop() noexcept
	{
		m_scrollY = 0.0f;
		m_velocityY = 0.0f;
	}

	/// @brief 末尾まで scroll する
	void scrollToBottom() noexcept
	{
		m_scrollY = maxScrollY();
		m_velocityY = 0.0f;
	}

	/// @brief 縦方向の領域が可視になるよう最小限 scroll する
	/// @param itemTop コンテンツ空間での item 上端
	/// @param itemHeight item の高さ
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

	// ── 入力処理 ───────────────────────────────────────

	/// @brief マウスホイール入力を処理する
	/// @param delta ホイール delta（負 = 下方向 scroll）
	void onMouseWheel(float delta)
	{
		if (m_config.verticalScroll)
		{
			m_velocityY -= delta * m_config.wheelMultiplier;
		}
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief 横方向のマウスホイールを処理する
	/// @param delta ホイール delta（負 = 右方向 scroll）
	void onMouseWheelH(float delta)
	{
		if (m_config.horizontalScroll)
		{
			m_velocityX -= delta * m_config.wheelMultiplier;
		}
		m_timeSinceInteraction = 0.0f;
	}

	/// @brief drag/タッチ scroll を開始する
	/// @param screenX screen X 位置
	/// @param screenY screen Y 位置
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

	/// @brief drag 位置を更新する
	/// @param screenX 現在の screen X
	/// @param screenY 現在の screen Y
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

	/// @brief release velocity を与えて drag/タッチ scroll を終了する
	/// @param velocityX release 時の velocity X（ピクセル/秒）
	/// @param velocityY release 時の velocity Y（ピクセル/秒）
	void onDragEnd(float velocityX = 0.0f, float velocityY = 0.0f)
	{
		m_dragging = false;
		m_velocityX = velocityX;
		m_velocityY = velocityY;
		m_timeSinceInteraction = 0.0f;
	}

	// ── 更新 ───────────────────────────────────────────────

	/// @brief scroll の物理挙動を更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		m_timeSinceInteraction += dt;

		if (m_dragging) return;

		// 慣性付きで velocity を適用
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

		// overscroll から bounce で戻す
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

		// momentum が収まった後に item 境界へ snap する
		if (m_config.snapToItem && m_config.snapItemHeight > 0.0f
		    && m_velocityY == 0.0f && !m_dragging)
		{
			const float itemH = m_config.snapItemHeight;
			const float snapped = std::round(m_scrollY / itemH) * itemH;
			m_scrollY += (snapped - m_scrollY) * std::min(1.0f, dt * 10.0f);
		}
	}

	// ── 描画 ────────────────────────────────────────────

	/// @brief 縦 scroll bar を描画する
	/// @param batch 描画先の SpriteBatch（begin/end の間で呼ぶこと）
	void drawScrollBar(render::SpriteBatch& batch) const
	{
		if (!m_config.showScrollBar || !canScrollVertically())
		{
			return;
		}

		const auto& style = m_config.scrollBar;

		// auto-hide の処理
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

		// track
		const float trackX = m_viewport.x() + m_viewport.width() - style.width;
		const sgc::Rectf trackRect{
			trackX, m_viewport.y(), style.width, m_viewport.height()};

		auto trackCol = style.trackColor;
		trackCol.a *= alpha;
		batch.drawRect(trackRect, trackCol);

		// thumb
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
	/// @brief scroll offset を有効範囲に clamp する
	[[nodiscard]] static float clampScroll(float value, float maxVal) noexcept
	{
		if (value < 0.0f) return 0.0f;
		if (value > maxVal) return maxVal;
		return value;
	}

	/// @brief rubber-band overscroll の抵抗を適用する
	[[nodiscard]] static float applyOverscroll(float value, float maxVal) noexcept
	{
		if (value < 0.0f)
		{
			return value * 0.3f;  // 抵抗係数
		}
		if (value > maxVal)
		{
			return maxVal + (value - maxVal) * 0.3f;
		}
		return value;
	}

	/// @brief overscroll から有効範囲へ spring back させる
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
