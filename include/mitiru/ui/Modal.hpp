#pragma once

/// @file Modal.hpp
/// @brief General-purpose modal dialog system for the UI framework.
/// @details Provides a flexible modal dialog with configurable buttons,
///          backdrop, animations, keyboard navigation, and a modal stack
///          manager for layered dialogs. Unlike vn::ConfirmDialog which
///          is limited to Yes/No/Cancel presets, this system supports
///          arbitrary button configurations.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <mitiru/ui/UINode.hpp>
#include <mitiru/ui/UITheme.hpp>
#include <mitiru/ui/Easing.hpp>

namespace mitiru::ui
{

// ════════════════════════════════════════════════════════════════════
//  Modal button
// ════════════════════════════════════════════════════════════════════

/// @brief Style hint for a modal button.
enum class ModalButtonStyle : std::uint8_t
{
	Default,    ///< Standard appearance.
	Primary,    ///< Emphasized / confirm action.
	Danger,     ///< Destructive action (delete, etc.).
	Cancel,     ///< Cancel / dismiss action.
};

/// @brief A single button in a modal dialog.
struct ModalButton
{
	std::string label;                              ///< Button text.
	std::string resultId;                           ///< Identifier returned on click.
	ModalButtonStyle style = ModalButtonStyle::Default; ///< Visual style hint.
	bool focused = false;                           ///< Whether this button has focus.
};

// ════════════════════════════════════════════════════════════════════
//  Modal configuration
// ════════════════════════════════════════════════════════════════════

/// @brief Backdrop (overlay behind modal) configuration.
struct ModalBackdrop
{
	float opacity         = 0.5f;   ///< Backdrop opacity [0, 1].
	bool dismissOnClick   = false;  ///< Click backdrop to dismiss.
};

/// @brief Animation configuration for modal show/hide.
struct ModalAnimation
{
	float fadeInDuration  = 0.2f;   ///< Fade-in time in seconds.
	float fadeOutDuration = 0.15f;  ///< Fade-out time in seconds.
	EasingType showEasing = EasingType::EaseOutBack;  ///< Show easing curve.
	EasingType hideEasing = EasingType::EaseInQuad;   ///< Hide easing curve.
};

/// @brief Configuration for creating a modal dialog.
struct ModalConfig
{
	std::string title;                     ///< Dialog title (empty = no title bar).
	std::string content;                   ///< Body text / message.
	std::vector<ModalButton> buttons;      ///< Button list (left to right).
	ModalBackdrop backdrop;                ///< Backdrop settings.
	ModalAnimation animation;              ///< Animation settings.
	bool escapeToClose    = true;          ///< Allow Escape key to dismiss.
	std::string escapeResultId = "cancel"; ///< Result ID when dismissed by Escape.
	float width           = 400.0f;        ///< Dialog width in pixels.
	float minHeight       = 0.0f;          ///< Minimum dialog height.
};

// ════════════════════════════════════════════════════════════════════
//  Modal state
// ════════════════════════════════════════════════════════════════════

/// @brief Display state of a modal dialog.
enum class ModalState : std::uint8_t
{
	Hidden,     ///< Not visible.
	FadingIn,   ///< Appearing.
	Visible,    ///< Fully visible and interactive.
	FadingOut,  ///< Disappearing.
};

// ════════════════════════════════════════════════════════════════════
//  Modal dialog
// ════════════════════════════════════════════════════════════════════

/// @brief General-purpose modal dialog.
///
/// @code
/// mitiru::ui::ModalConfig cfg;
/// cfg.title = "Delete Save?";
/// cfg.content = "This action cannot be undone.";
/// cfg.buttons = {
///     {"Delete", "delete", mitiru::ui::ModalButtonStyle::Danger},
///     {"Cancel", "cancel", mitiru::ui::ModalButtonStyle::Cancel},
/// };
/// cfg.backdrop.dismissOnClick = true;
///
/// mitiru::ui::Modal dialog;
/// dialog.setOnResult([](const std::string& id) {
///     if (id == "delete") { /* perform delete */ }
/// });
/// dialog.show(cfg);
/// @endcode
class Modal
{
public:
	/// @brief Construct an empty modal.
	Modal() noexcept = default;

	/// @brief Construct with a UITheme.
	/// @param theme Theme for visual styling.
	explicit Modal(const UITheme& theme) noexcept
		: m_theme(theme)
	{
	}

	// ── Show / hide ──────────────────────────────────────────

	/// @brief Show the modal with the given configuration.
	/// @param config Modal parameters.
	void show(ModalConfig config)
	{
		m_config = std::move(config);
		m_state = ModalState::FadingIn;
		m_animProgress = 0.0f;
		m_resultId.clear();
		m_focusIndex = 0;

		// Set initial focus.
		if (!m_config.buttons.empty())
		{
			m_config.buttons[0].focused = true;
		}
	}

	/// @brief Dismiss the modal with a specific result.
	/// @param resultId The result identifier.
	void dismiss(const std::string& resultId)
	{
		if (m_state == ModalState::Hidden || m_state == ModalState::FadingOut)
		{
			return;
		}
		m_resultId = resultId;
		m_state = ModalState::FadingOut;
		m_animProgress = 0.0f;
	}

	/// @brief Immediately hide the modal without animation.
	void hide() noexcept
	{
		m_state = ModalState::Hidden;
		m_animProgress = 0.0f;
		m_resultId.clear();
	}

	// ── Input handling ───────────────────────────────────────

	/// @brief Handle confirm key press (Enter / Space).
	void onConfirmPressed()
	{
		if (m_state != ModalState::Visible || m_config.buttons.empty())
		{
			return;
		}
		const auto& btn = m_config.buttons[static_cast<std::size_t>(m_focusIndex)];
		dismiss(btn.resultId);
	}

	/// @brief Handle Escape key press.
	void onEscapePressed()
	{
		if (m_state != ModalState::Visible || !m_config.escapeToClose)
		{
			return;
		}
		dismiss(m_config.escapeResultId);
	}

	/// @brief Handle backdrop click.
	void onBackdropClicked()
	{
		if (m_state != ModalState::Visible || !m_config.backdrop.dismissOnClick)
		{
			return;
		}
		dismiss(m_config.escapeResultId);
	}

	/// @brief Move focus to the previous button.
	void onFocusPrev() noexcept
	{
		if (m_state != ModalState::Visible || m_config.buttons.empty())
		{
			return;
		}
		m_config.buttons[static_cast<std::size_t>(m_focusIndex)].focused = false;
		m_focusIndex = (m_focusIndex - 1 + static_cast<int>(m_config.buttons.size()))
			% static_cast<int>(m_config.buttons.size());
		m_config.buttons[static_cast<std::size_t>(m_focusIndex)].focused = true;
	}

	/// @brief Move focus to the next button.
	void onFocusNext() noexcept
	{
		if (m_state != ModalState::Visible || m_config.buttons.empty())
		{
			return;
		}
		m_config.buttons[static_cast<std::size_t>(m_focusIndex)].focused = false;
		m_focusIndex = (m_focusIndex + 1)
			% static_cast<int>(m_config.buttons.size());
		m_config.buttons[static_cast<std::size_t>(m_focusIndex)].focused = true;
	}

	/// @brief Handle button click by index.
	/// @param index Button index.
	void onButtonClicked(int index)
	{
		if (m_state != ModalState::Visible) return;
		if (index < 0 || index >= static_cast<int>(m_config.buttons.size())) return;
		dismiss(m_config.buttons[static_cast<std::size_t>(index)].resultId);
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief Update modal animation state.
	/// @param deltaTime Frame delta time in seconds.
	void update(float deltaTime) noexcept
	{
		switch (m_state)
		{
		case ModalState::Hidden:
			break;

		case ModalState::FadingIn:
		{
			const float dur = std::max(m_config.animation.fadeInDuration, 0.001f);
			m_animProgress += deltaTime / dur;
			if (m_animProgress >= 1.0f)
			{
				m_animProgress = 1.0f;
				m_state = ModalState::Visible;
			}
			break;
		}

		case ModalState::Visible:
			break;

		case ModalState::FadingOut:
		{
			const float dur = std::max(m_config.animation.fadeOutDuration, 0.001f);
			m_animProgress += deltaTime / dur;
			if (m_animProgress >= 1.0f)
			{
				m_animProgress = 1.0f;
				m_state = ModalState::Hidden;
				if (m_onResult)
				{
					m_onResult(m_resultId);
				}
			}
			break;
		}
		}
	}

	// ── Callback ─────────────────────────────────────────────

	/// @brief Set the result callback.
	/// @param fn Function called with the resultId when the modal closes.
	void setOnResult(std::function<void(const std::string&)> fn)
	{
		m_onResult = std::move(fn);
	}

	// ── Theme ────────────────────────────────────────────────

	/// @brief Set the UITheme.
	void setTheme(const UITheme& theme) noexcept { m_theme = theme; }

	/// @brief Access the current UITheme.
	[[nodiscard]] const UITheme& theme() const noexcept { return m_theme; }

	// ── State queries ────────────────────────────────────────

	/// @brief Current display state.
	[[nodiscard]] ModalState state() const noexcept { return m_state; }

	/// @brief Whether the modal is visible (not Hidden).
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_state != ModalState::Hidden;
	}

	/// @brief Whether the modal should block input to layers below.
	[[nodiscard]] bool isModal() const noexcept
	{
		return m_state != ModalState::Hidden;
	}

	/// @brief The result identifier from the last dismiss.
	[[nodiscard]] const std::string& resultId() const noexcept { return m_resultId; }

	/// @brief Animation progress [0.0, 1.0].
	[[nodiscard]] float animProgress() const noexcept { return m_animProgress; }

	/// @brief Display scale (eased for show/hide animations).
	[[nodiscard]] float displayScale() const noexcept
	{
		switch (m_state)
		{
		case ModalState::FadingIn:
			return easing::apply(m_config.animation.showEasing, m_animProgress);
		case ModalState::FadingOut:
			return 1.0f - easing::apply(m_config.animation.hideEasing, m_animProgress);
		case ModalState::Visible:
			return 1.0f;
		default:
			return 0.0f;
		}
	}

	/// @brief Display alpha (linear fade).
	[[nodiscard]] float displayAlpha() const noexcept
	{
		switch (m_state)
		{
		case ModalState::FadingIn:
			return m_animProgress;
		case ModalState::FadingOut:
			return 1.0f - m_animProgress;
		case ModalState::Visible:
			return 1.0f;
		default:
			return 0.0f;
		}
	}

	/// @brief Backdrop alpha (accounts for display alpha and backdrop opacity).
	[[nodiscard]] float backdropAlpha() const noexcept
	{
		return displayAlpha() * m_config.backdrop.opacity;
	}

	/// @brief Access the configuration.
	[[nodiscard]] const ModalConfig& config() const noexcept { return m_config; }

	/// @brief Access the button list (with current focus state).
	[[nodiscard]] const std::vector<ModalButton>& buttons() const noexcept
	{
		return m_config.buttons;
	}

	/// @brief Current focus index.
	[[nodiscard]] int focusIndex() const noexcept { return m_focusIndex; }

private:
	ModalConfig m_config;
	ModalState m_state = ModalState::Hidden;
	std::string m_resultId;
	float m_animProgress = 0.0f;
	int m_focusIndex = 0;
	UITheme m_theme;
	std::function<void(const std::string&)> m_onResult;
};

// ════════════════════════════════════════════════════════════════════
//  Modal manager (stack)
// ════════════════════════════════════════════════════════════════════

/// @brief Manages a stack of active modals with input blocking.
/// @details Only the topmost modal receives input. Lower modals remain
///          visible but inactive. When the top modal is dismissed, the
///          next one in the stack becomes active.
///
/// @code
/// mitiru::ui::ModalManager modals;
/// modals.push(alertConfig, [](const std::string& r) { /* ... */ });
/// modals.push(confirmConfig, [](const std::string& r) { /* ... */ });
///
/// // Each frame:
/// modals.update(dt);
///
/// // Input goes to topmost:
/// if (escapePressed) modals.onEscapePressed();
/// @endcode
class ModalManager
{
public:
	/// @brief Push a new modal onto the stack.
	/// @param config Modal configuration.
	/// @param onResult Callback when this modal is dismissed.
	void push(ModalConfig config,
	          std::function<void(const std::string&)> onResult = nullptr)
	{
		auto& entry = m_stack.emplace_back();
		entry.modal.show(std::move(config));
		entry.onResult = std::move(onResult);
	}

	/// @brief Whether any modals are active.
	[[nodiscard]] bool hasActiveModal() const noexcept
	{
		return !m_stack.empty();
	}

	/// @brief Number of active modals.
	[[nodiscard]] std::size_t count() const noexcept { return m_stack.size(); }

	/// @brief Access the topmost modal (if any).
	/// @return Pointer to the top modal, or nullptr if stack is empty.
	[[nodiscard]] const Modal* top() const noexcept
	{
		return m_stack.empty() ? nullptr : &m_stack.back().modal;
	}

	/// @brief Access the topmost modal (mutable).
	[[nodiscard]] Modal* top() noexcept
	{
		return m_stack.empty() ? nullptr : &m_stack.back().modal;
	}

	// ── Input forwarding ─────────────────────────────────────

	/// @brief Forward confirm key to the topmost modal.
	void onConfirmPressed()
	{
		if (!m_stack.empty()) m_stack.back().modal.onConfirmPressed();
	}

	/// @brief Forward Escape key to the topmost modal.
	void onEscapePressed()
	{
		if (!m_stack.empty()) m_stack.back().modal.onEscapePressed();
	}

	/// @brief Forward focus navigation to the topmost modal.
	void onFocusPrev()
	{
		if (!m_stack.empty()) m_stack.back().modal.onFocusPrev();
	}

	/// @brief Forward focus navigation to the topmost modal.
	void onFocusNext()
	{
		if (!m_stack.empty()) m_stack.back().modal.onFocusNext();
	}

	/// @brief Forward button click to the topmost modal.
	void onButtonClicked(int index)
	{
		if (!m_stack.empty()) m_stack.back().modal.onButtonClicked(index);
	}

	/// @brief Forward backdrop click to the topmost modal.
	void onBackdropClicked()
	{
		if (!m_stack.empty()) m_stack.back().modal.onBackdropClicked();
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief Update all modals and remove completed ones.
	/// @param deltaTime Frame delta time in seconds.
	void update(float deltaTime)
	{
		for (auto& entry : m_stack)
		{
			entry.modal.update(deltaTime);
		}

		// Remove modals that have finished hiding.
		while (!m_stack.empty()
			&& m_stack.back().modal.state() == ModalState::Hidden)
		{
			auto& entry = m_stack.back();
			if (entry.onResult)
			{
				entry.onResult(entry.modal.resultId());
			}
			m_stack.pop_back();
		}
	}

	/// @brief Access all modals in the stack (bottom to top).
	[[nodiscard]] std::size_t modalCount() const noexcept { return m_stack.size(); }

	/// @brief Access a modal by stack index (0 = bottom).
	[[nodiscard]] const Modal& modalAt(std::size_t index) const
	{
		return m_stack.at(index).modal;
	}

private:
	struct ModalEntry
	{
		Modal modal;
		std::function<void(const std::string&)> onResult;
	};

	std::vector<ModalEntry> m_stack;
};

} // namespace mitiru::ui
