#pragma once
/// @file UISmartTooltip.hpp
/// @brief 高機能ツールチップシステム（自動配置・リッチテキスト・ピンモード・グループ制御）

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/UIStyle.hpp>

namespace mitiru::ui
{

/// @brief ツールチップ配置方向
enum class SmartTooltipPosition : std::uint8_t { Auto, Above, Below, Left, Right };

/// @brief ツールチップのコンテンツ種別
enum class TooltipContentType : std::uint8_t { PlainText, RichText, Custom };

/// @brief カスタム描画コールバック: (rect, opacity)
using TooltipDrawCallback = std::function<void(const sgc::Rectf&, float)>;

/// @brief SmartTooltipの設定
struct SmartTooltipConfig
{
	float showDelay = 0.4f, hideDelay = 0.15f;
	float maxWidth = 320.0f, padding = 10.0f, arrowSize = 8.0f;
	SmartTooltipPosition position = SmartTooltipPosition::Auto;
	bool arrowEnabled = true, followMouse = false, pinnable = false;
	float fadeInDuration = 0.12f, fadeOutDuration = 0.08f;
	float screenMargin = 6.0f, screenWidth = 1920.0f, screenHeight = 1080.0f;
	UIBoxStyle panelStyle;
};

/// @brief 高機能ツールチップ
/// @code
///   SmartTooltip tip(cfg);
///   tip.show("Save file", {100, 50, 80, 30});
///   tip.showRich("<b>Bold</b> text", anchor);
///   tip.update(dt);
/// @endcode
class SmartTooltip
{
	enum class Phase : std::uint8_t { Hidden, WaitShow, FadeIn, Visible, WaitHide, FadeOut };

	SmartTooltipConfig m_cfg;
	Phase m_phase = Phase::Hidden;
	float m_timer = 0.0f, m_opacity = 0.0f;
	bool m_pinned = false;
	TooltipContentType m_contentType = TooltipContentType::PlainText;
	std::string m_text;
	TooltipDrawCallback m_drawCb;
	sgc::Rectf m_anchor, m_bounds;
	SmartTooltipPosition m_resolved = SmartTooltipPosition::Below;
	float m_contentW = 0.0f, m_contentH = 0.0f;

public:
	explicit SmartTooltip(const SmartTooltipConfig& cfg = {}) : m_cfg(cfg) {}

	[[nodiscard]] bool isVisible() const noexcept { return m_phase >= Phase::FadeIn && m_phase <= Phase::FadeOut; }
	[[nodiscard]] float opacity() const noexcept { return m_opacity; }
	[[nodiscard]] bool isPinned() const noexcept { return m_pinned; }
	[[nodiscard]] const sgc::Rectf& bounds() const noexcept { return m_bounds; }
	[[nodiscard]] SmartTooltipPosition resolvedPosition() const noexcept { return m_resolved; }
	[[nodiscard]] const std::string& text() const noexcept { return m_text; }
	[[nodiscard]] TooltipContentType contentType() const noexcept { return m_contentType; }
	[[nodiscard]] const SmartTooltipConfig& config() const noexcept { return m_cfg; }
	[[nodiscard]] bool arrowEnabled() const noexcept { return m_cfg.arrowEnabled; }
	[[nodiscard]] const TooltipDrawCallback& drawCallback() const noexcept { return m_drawCb; }

	void setScreenBounds(float w, float h) noexcept { m_cfg.screenWidth = w; m_cfg.screenHeight = h; }
	void setConfig(const SmartTooltipConfig& c) { m_cfg = c; }
	void setContentSize(float w, float h) noexcept { m_contentW = std::min(w, m_cfg.maxWidth); m_contentH = h; resolve(); }

	void show(const std::string& text, const sgc::Rectf& anchor)
	{
		m_text = text; m_contentType = TooltipContentType::PlainText; m_drawCb = nullptr;
		m_anchor = anchor; requestShow();
	}

	void showRich(const std::string& richText, const sgc::Rectf& anchor)
	{
		m_text = richText; m_contentType = TooltipContentType::RichText; m_drawCb = nullptr;
		m_anchor = anchor; requestShow();
	}

	void showCustom(TooltipDrawCallback cb, const sgc::Rectf& anchor)
	{
		m_text.clear(); m_contentType = TooltipContentType::Custom; m_drawCb = std::move(cb);
		m_anchor = anchor; requestShow();
	}

	void hide()
	{
		if (m_pinned || m_phase == Phase::Hidden || m_phase == Phase::FadeOut) { return; }
		if (m_phase == Phase::WaitShow) { m_phase = Phase::Hidden; m_timer = 0; m_opacity = 0; return; }
		if (m_cfg.hideDelay > 0) { m_phase = Phase::WaitHide; m_timer = 0; } else { m_phase = Phase::FadeOut; m_timer = 0; }
	}

	void hideImmediate() { m_pinned = false; m_phase = Phase::Hidden; m_timer = 0; m_opacity = 0; }

	void togglePin()
	{
		if (!m_cfg.pinnable) { return; }
		m_pinned = !m_pinned;
		if (!m_pinned && m_phase == Phase::Visible) { hide(); }
	}

	void updateMousePosition(float x, float y) noexcept
	{
		if (m_cfg.followMouse && isVisible()) { m_anchor = {x, y, 0, 0}; resolve(); }
	}

	void update(float dt)
	{
		switch (m_phase) {
		case Phase::WaitShow: m_timer += dt; if (m_timer >= m_cfg.showDelay) { m_phase = Phase::FadeIn; m_timer = 0; resolve(); } break;
		case Phase::FadeIn: m_timer += dt; m_opacity = m_cfg.fadeInDuration > 0 ? std::clamp(m_timer / m_cfg.fadeInDuration, 0.f, 1.f) : 1.f; if (m_opacity >= 1.f) { m_phase = Phase::Visible; m_opacity = 1.f; } break;
		case Phase::WaitHide: m_timer += dt; if (m_timer >= m_cfg.hideDelay) { m_phase = Phase::FadeOut; m_timer = 0; } break;
		case Phase::FadeOut: m_timer += dt; m_opacity = m_cfg.fadeOutDuration > 0 ? std::clamp(1.f - m_timer / m_cfg.fadeOutDuration, 0.f, 1.f) : 0.f; if (m_opacity <= 0.f) { m_phase = Phase::Hidden; m_opacity = 0; } break;
		default: break;
		}
	}

private:
	void requestShow()
	{
		if (m_phase == Phase::Hidden) { if (m_cfg.showDelay > 0) { m_phase = Phase::WaitShow; m_timer = 0; } else { m_phase = Phase::FadeIn; m_timer = 0; resolve(); } }
		else if (m_phase == Phase::FadeOut || m_phase == Phase::WaitHide) { m_phase = Phase::FadeIn; m_timer = 0; resolve(); }
	}

	void resolve()
	{
		const float tw = m_contentW + m_cfg.padding * 2, th = m_contentH + m_cfg.padding * 2;
		const float arr = m_cfg.arrowEnabled ? m_cfg.arrowSize : 0;
		m_resolved = m_cfg.position;
		if (m_resolved == SmartTooltipPosition::Auto) { m_resolved = autoPos(tw, th, arr); }
		const float acx = m_anchor.x() + m_anchor.width() * 0.5f;
		const float acy = m_anchor.y() + m_anchor.height() * 0.5f;
		float x = 0, y = 0;
		switch (m_resolved) {
		case SmartTooltipPosition::Above: x = acx - tw * 0.5f; y = m_anchor.y() - th - arr; break;
		case SmartTooltipPosition::Below: x = acx - tw * 0.5f; y = m_anchor.y() + m_anchor.height() + arr; break;
		case SmartTooltipPosition::Left:  x = m_anchor.x() - tw - arr; y = acy - th * 0.5f; break;
		case SmartTooltipPosition::Right: x = m_anchor.x() + m_anchor.width() + arr; y = acy - th * 0.5f; break;
		default: break;
		}
		const float m = m_cfg.screenMargin;
		m_bounds = {std::clamp(x, m, m_cfg.screenWidth - tw - m), std::clamp(y, m, m_cfg.screenHeight - th - m), tw, th};
	}

	[[nodiscard]] SmartTooltipPosition autoPos(float tw, float th, float arr) const noexcept
	{
		const float m = m_cfg.screenMargin;
		if (m_cfg.screenHeight - m_anchor.y() - m_anchor.height() - m >= th + arr) { return SmartTooltipPosition::Below; }
		if (m_anchor.y() - m >= th + arr) { return SmartTooltipPosition::Above; }
		if (m_cfg.screenWidth - m_anchor.x() - m_anchor.width() - m >= tw + arr) { return SmartTooltipPosition::Right; }
		if (m_anchor.x() - m >= tw + arr) { return SmartTooltipPosition::Left; }
		return SmartTooltipPosition::Below;
	}
};

/// @brief グループ内で同時に1つだけ表示されるツールチップ管理
class SmartTooltipGroup
{
	struct Entry { std::string key; SmartTooltip tooltip; };
	std::vector<Entry> m_entries;
	std::string m_activeKey;

public:
	void add(const std::string& key, const SmartTooltipConfig& cfg = {})
	{
		for (const auto& e : m_entries) { if (e.key == key) { return; } }
		m_entries.push_back({key, SmartTooltip{cfg}});
	}

	void show(const std::string& key, const std::string& text, const sgc::Rectf& anchor)
	{
		for (auto& e : m_entries) {
			if (e.key == key) { e.tooltip.show(text, anchor); }
			else if (e.key == m_activeKey) { e.tooltip.hide(); }
		}
		m_activeKey = key;
	}

	void hideActive()
	{
		for (auto& e : m_entries) { if (e.key == m_activeKey) { e.tooltip.hide(); break; } }
		m_activeKey.clear();
	}

	void update(float dt) { for (auto& e : m_entries) { e.tooltip.update(dt); } }

	[[nodiscard]] SmartTooltip* get(const std::string& key)
	{
		for (auto& e : m_entries) { if (e.key == key) { return &e.tooltip; } }
		return nullptr;
	}
	[[nodiscard]] const std::string& activeKey() const noexcept { return m_activeKey; }
	[[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }
};

} // namespace mitiru::ui
