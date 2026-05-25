#pragma once

/// @file UITooltip.hpp
/// @brief hover ポップアップの tooltip widget。auto-positioning、fade アニメーション、UINode への attach に対応。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mitiru::ui {

/// @brief 対象要素に対する anchor 位置。
enum class TooltipPosition : std::uint8_t
{
	Above,
	Below,
	Left,
	Right,
	Auto ///< 利用可能な画面スペースに応じて自動選択する。
};

/// @brief UITooltip 生成用の設定。
struct UITooltipConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;
	std::string text;

	// ── Layout ────────────────────────────────────────────────
	float maxWidth = 300.0f;         ///< テキストが折り返す前の最大幅。
	float padding = 8.0f;            ///< 全方向の内側 padding。
	float arrowSize = 6.0f;          ///< 方向矢印のサイズ。
	float anchorOffsetX = 0.0f;      ///< anchor 点からの X offset。
	float anchorOffsetY = 0.0f;      ///< anchor 点からの Y offset。
	TooltipPosition position = TooltipPosition::Auto;

	// ── Timing ────────────────────────────────────────────────
	float showDelay = 0.5f;          ///< tooltip 出現までの秒数。
	float hideDelay = 0.1f;          ///< pointer が離れてから非表示までの秒数。
	float fadeInDuration = 0.15f;    ///< fade-in アニメーションの秒数。
	float fadeOutDuration = 0.1f;    ///< fade-out アニメーションの秒数。

	// ── Behavior ──────────────────────────────────────────────
	bool followMouse = false;        ///< true なら tooltip が cursor 位置を追従する。
	float screenMargin = 4.0f;       ///< 画面端からの最小 margin。

	// ── Screen bounds (for auto-positioning) ──────────────────
	float screenWidth = 1920.0f;     ///< 境界 clamp 用の画面幅。
	float screenHeight = 1080.0f;    ///< 境界 clamp 用の画面高さ。

	// ── Image keys ────────────────────────────────────────────
	std::string backgroundImageKey;  ///< tooltip 背景の image key。
	std::string borderImageKey;      ///< tooltip の border / frame の image key。
	std::string arrowImageKey;       ///< 方向矢印の image key。
};

/// @brief cursor または anchor 要素の近くに文脈情報を表示する tooltip widget。
///
/// show/hide の delay、fade アニメーション、画面境界内での auto-positioning、
/// 任意の UINode への attach を管理する。描画は外部の UIRenderer が担う。
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
	/// @brief tooltip lifecycle の内部 phase。
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
	/// @brief 設定から tooltip を構築する。
	/// @param config tooltip 設定。
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

	/// @brief 基となる UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_node; }

	/// @brief tooltip が現在 visible か判定する (fade 中を含む)。
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_phase == Phase::FadingIn
			|| m_phase == Phase::Visible
			|| m_phase == Phase::WaitingToHide
			|| m_phase == Phase::FadingOut;
	}

	/// @brief 現在の不透明度を取得する (0.0 = 完全透明、1.0 = 完全不透明)。
	[[nodiscard]] float opacity() const noexcept { return m_opacity; }

	/// @brief 現在の tooltip 境界を screen space で取得する。
	[[nodiscard]] sgc::Rectf currentBounds() const noexcept { return m_node->bounds(); }

	/// @brief 解決済みの anchor 位置を取得する (auto-positioning 適用後)。
	[[nodiscard]] TooltipPosition resolvedPosition() const noexcept { return m_resolvedPosition; }

	/// @brief tooltip テキストを取得する。
	[[nodiscard]] const std::string& text() const noexcept { return m_node->text(); }

	/// @brief 背景の image key を取得する。
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief border の image key を取得する。
	[[nodiscard]] const std::string& borderImageKey() const noexcept { return m_borderImageKey; }

	/// @brief 矢印の image key を取得する。
	[[nodiscard]] const std::string& arrowImageKey() const noexcept { return m_arrowImageKey; }

	// ── Actions ───────────────────────────────────────────────

	/// @brief 指定 anchor 位置に tooltip 表示を要求する。
	/// @param text 表示するテキスト (rich text の素通しに対応)。
	/// @param x screen space の anchor X 位置。
	/// @param y screen space の anchor Y 位置。
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

	/// @brief 直前に設定したテキストで tooltip 表示を要求する。
	/// @param x anchor X 位置。
	/// @param y anchor Y 位置。
	void show(float x, float y)
	{
		show(m_node->text(), x, y);
	}

	/// @brief tooltip の非表示を要求する (hide delay / fade-out を開始)。
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

	/// @brief delay やアニメーション無しで即座に非表示にする。
	void hideImmediate()
	{
		m_phase = Phase::Hidden;
		m_timer = 0.0f;
		m_opacity = 0.0f;
		syncNodeState();
	}

	/// @brief tooltip を UINode へ attach する (hover で自動表示)。
	/// @param target attach 先の node。
	void attachTo(std::shared_ptr<UINode> target)
	{
		m_attachedNode = target;
	}

	/// @brief 現在 attach 中の UINode から detach する。
	void detach()
	{
		m_attachedNode.reset();
	}

	/// @brief followMouse 有効時に cursor 位置を更新する。
	/// @param x 現在の cursor X。
	/// @param y 現在の cursor Y。
	void updateCursorPosition(float x, float y) noexcept
	{
		if (m_followMouse && isVisible())
		{
			m_anchorX = x;
			m_anchorY = y;
			resolvePositionAndClamp();
		}
	}

	/// @brief 推定 content サイズを設定する (layout / renderer から呼ばれる)。
	/// @param width content 幅。
	/// @param height content 高さ。
	void setContentSize(float width, float height) noexcept
	{
		m_contentWidth = std::min(width, m_maxWidth);
		m_contentHeight = height;
		resolvePositionAndClamp();
	}

	/// @brief auto-positioning 用の画面境界を設定する。
	/// @param width 画面幅。
	/// @param height 画面高さ。
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
	}

	// ── Update ────────────────────────────────────────────────

	/// @brief tooltip のアニメーションと timing を進める。
	/// @param dt delta time (秒)。
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
			// 定常状態。更新は不要。
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

	/// @brief tooltip 位置を解決し画面境界内に clamp する。
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
			break; // 上で解決済み。
		}

		// 画面境界内に clamp する。
		x = std::clamp(x, margin, m_screenWidth - totalW - margin);
		y = std::clamp(y, margin, m_screenHeight - totalH - margin);

		m_node->setBounds(sgc::Rectf(x, y, totalW, totalH));
	}

	/// @brief Auto 選択時に最適な位置を選ぶ。
	[[nodiscard]] TooltipPosition chooseAutoPosition(float totalW, float totalH) const noexcept
	{
		const float margin = m_screenMargin;
		const float spaceAbove = m_anchorY - margin;
		const float spaceBelow = m_screenHeight - m_anchorY - margin;
		const float spaceLeft = m_anchorX - margin;
		const float spaceRight = m_screenWidth - m_anchorX - margin;

		// below を優先し、次に above、right、left の順。
		if (spaceBelow >= totalH + m_arrowSize) { return TooltipPosition::Below; }
		if (spaceAbove >= totalH + m_arrowSize) { return TooltipPosition::Above; }
		if (spaceRight >= totalW + m_arrowSize) { return TooltipPosition::Right; }
		if (spaceLeft >= totalW + m_arrowSize)  { return TooltipPosition::Left; }

		return TooltipPosition::Below; // fallback。
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
