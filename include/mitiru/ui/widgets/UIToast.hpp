#pragma once

/// @file UIToast.hpp
/// @brief Non-blocking toast notification system with stacking, auto-dismiss, and slide animation.

#include <mitiru/ui/UINode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mitiru::ui {

/// @brief Screen anchor position for the toast stack.
enum class ToastPosition : std::uint8_t
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
	TopCenter,
	BottomCenter
};

/// @brief Stack growth direction.
enum class ToastStackDirection : std::uint8_t
{
	Up,   ///< New toasts push older ones upward.
	Down  ///< New toasts push older ones downward.
};

/// @brief Toast notification severity type.
enum class ToastType : std::uint8_t
{
	Info,
	Success,
	Warning,
	Error
};

/// @brief Slide animation direction for toast entry/exit.
enum class ToastSlideDirection : std::uint8_t
{
	Left,
	Right,
	Up,
	Down
};

/// @brief Per-type style configuration for toasts.
struct UIToastTypeStyle
{
	std::string backgroundImageKey;   ///< Background image for this toast type.
	std::string iconImageKey;         ///< Icon image for this toast type.
};

/// @brief Configuration for the toast notification manager.
struct UIToastConfig
{
	UINodeId id = INVALID_UI_NODE;
	std::string name;

	// ── Layout ────────────────────────────────────────────────
	ToastPosition position = ToastPosition::TopRight;
	ToastStackDirection stackDirection = ToastStackDirection::Down;
	int maxVisible = 5;                ///< Maximum toasts visible at once.
	float spacing = 8.0f;             ///< Vertical spacing between toasts.
	float width = 320.0f;             ///< Toast width.
	float minHeight = 48.0f;          ///< Minimum toast height.
	float padding = 12.0f;            ///< Inner padding.
	float iconSize = 24.0f;           ///< Icon size.
	float marginX = 16.0f;            ///< Margin from screen edge X.
	float marginY = 16.0f;            ///< Margin from screen edge Y.

	// ── Timing ────────────────────────────────────────────────
	float defaultDuration = 4.0f;     ///< Default auto-dismiss duration in seconds.
	float slideInDuration = 0.25f;    ///< Slide-in animation duration.
	float slideOutDuration = 0.2f;    ///< Slide-out animation duration.
	ToastSlideDirection slideInDirection = ToastSlideDirection::Right;
	ToastSlideDirection slideOutDirection = ToastSlideDirection::Right;
	float slideDistance = 0.0f;       ///< Slide distance (0 = auto, uses toast width).

	// ── Screen bounds ─────────────────────────────────────────
	float screenWidth = 1920.0f;
	float screenHeight = 1080.0f;

	// ── Image keys ────────────────────────────────────────────
	std::string backgroundImageKey;     ///< Default background image.
	std::string closeButtonImageKey;    ///< Close button image.
	std::string iconImageKey;           ///< Default icon image.

	// ── Per-type styles ───────────────────────────────────────
	UIToastTypeStyle infoStyle;         ///< Style overrides for Info toasts.
	UIToastTypeStyle successStyle;      ///< Style overrides for Success toasts.
	UIToastTypeStyle warningStyle;      ///< Style overrides for Warning toasts.
	UIToastTypeStyle errorStyle;        ///< Style overrides for Error toasts.
};

/// @brief A single toast notification entry.
struct UIToastEntry
{
	std::uint32_t id = 0;            ///< Unique toast identifier.
	std::string text;                ///< Display text.
	std::string iconImageKey;        ///< Icon image key (overrides type default).
	float duration = 4.0f;           ///< Auto-dismiss duration in seconds.
	ToastType type = ToastType::Info;
	float timestamp = 0.0f;          ///< Time when the toast was created.
};

/// @brief Non-blocking toast notification manager.
///
/// Manages a stack of toast notifications with auto-dismiss, slide animations,
/// per-type styling, and configurable positioning. Rendering is handled externally
/// by UIRenderer.
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
	/// @brief Animation phase for individual toasts.
	enum class ToastPhase : std::uint8_t
	{
		SlidingIn,
		Visible,
		SlidingOut,
		Dismissed
	};

	/// @brief Internal runtime state of a single toast.
	struct ActiveToast
	{
		UIToastEntry entry;
		ToastPhase phase = ToastPhase::SlidingIn;
		float phaseTimer = 0.0f;        ///< Time spent in current phase.
		float elapsed = 0.0f;           ///< Total time since creation.
		float animationProgress = 0.0f; ///< 0..1 animation factor.
		float height = 0.0f;            ///< Measured height (set by renderer).
		std::shared_ptr<UINode> node;
	};

	std::shared_ptr<UINode> m_rootNode;
	std::vector<ActiveToast> m_toasts;

	// ── Config copies ─────────────────────────────────────────
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

	// ── Runtime state ─────────────────────────────────────────
	std::uint32_t m_nextId = 1;
	float m_totalTime = 0.0f;

	// ── Callbacks ─────────────────────────────────────────────
	std::function<void(std::uint32_t)> m_onDismissed;
	std::function<void(std::uint32_t)> m_onClicked;

public:
	/// @brief Construct a toast manager from configuration.
	/// @param config Toast configuration.
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

	/// @brief Get the underlying root UINode.
	[[nodiscard]] std::shared_ptr<UINode> node() const noexcept { return m_rootNode; }

	/// @brief Get the number of currently active toasts (including animating ones).
	[[nodiscard]] std::size_t activeCount() const noexcept { return m_toasts.size(); }

	/// @brief Get the number of visible toasts (not yet dismissed).
	[[nodiscard]] std::size_t visibleCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& t : m_toasts)
		{
			if (t.phase != ToastPhase::Dismissed) { ++count; }
		}
		return count;
	}

	/// @brief Check if there are any active toasts.
	[[nodiscard]] bool hasToasts() const noexcept { return !m_toasts.empty(); }

	/// @brief Get the background image key.
	[[nodiscard]] const std::string& backgroundImageKey() const noexcept { return m_backgroundImageKey; }

	/// @brief Get the close button image key.
	[[nodiscard]] const std::string& closeButtonImageKey() const noexcept { return m_closeButtonImageKey; }

	/// @brief Get toast info for rendering.
	struct ToastRenderInfo
	{
		std::uint32_t id = 0;
		std::string text;
		ToastType type = ToastType::Info;
		sgc::Rectf bounds;
		float opacity = 1.0f;
		float slideOffset = 0.0f;   ///< Offset for slide animation (pixels).
		std::string backgroundImageKey;
		std::string iconImageKey;
		std::string closeButtonImageKey;
	};

	/// @brief Get render info for all visible toasts.
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

			// Determine opacity and offset from animation.
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

			// Resolve per-type images.
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

	/// @brief Set the callback invoked when a toast is dismissed.
	void setOnDismissed(std::function<void(std::uint32_t)> callback)
	{
		m_onDismissed = std::move(callback);
	}

	/// @brief Set the callback invoked when a toast is clicked.
	void setOnClicked(std::function<void(std::uint32_t)> callback)
	{
		m_onClicked = std::move(callback);
	}

	/// @brief Set the measured height for a toast (called by layout/renderer).
	/// @param toastId Toast identifier.
	/// @param height Measured height.
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

	/// @brief Set screen bounds.
	void setScreenBounds(float width, float height) noexcept
	{
		m_screenWidth = width;
		m_screenHeight = height;
		recalculatePositions();
	}

	// ── Actions ───────────────────────────────────────────────

	/// @brief Show a new toast notification.
	/// @param text Display text.
	/// @param type Toast type for styling.
	/// @param duration Duration in seconds (0 = use default, <0 = never auto-dismiss).
	/// @return Toast identifier for programmatic dismissal.
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

	/// @brief Show a toast with a custom icon.
	/// @param text Display text.
	/// @param iconImageKey Custom icon image key.
	/// @param type Toast type.
	/// @param duration Duration in seconds.
	/// @return Toast identifier.
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

	/// @brief Dismiss a specific toast by ID.
	/// @param toastId Toast identifier.
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

	/// @brief Dismiss all active toasts.
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

	/// @brief Handle click at the given position (checks close buttons).
	/// @param mouseX Mouse X.
	/// @param mouseY Mouse Y.
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

	/// @brief Advance all toast animations and auto-dismiss timers.
	/// @param dt Delta time in seconds.
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
				// Auto-dismiss check (negative duration = never auto-dismiss).
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

		// Remove dismissed toasts.
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
		// Evict oldest if at capacity.
		while (visibleCount() >= static_cast<std::size_t>(m_maxVisible) && !m_toasts.empty())
		{
			// Dismiss the oldest visible toast.
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
			break; // Only evict one at a time.
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

	/// @brief Recalculate toast positions based on stack configuration.
	void recalculatePositions()
	{
		float anchorX = 0.0f;
		float anchorY = 0.0f;

		// Determine anchor corner.
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

	/// @brief Get the effective slide distance.
	[[nodiscard]] float effectiveSlideDistance() const noexcept
	{
		return m_slideDistance > 0.0f ? m_slideDistance : m_width;
	}

	/// @brief Get the style for a toast type.
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

	/// @brief Convert toast type to string.
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
