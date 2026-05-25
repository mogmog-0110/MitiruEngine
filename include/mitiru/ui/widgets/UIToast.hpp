#pragma once

/// @file UIToast.hpp
/// @brief 非ブロッキングな toast 通知システム。stacking / 自動消去 / slide animation 対応。

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief toast stack の screen anchor 位置。
enum class ToastPosition : std::uint8_t
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	TopCenter,
	BottomCenter
};

/// @brief stack の伸長方向。
enum class ToastStackDirection : std::uint8_t
{
	Up,   ///< 新しい toast が古いものを上へ押す。
	Down  ///< 新しい toast が古いものを下へ押す。
};

/// @brief toast 通知の severity 種別。
enum class ToastType : std::uint8_t
{
	Info,
	Success,
	Warning,
	Error
};

/// @brief toast の入出時 slide animation 方向。
enum class ToastSlideDirection : std::uint8_t
{
	Left,
	Right,
	Up,
	Down
};

/// @brief toast の種別ごとの style 設定。
struct UIToastTypeStyle
{
	std::string backgroundImageKey;   ///< この toast 種別の背景画像。
	std::string iconImageKey;         ///< この toast 種別の icon 画像。
};

/// @brief toast 通知 manager の設定。
struct UIToastConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;

	// ── Layout ────────────────────────────────────────────────
	ToastPosition position = ToastPosition::TopRight;
	ToastStackDirection stackDirection = ToastStackDirection::Down;
	int maxVisible = 5;                ///< 同時に表示する toast の最大数。
	float spacing = 8.0f;             ///< toast 間の縦方向 spacing。
	float width = 320.0f;             ///< toast の幅。
	float minHeight = 48.0f;          ///< toast の最小高さ。
	float padding = 12.0f;            ///< 内側 padding。
	float iconSize = 24.0f;           ///< icon のサイズ。
	float marginX = 16.0f;            ///< 画面端からの X margin。
	float marginY = 16.0f;            ///< 画面端からの Y margin。

	// ── Timing ────────────────────────────────────────────────
	float defaultDuration = 4.0f;     ///< 自動消去までの既定時間 (秒)。
	float slideInDuration = 0.25f;    ///< slide-in animation の時間。
	float slideOutDuration = 0.2f;    ///< slide-out animation の時間。
	ToastSlideDirection slideInDirection = ToastSlideDirection::Right;
	ToastSlideDirection slideOutDirection = ToastSlideDirection::Right;
	float slideDistance = 0.0f;       ///< slide 距離 (0 = auto、toast 幅を使う)。

	// ── 画面 bounds ───────────────────────────────────────────
	float screenWidth = 1920.0f;
	float screenHeight = 1080.0f;

	// ── 画像 key 群 ───────────────────────────────────────────
	std::string backgroundImageKey;     ///< 既定の背景画像。
	std::string closeButtonImageKey;    ///< close button の画像。
	std::string iconImageKey;           ///< 既定の icon 画像。

	// ── 種別ごとの style ──────────────────────────────────────
	UIToastTypeStyle infoStyle;         ///< Info toast の style 上書き。
	UIToastTypeStyle successStyle;      ///< Success toast の style 上書き。
	UIToastTypeStyle warningStyle;      ///< Warning toast の style 上書き。
	UIToastTypeStyle errorStyle;        ///< Error toast の style 上書き。
};

/// @brief 単一の toast 通知 entry。
struct UIToastEntry
{
	std::uint32_t id = 0;            ///< toast 固有の識別子。
	std::string text;                ///< 表示テキスト。
	std::string iconImageKey;        ///< icon 画像 key (種別の既定を上書き)。
	float duration = 4.0f;           ///< 自動消去までの時間 (秒)。
	ToastType type = ToastType::Info;
	float timestamp = 0.0f;          ///< toast 生成時刻。
};

/// @brief 非ブロッキングな toast 通知 manager。
///
/// 自動消去 / slide animation / 種別ごとの styling / 配置設定を持つ toast 通知の
/// stack を管理する。描画は外部の UIRenderer が担当する。
///
/// @code
///   UIToastConfig cfg;
///   cfg.id = 400;
///   cfg.position = ToastPosition::BottomRight;
///   cfg.maxVisible = 3;
///   cfg.defaultDuration = 5.0f;
///   cfg.errorStyle.backgroundImageKey = "toast_error_bg";
///   UIToastManager toasts(cfg);
///
///   toasts.show("File saved successfully", ToastType::Success);
///   toasts.show("Connection lost", ToastType::Error, 8.0f);
///   toasts.update(deltaTime);
/// @endcode
class UIToastManager
{
	/// @brief 個々の toast の animation phase。
	enum class ToastPhase : std::uint8_t
	{
		SlidingIn,
		Visible,
		SlidingOut,
		Dismissed
	};

	/// @brief 単一 toast の内部 runtime state。
	struct ActiveToast
	{
		UIToastEntry entry;
		ToastPhase phase = ToastPhase::SlidingIn;
		float phaseTimer = 0.0f;        ///< 現在の phase での経過時間。
		float elapsed = 0.0f;           ///< 生成からの総経過時間。
		float animationProgress = 0.0f; ///< 0..1 の animation 係数。
		float height = 0.0f;            ///< 測定された高さ (renderer が設定)。
		std::shared_ptr<UINode> node;
	};

	std::shared_ptr<UINode> m_rootNode;
	std::vector<ActiveToast> m_toasts;

	// ── config の複製 ─────────────────────────────────────────
	ToastPosition m_position;
	ToastStackDirection m_stackDirection;
	int m_maxVisible;
	float m_spacing;
	float m_width;
	float m_minHeight;
	float m_padding;
	float m_iconSize;
	float m_marginX;
	float m_marginY;
	float m_defaultDuration;
	float m_slideInDuration;
	float m_slideOutDuration;
	ToastSlideDirection m_slideInDirection;
	ToastSlideDirection m_slideOutDirection;
	float m_slideDistance;
	float m_screenWidth;
	float m_screenHeight;
	std::string m_backgroundImageKey;
	std::string m_closeButtonImageKey;
	std::string m_iconImageKey;
	UIToastTypeStyle m_infoStyle;
	UIToastTypeStyle m_successStyle;
	UIToastTypeStyle m_warningStyle;
	UIToastTypeStyle m_errorStyle;

	// ── runtime state ─────────────────────────────────────────
	std::uint32_t m_nextId = 1;
	float m_totalTime = 0.0f;

	// ── Callbacks ─────────────────────────────────────────────
	std::function<void(std::uint32_t)> m_onDismissed;
	std::function<void(std::uint32_t)> m_onClicked;

public:
	/// @brief 設定から toast manager を構築する。
	/// @param config toast 設定。
	explicit UIToastManager(const UIToastConfig& config)
		: m_position(config.position)
		, m_stackDirection(config.stackDirection)
		, m_maxVisible(config.maxVisible)
		, m_spacing(config.spacing)
		, m_width(config.width)
		, m_minHeight(config.minHeight)
		, m_padding(config.padding)
		, m_iconSize(config.iconSize)
		, m_marginX(config.marginX)
		, m_marginY(config.marginY)
		, m_defaultDuration(config.defaultDuration)
		, m_slideInDuration(config.slideInDuration)
		, m_slideOutDuration(config.slideOutDuration)
		, m_slideInDirection(config.slideInDirection)
		, m_slideOutDirection(config.slideOutDirection)
		, m_slideDistance(config.slideDistance)
		, m_screenWidth(config.screenWidth)
		, m_screenHeight(config.screenHeight)
		, m_backgroundImageKey(config.backgroundImageKey)
		, m_closeButtonImageKey(config.closeButtonImageKey)
		, m_iconImageKey(config.iconImageKey)
		, m_infoStyle(config.infoStyle)
		, m_successStyle(config.successStyle)
		, m_warningStyle(config.warningStyle)
		, m_errorStyle(config.errorStyle)
	{
		UINodeData data;
		data.id = config.id;
		data.name = config.name;
		data.role = UIRole::Container;
		data.properties["widget_type"] = "toast_manager";
		data.properties["background_image"] = config.backgroundImageKey;
		data.properties["close_button_image"] = config.closeButtonImageKey;

		m_rootNode = std::make_shared<UINode>(std::move(data));
		syncNodeState();
	}

	// ── Accessors ─────────────────────────────────────────────

	/// @brief 内部の root UINode を取得する。
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_rootNode; }

	/// @brief 現在 active な toast 数 (animation 中も含む) を取得する。
	[[nodiscard]] std::size_t activeCount() const noexcept { return m_toasts.size(); }

	/// @brief 表示中 (未消去) の toast 数を取得する。
	[[nodiscard]] std::size_t visibleCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& t : m_toasts)
		{
			if (t.phase != ToastPhase::Dismissed) { ++count; }
		}
		return count;
	}

	/// @brief active な toast が存在するか確認する。
	[[nodiscard]] bool hasToasts() const noexcept { return !m_toasts.empty(); }

	/// @brief 背景画像 key を取得する。
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief close button の画像 key を取得する。
	[[nodiscard]] const std::string& closeButtonImageKey() const noexcept { return m_closeButtonImageKey; }

	/// @brief 描画用の toast 情報を取得する。
	struct ToastRenderInfo
	{
		std::uint32_t id = 0;
		std::string text;
		ToastType type = ToastType::Info;
		sgc::Rectf bounds;
		float opacity = 1.0f;
		float slideOffset = 0.0f;   ///< slide animation 用の offset (pixel)。
		std::string backgroundImageKey;
		std::string iconImageKey;
		std::string closeButtonImageKey;
	};

	/// @brief 表示中の全 toast の描画情報を取得する。
	[[nodiscard]] std::vector<ToastRenderInfo> renderInfo() const
	{
		std::vector<ToastRenderInfo> result;
		result.reserve(m_toasts.size());

		for (const auto& toast : m_toasts)
		{
			if (toast.phase == ToastPhase::Dismissed) { continue; }

			ToastRenderInfo info;
			info.id = toast.entry.id;
			info.text = toast.entry.text;
			info.type = toast.entry.type;
			info.bounds = toast.node ? toast.node->bounds() : sgc::Rectf{};
			info.closeButtonImageKey = m_closeButtonImageKey;

			// animation から opacity と offset を決める。
			const float slideDistActual = effectiveSlideDistance();

			switch (toast.phase)
			{
			case ToastPhase::SlidingIn:
				info.opacity = toast.animationProgress;
				info.slideOffset = slideDistActual * (1.0f - toast.animationProgress);
				break;
			case ToastPhase::Visible:
				info.opacity = 1.0f;
				info.slideOffset = 0.0f;
				break;
			case ToastPhase::SlidingOut:
				info.opacity = 1.0f - toast.animationProgress;
				info.slideOffset = slideDistActual * toast.animationProgress;
				break;
			default:
				break;
			}

			// 種別ごとの画像を解決する。
			const auto& typeStyle = styleForType(toast.entry.type);
			info.backgroundImageKey = typeStyle.backgroundImageKey.empty()
				? m_backgroundImageKey : typeStyle.backgroundImageKey;
			info.iconImageKey = !toast.entry.iconImageKey.empty()
				? toast.entry.iconImageKey
				: (typeStyle.iconImageKey.empty() ? m_iconImageKey : typeStyle.iconImageKey);

			result.push_back(std::move(info));
		}
		return result;
	}

	// ── Configuration ─────────────────────────────────────────

	/// @brief toast が消去されたときに呼ばれる callback を設定する。
	void setOnDismissed(std::function<void(std::uint32_t)> callback)
	{
		m_onDismissed = std::move(callback);
	}

	/// @brief toast が click されたときに呼ばれる callback を設定する。
	void setOnClicked(std::function<void(std::uint32_t)> callback)
	{
		m_onClicked = std::move(callback);
	}

	/// @brief toast の測定済み高さを設定する (layout/renderer が呼ぶ)。
	/// @param toastId toast の識別子。
	/// @param height 測定された高さ。
	void setToastHeight(std::uint32_t toastId, float height)
	{
		for (auto& toast : m_toasts)
		{
			if (toast.entry.id == toastId)
			{
				toast.height = std::max(height, m_minHeight);
				recalculatePositions();
				return;
			}
		}
	}

	/// @brief screen の bounds を設定する。
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
		recalculatePositions();
	}

	// ── Actions ───────────────────────────────────────────────

	/// @brief 新しい toast 通知を表示する。
	/// @param text 表示テキスト。
	/// @param type styling 用の toast 種別。
	/// @param duration 表示時間 (秒) (0 = 既定値を使う、<0 = 自動消去しない)。
	/// @return プログラムからの消去に使う toast 識別子。
	std::uint32_t show(const std::string& text, ToastType type = ToastType::Info, float duration = 0.0f)
	{
		UIToastEntry entry;
		entry.id = m_nextId++;
		entry.text = text;
		entry.type = type;
		entry.duration = (duration > 0.0f) ? duration : m_defaultDuration;
		entry.timestamp = m_totalTime;

		return showEntry(entry);
	}

	/// @brief custom icon 付きの toast を表示する。
	/// @param text 表示テキスト。
	/// @param iconImageKey custom icon 画像 key。
	/// @param type toast 種別。
	/// @param duration 表示時間 (秒)。
	/// @return toast 識別子。
	std::uint32_t show(const std::string& text, const std::string& iconImageKey,
					   ToastType type = ToastType::Info, float duration = 0.0f)
	{
		UIToastEntry entry;
		entry.id = m_nextId++;
		entry.text = text;
		entry.iconImageKey = iconImageKey;
		entry.type = type;
		entry.duration = (duration > 0.0f) ? duration : m_defaultDuration;
		entry.timestamp = m_totalTime;

		return showEntry(entry);
	}

	/// @brief ID 指定で特定の toast を消去する。
	/// @param toastId toast の識別子。
	void dismiss(std::uint32_t toastId)
	{
		for (auto& toast : m_toasts)
		{
			if (toast.entry.id == toastId && toast.phase != ToastPhase::SlidingOut
				&& toast.phase != ToastPhase::Dismissed)
			{
				toast.phase = ToastPhase::SlidingOut;
				toast.phaseTimer = 0.0f;
				toast.animationProgress = 0.0f;
				break;
			}
		}
	}

	/// @brief active な全 toast を消去する。
	void dismissAll()
	{
		for (auto& toast : m_toasts)
		{
			if (toast.phase != ToastPhase::SlidingOut && toast.phase != ToastPhase::Dismissed)
			{
				toast.phase = ToastPhase::SlidingOut;
				toast.phaseTimer = 0.0f;
				toast.animationProgress = 0.0f;
			}
		}
	}

	/// @brief 指定位置での click を処理する (close button を判定)。
	/// @param mouseX mouse の X。
	/// @param mouseY mouse の Y。
	void onMouseClick(float mouseX, float mouseY)
	{
		for (auto& toast : m_toasts)
		{
			if (toast.phase == ToastPhase::Dismissed) { continue; }
			if (!toast.node) { continue; }

			const auto bounds = toast.node->bounds();
			if (mouseX >= bounds.x && mouseX < bounds.x + bounds.w
				&& mouseY >= bounds.y && mouseY < bounds.y + bounds.h)
			{
				if (m_onClicked) { m_onClicked(toast.entry.id); }
				dismiss(toast.entry.id);
				return;
			}
		}
	}

	// ── Update ────────────────────────────────────────────────

	/// @brief 全 toast の animation と自動消去 timer を進める。
	/// @param dt delta time (秒)。
	void update(float dt)
	{
		m_totalTime += dt;
		bool needsLayout = false;

		for (auto& toast : m_toasts)
		{
			toast.elapsed += dt;
			toast.phaseTimer += dt;

			switch (toast.phase)
			{
			case ToastPhase::SlidingIn:
				if (m_slideInDuration > 0.0f)
				{
					toast.animationProgress = std::clamp(toast.phaseTimer / m_slideInDuration, 0.0f, 1.0f);
				}
				else
				{
					toast.animationProgress = 1.0f;
				}
				if (toast.animationProgress >= 1.0f)
				{
					toast.phase = ToastPhase::Visible;
					toast.phaseTimer = 0.0f;
					toast.animationProgress = 1.0f;
				}
				break;

			case ToastPhase::Visible:
				// 自動消去判定 (duration が負 = 自動消去しない)。
				if (toast.entry.duration >= 0.0f && toast.elapsed >= toast.entry.duration)
				{
					toast.phase = ToastPhase::SlidingOut;
					toast.phaseTimer = 0.0f;
					toast.animationProgress = 0.0f;
				}
				break;

			case ToastPhase::SlidingOut:
				if (m_slideOutDuration > 0.0f)
				{
					toast.animationProgress = std::clamp(toast.phaseTimer / m_slideOutDuration, 0.0f, 1.0f);
				}
				else
				{
					toast.animationProgress = 1.0f;
				}
				if (toast.animationProgress >= 1.0f)
				{
					toast.phase = ToastPhase::Dismissed;
					needsLayout = true;
					if (m_onDismissed) { m_onDismissed(toast.entry.id); }
				}
				break;

			case ToastPhase::Dismissed:
				break;
			}
		}

		// 消去済みの toast を除去する。
		const auto removed = std::remove_if(m_toasts.begin(), m_toasts.end(),
			[](const ActiveToast& t) { return t.phase == ToastPhase::Dismissed; });
		if (removed != m_toasts.end())
		{
			m_toasts.erase(removed, m_toasts.end());
			needsLayout = true;
		}

		if (needsLayout)
		{
			recalculatePositions();
		}

		syncNodeState();
	}

private:
	std::uint32_t showEntry(const UIToastEntry& entry)
	{
		// 上限に達していたら最古を追い出す。
		while (visibleCount() >= static_cast<std::size_t>(m_maxVisible) && !m_toasts.empty())
		{
			// 表示中で最古の toast を消去する。
			for (auto& t : m_toasts)
			{
				if (t.phase != ToastPhase::SlidingOut && t.phase != ToastPhase::Dismissed)
				{
					t.phase = ToastPhase::SlidingOut;
					t.phaseTimer = 0.0f;
					t.animationProgress = 0.0f;
					break;
				}
			}
			break; // 一度に 1 個だけ追い出す。
		}

		ActiveToast active;
		active.entry = entry;
		active.phase = ToastPhase::SlidingIn;
		active.height = m_minHeight;

		UINodeData nodeData;
		nodeData.id = INVALID_UI_NODE;
		nodeData.name = "toast_" + std::to_string(entry.id);
		nodeData.role = UIRole::Container;
		nodeData.text = entry.text;
		nodeData.properties["widget_type"] = "toast";
		nodeData.properties["toast_type"] = typeToString(entry.type);

		const auto& typeStyle = styleForType(entry.type);
		nodeData.properties["background_image"] = typeStyle.backgroundImageKey.empty()
			? m_backgroundImageKey : typeStyle.backgroundImageKey;
		nodeData.properties["icon_image"] = !entry.iconImageKey.empty()
			? entry.iconImageKey
			: (typeStyle.iconImageKey.empty() ? m_iconImageKey : typeStyle.iconImageKey);
		nodeData.properties["close_button_image"] = m_closeButtonImageKey;

		active.node = std::make_shared<UINode>(std::move(nodeData));
		m_toasts.push_back(std::move(active));

		recalculatePositions();
		syncNodeState();

		return entry.id;
	}

	/// @brief stack 設定に基づき toast の位置を再計算する。
	void recalculatePositions()
	{
		float anchorX = 0.0f;
		float anchorY = 0.0f;

		// anchor となる corner を決める。
		switch (m_position)
		{
		case ToastPosition::TopLeft:
			anchorX = m_marginX;
			anchorY = m_marginY;
			break;
		case ToastPosition::TopRight:
			anchorX = m_screenWidth - m_width - m_marginX;
			anchorY = m_marginY;
			break;
		case ToastPosition::BottomLeft:
			anchorX = m_marginX;
			anchorY = m_screenHeight - m_marginY;
			break;
		case ToastPosition::BottomRight:
			anchorX = m_screenWidth - m_width - m_marginX;
			anchorY = m_screenHeight - m_marginY;
			break;
		case ToastPosition::TopCenter:
			anchorX = (m_screenWidth - m_width) * 0.5f;
			anchorY = m_marginY;
			break;
		case ToastPosition::BottomCenter:
			anchorX = (m_screenWidth - m_width) * 0.5f;
			anchorY = m_screenHeight - m_marginY;
			break;
		}

		const bool growsDown = (m_stackDirection == ToastStackDirection::Down);
		const bool anchorIsBottom = (m_position == ToastPosition::BottomLeft
			|| m_position == ToastPosition::BottomRight
			|| m_position == ToastPosition::BottomCenter);

		float yOffset = 0.0f;

		for (auto& toast : m_toasts)
		{
			if (toast.phase == ToastPhase::Dismissed || !toast.node) { continue; }

			float y = 0.0f;
			if (anchorIsBottom)
			{
				if (growsDown)
				{
					y = anchorY - toast.height - yOffset;
				}
				else
				{
					y = anchorY - toast.height - yOffset;
				}
			}
			else
			{
				y = anchorY + yOffset;
			}

			toast.node->setBounds(sgc::Rectf(anchorX, y, m_width, toast.height));
			yOffset += toast.height + m_spacing;
		}
	}

	/// @brief 実効的な slide 距離を取得する。
	[[nodiscard]] float effectiveSlideDistance() const noexcept
	{
		return m_slideDistance > 0.0f ? m_slideDistance : m_width;
	}

	/// @brief toast 種別に対応する style を取得する。
	[[nodiscard]] const UIToastTypeStyle& styleForType(ToastType type) const noexcept
	{
		switch (type)
		{
		case ToastType::Info:    return m_infoStyle;
		case ToastType::Success: return m_successStyle;
		case ToastType::Warning: return m_warningStyle;
		case ToastType::Error:   return m_errorStyle;
		}
		return m_infoStyle;
	}

	/// @brief toast 種別を文字列に変換する。
	[[nodiscard]] static const char* typeToString(ToastType type) noexcept
	{
		switch (type)
		{
		case ToastType::Info:    return "info";
		case ToastType::Success: return "success";
		case ToastType::Warning: return "warning";
		case ToastType::Error:   return "error";
		}
		return "info";
	}

	void syncNodeState()
	{
		m_rootNode->setProperty("active_count", std::to_string(m_toasts.size()));
		m_rootNode->setProperty("background_image", m_backgroundImageKey);
		m_rootNode->setProperty("close_button_image", m_closeButtonImageKey);

		const char* posStr = "top_right";
		switch (m_position)
		{
		case ToastPosition::TopLeft:      posStr = "top_left";      break;
		case ToastPosition::TopRight:     posStr = "top_right";     break;
		case ToastPosition::BottomLeft:   posStr = "bottom_left";   break;
		case ToastPosition::BottomRight:  posStr = "bottom_right";  break;
		case ToastPosition::TopCenter:    posStr = "top_center";    break;
		case ToastPosition::BottomCenter: posStr = "bottom_center"; break;
		}
		m_rootNode->setProperty("position", posStr);
	}
};

} // namespace mitiru::ui
