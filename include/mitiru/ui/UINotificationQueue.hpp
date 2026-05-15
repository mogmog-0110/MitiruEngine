#pragma once
/// @file UINotificationQueue.hpp
/// @brief キュー付き通知/トーストシステム（プログレスバー・アクションボタン対応）

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::ui
{

/// @brief 通知種別
enum class NotificationType : std::uint8_t { Info, Success, Warning, Error, Achievement };

/// @brief 通知表示位置
enum class NotificationPosition : std::uint8_t { TopRight, TopLeft, BottomRight, BottomLeft, TopCenter, BottomCenter };

/// @brief アクションボタン
struct NotificationAction
{
	std::string label;
	std::function<void()> onClick;
};

/// @brief 通知データ
struct UINotification
{
	NotificationType type = NotificationType::Info;
	std::string title, message, icon;
	float duration = 5.0f;                          ///< 自動非表示（負値=手動のみ）
	std::function<void()> onClick;
	std::function<float()> getProgress;             ///< プログレス取得（nullなら非表示）
	std::vector<NotificationAction> actions;
};

/// @brief キュー設定
struct NotificationQueueConfig
{
	NotificationPosition position = NotificationPosition::TopRight;
	int maxVisible = 5;
	float width = 350.0f, minHeight = 60.0f, spacing = 8.0f;
	float marginX = 16.0f, marginY = 16.0f;
	float slideInDuration = 0.25f, slideOutDuration = 0.2f;
	float screenWidth = 1920.0f, screenHeight = 1080.0f;
};

/// @brief 通知1件のレンダリング情報
struct NotificationRenderInfo
{
	std::uint32_t id = 0;
	NotificationType type = NotificationType::Info;
	std::string title, message, icon;
	sgc::Rectf bounds;
	float opacity = 1.0f, slideOffset = 0.0f, progress = -1.0f;
	std::vector<std::string> actionLabels;
};

/// @brief キュー付き通知管理
/// @code
///   UINotificationQueue q(cfg);
///   q.pushInfo("Save", "File saved.");
///   q.pushWithProgress("Download", [&]{ return prog; });
///   q.update(dt);
///   auto vis = q.getVisible();
/// @endcode
class UINotificationQueue
{
	enum class Phase : std::uint8_t { SlideIn, Visible, SlideOut, Dismissed };
	struct Active
	{
		std::uint32_t id = 0;
		UINotification notif;
		Phase phase = Phase::SlideIn;
		float phaseT = 0, elapsed = 0, anim = 0, height = 0;
	};

	NotificationQueueConfig m_cfg;
	std::vector<Active> m_active;
	std::vector<UINotification> m_pending;
	std::uint32_t m_nextId = 1;
	std::function<void(std::uint32_t)> m_onDismissed;

public:
	explicit UINotificationQueue(const NotificationQueueConfig& cfg = {}) : m_cfg(cfg) {}

	void setScreenBounds(float w, float h) noexcept { m_cfg.screenWidth = w; m_cfg.screenHeight = h; }
	void setOnDismissed(std::function<void(std::uint32_t)> cb) { m_onDismissed = std::move(cb); }
	[[nodiscard]] const NotificationQueueConfig& config() const noexcept { return m_cfg; }

	std::uint32_t push(const UINotification& n)
	{
		const auto id = m_nextId++;
		if (visCnt() >= static_cast<std::size_t>(m_cfg.maxVisible)) { m_pending.push_back(n); return id; }
		activate(id, n); return id;
	}

	std::uint32_t pushInfo(const std::string& t, const std::string& m) { UINotification n; n.type = NotificationType::Info; n.title = t; n.message = m; return push(n); }
	std::uint32_t pushSuccess(const std::string& t, const std::string& m) { UINotification n; n.type = NotificationType::Success; n.title = t; n.message = m; return push(n); }
	std::uint32_t pushWarning(const std::string& t, const std::string& m) { UINotification n; n.type = NotificationType::Warning; n.title = t; n.message = m; return push(n); }
	std::uint32_t pushError(const std::string& t, const std::string& m) { UINotification n; n.type = NotificationType::Error; n.title = t; n.message = m; return push(n); }

	std::uint32_t pushWithProgress(const std::string& title, std::function<float()> gp)
	{
		UINotification n; n.type = NotificationType::Info; n.title = title; n.duration = -1.0f; n.getProgress = std::move(gp);
		return push(n);
	}

	void dismiss(std::uint32_t id)
	{
		for (auto& a : m_active) {
			if (a.id == id && a.phase != Phase::SlideOut && a.phase != Phase::Dismissed) { a.phase = Phase::SlideOut; a.phaseT = 0; a.anim = 0; break; }
		}
	}

	void dismissAll()
	{
		for (auto& a : m_active) { if (a.phase != Phase::SlideOut && a.phase != Phase::Dismissed) { a.phase = Phase::SlideOut; a.phaseT = 0; a.anim = 0; } }
		m_pending.clear();
	}

	void setNotificationHeight(std::uint32_t id, float h) { for (auto& a : m_active) { if (a.id == id) { a.height = std::max(h, m_cfg.minHeight); return; } } }

	[[nodiscard]] std::size_t visibleCount() const noexcept { return visCnt(); }
	[[nodiscard]] std::size_t pendingCount() const noexcept { return m_pending.size(); }
	[[nodiscard]] std::size_t totalCount() const noexcept { return m_active.size() + m_pending.size(); }

	[[nodiscard]] std::vector<NotificationRenderInfo> getVisible() const
	{
		std::vector<NotificationRenderInfo> r;
		float yOff = 0;
		for (const auto& a : m_active) {
			if (a.phase == Phase::Dismissed) { continue; }
			NotificationRenderInfo i;
			i.id = a.id; i.type = a.notif.type; i.title = a.notif.title; i.message = a.notif.message; i.icon = a.notif.icon;
			i.bounds = calcBounds(yOff, a.height);
			if (a.phase == Phase::SlideIn) { i.opacity = a.anim; i.slideOffset = m_cfg.width * (1.f - a.anim); }
			else if (a.phase == Phase::Visible) { i.opacity = 1; }
			else if (a.phase == Phase::SlideOut) { i.opacity = 1.f - a.anim; i.slideOffset = m_cfg.width * a.anim; }
			if (a.notif.getProgress) { i.progress = a.notif.getProgress(); }
			for (const auto& act : a.notif.actions) { i.actionLabels.push_back(act.label); }
			r.push_back(std::move(i));
			yOff += a.height + m_cfg.spacing;
		}
		return r;
	}

	void update(float dt)
	{
		bool cleanup = false;
		for (auto& a : m_active) {
			a.elapsed += dt; a.phaseT += dt;
			switch (a.phase) {
			case Phase::SlideIn:
				a.anim = m_cfg.slideInDuration > 0 ? std::clamp(a.phaseT / m_cfg.slideInDuration, 0.f, 1.f) : 1.f;
				if (a.anim >= 1.f) { a.phase = Phase::Visible; a.phaseT = 0; a.anim = 1; } break;
			case Phase::Visible:
				if (a.notif.duration >= 0 && a.elapsed >= a.notif.duration) { a.phase = Phase::SlideOut; a.phaseT = 0; a.anim = 0; } break;
			case Phase::SlideOut:
				a.anim = m_cfg.slideOutDuration > 0 ? std::clamp(a.phaseT / m_cfg.slideOutDuration, 0.f, 1.f) : 1.f;
				if (a.anim >= 1.f) { a.phase = Phase::Dismissed; cleanup = true; if (m_onDismissed) m_onDismissed(a.id); } break;
			case Phase::Dismissed: break;
			}
		}
		if (cleanup) {
			m_active.erase(std::remove_if(m_active.begin(), m_active.end(),
				[](const Active& a) { return a.phase == Phase::Dismissed; }), m_active.end());
			while (!m_pending.empty() && visCnt() < static_cast<std::size_t>(m_cfg.maxVisible)) {
				activate(m_nextId++, m_pending.front()); m_pending.erase(m_pending.begin());
			}
		}
	}

private:
	[[nodiscard]] std::size_t visCnt() const noexcept
	{
		std::size_t c = 0; for (const auto& a : m_active) { if (a.phase != Phase::Dismissed) ++c; } return c;
	}

	void activate(std::uint32_t id, const UINotification& n) { m_active.push_back({id, n, Phase::SlideIn, 0, 0, 0, m_cfg.minHeight}); }

	[[nodiscard]] sgc::Rectf calcBounds(float yOff, float h) const noexcept
	{
		const bool isR = (m_cfg.position == NotificationPosition::TopRight || m_cfg.position == NotificationPosition::BottomRight);
		const bool isC = (m_cfg.position == NotificationPosition::TopCenter || m_cfg.position == NotificationPosition::BottomCenter);
		const bool isB = (m_cfg.position == NotificationPosition::BottomLeft || m_cfg.position == NotificationPosition::BottomRight || m_cfg.position == NotificationPosition::BottomCenter);
		float x = isC ? (m_cfg.screenWidth - m_cfg.width) * 0.5f : (isR ? m_cfg.screenWidth - m_cfg.width - m_cfg.marginX : m_cfg.marginX);
		float y = isB ? m_cfg.screenHeight - m_cfg.marginY - h - yOff : m_cfg.marginY + yOff;
		return {x, y, m_cfg.width, h};
	}
};

} // namespace mitiru::ui
