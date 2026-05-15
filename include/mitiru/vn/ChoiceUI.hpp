#pragma once

/// @file ChoiceUI.hpp
/// @brief Interactive choice/selection system for visual novels.
/// @details Displays N choices as a vertical (or horizontal/grid) button list
///          with keyboard, mouse, and controller navigation. Supports timed
///          choices, conditional enable/disable, and sequential entry animation.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/SpriteBatch.hpp>

namespace mitiru::vn
{

// ── State machine ────────────────────────────────────────────

/// @brief Choice UI visibility / interaction state.
enum class ChoiceState : std::uint8_t
{
	Hidden,        ///< Not visible.
	Appearing,     ///< Entry animation in progress.
	Active,        ///< Accepting input.
	Selected,      ///< A choice was made, brief feedback state.
	Disappearing   ///< Exit animation in progress.
};

// ── Layout ───────────────────────────────────────────────────

/// @brief Layout direction for choices.
enum class ChoiceLayout : std::uint8_t
{
	Vertical,     ///< Top-to-bottom list.
	Horizontal,   ///< Left-to-right row.
	Grid          ///< Multi-column grid.
};

/// @brief Horizontal alignment of the choice list.
enum class ChoiceAlignment : std::uint8_t
{
	Left,
	Center,
	Right
};

/// @brief Entry animation for individual choices.
enum class ChoiceAnimation : std::uint8_t
{
	None,       ///< Instant appearance.
	FadeIn,     ///< Alpha fade per item.
	SlideIn     ///< Slide in from side.
};

// ── Styling ──────────────────────────────────────────────────

/// @brief Visual style for choice buttons.
struct ChoiceButtonStyle
{
	sgc::Colorf normalColor{0.15f, 0.15f, 0.15f, 0.85f};
	sgc::Colorf hoverColor{0.25f, 0.25f, 0.35f, 0.9f};
	sgc::Colorf selectedColor{0.0f, 0.5f, 1.0f, 0.9f};
	sgc::Colorf disabledColor{0.3f, 0.3f, 0.3f, 0.5f};
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
	sgc::Colorf disabledTextColor{0.6f, 0.6f, 0.6f, 0.6f};
	sgc::Colorf borderColor{0.5f, 0.5f, 0.5f, 0.8f};
	float borderWidth = 1.0f;
	float fontSize    = 20.0f;
	float paddingH    = 16.0f;   ///< Horizontal padding inside button.
	float paddingV    = 10.0f;   ///< Vertical padding inside button.
};

// ── Choice entry ─────────────────────────────────────────────

/// @brief A single choice item.
struct ChoiceEntry
{
	std::string text;                       ///< Display text.
	bool enabled = true;                    ///< Whether selectable (false = grayed).
	std::function<bool()> condition;        ///< Optional dynamic condition.

	/// @brief Evaluate whether this choice is currently selectable.
	[[nodiscard]] bool isEnabled() const
	{
		if (condition)
		{
			return condition();
		}
		return enabled;
	}
};

// ── Callbacks ────────────────────────────────────────────────

/// @brief Called when a choice is selected.
/// @param index Zero-based index of the selected choice.
using ChoiceSelectedCallback = std::function<void(std::size_t index)>;

/// @brief Called when the timer expires with no selection.
/// @param defaultIndex The default choice index.
using ChoiceTimeoutCallback = std::function<void(std::size_t defaultIndex)>;

/// @brief Text render callback for choice labels.
using ChoiceTextRenderer = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& text,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

// ── Configuration ────────────────────────────────────────────

/// @brief Full configuration for the choice UI.
struct ChoiceUIConfig
{
	// Layout
	sgc::Rectf containerBounds{460.0f, 300.0f, 1000.0f, 480.0f};
	ChoiceLayout layout     = ChoiceLayout::Vertical;
	ChoiceAlignment alignment = ChoiceAlignment::Center;
	float buttonWidth       = 600.0f;
	float buttonHeight      = 48.0f;
	float spacing           = 8.0f;
	int gridColumns         = 2;       ///< Columns for Grid layout.

	// Animation
	ChoiceAnimation entryAnimation = ChoiceAnimation::FadeIn;
	float animDurationSec     = 0.3f;     ///< Total entry animation time.
	float perItemDelaySec     = 0.08f;    ///< Stagger delay between items.
	float exitDurationSec     = 0.2f;     ///< Exit animation time.

	// Timer
	bool timedChoice          = false;    ///< Enable countdown timer.
	float timeoutSec          = 10.0f;    ///< Seconds before timeout.
	std::size_t defaultChoice = 0;        ///< Choice selected on timeout.
	sgc::Colorf timerBarColor{1.0f, 0.6f, 0.0f, 0.9f};
	float timerBarHeight      = 4.0f;

	// Style
	ChoiceButtonStyle buttonStyle;
};

// ── ChoiceUI class ───────────────────────────────────────────

/// @brief Interactive choice/selection UI for visual novels.
///
/// @code
/// mitiru::vn::ChoiceUI choices;
/// choices.setChoices({
///     {"Go north", true},
///     {"Go south", true},
///     {"Stay here", false}
/// });
/// choices.onSelected([](std::size_t idx) {
///     // Handle selection...
/// });
/// choices.show();
///
/// // In game loop:
/// choices.update(dt);
/// batch.begin();
/// choices.draw(batch);
/// batch.end();
/// @endcode
class ChoiceUI
{
	ChoiceUIConfig m_config;
	ChoiceState m_state = ChoiceState::Hidden;

	std::vector<ChoiceEntry> m_choices;
	int m_focusedIndex = 0;       ///< Keyboard/controller focus index.
	int m_hoveredIndex = -1;      ///< Mouse hover index (-1 = none).
	int m_selectedIndex = -1;     ///< Final selection (-1 = none).

	// Animation
	float m_animTimer     = 0.0f;
	float m_exitTimer     = 0.0f;
	float m_exitAlpha     = 1.0f;

	// Countdown timer
	float m_countdownTimer = 0.0f;

	// Per-item animation progress (0 to 1).
	std::vector<float> m_itemProgress;

	// Callbacks
	ChoiceSelectedCallback m_onSelected;
	ChoiceTimeoutCallback m_onTimeout;
	ChoiceTextRenderer m_textRenderer;

public:
	/// @brief Construct with default configuration.
	/// @param config Choice UI configuration.
	explicit ChoiceUI(ChoiceUIConfig config = {})
		: m_config(std::move(config))
	{
	}

	// ── State ────────────────────────────────────────────────

	/// @brief Current state.
	[[nodiscard]] ChoiceState state() const noexcept { return m_state; }

	/// @brief Whether the UI is accepting input.
	[[nodiscard]] bool isActive() const noexcept
	{
		return m_state == ChoiceState::Active;
	}

	/// @brief Index of the selected choice (-1 if none).
	[[nodiscard]] int selectedIndex() const noexcept { return m_selectedIndex; }

	/// @brief Index of the focused choice.
	[[nodiscard]] int focusedIndex() const noexcept { return m_focusedIndex; }

	/// @brief Remaining countdown time (0 if not timed).
	[[nodiscard]] float remainingTime() const noexcept
	{
		return m_config.timedChoice
			? std::max(0.0f, m_config.timeoutSec - m_countdownTimer)
			: 0.0f;
	}

	/// @brief Access configuration.
	[[nodiscard]] const ChoiceUIConfig& config() const noexcept { return m_config; }

	/// @brief Access the choice list.
	[[nodiscard]] const std::vector<ChoiceEntry>& choices() const noexcept
	{
		return m_choices;
	}

	// ── Setup ────────────────────────────────────────────────

	/// @brief Replace configuration.
	void setConfig(ChoiceUIConfig config) { m_config = std::move(config); }

	/// @brief Set the list of choices.
	void setChoices(std::vector<ChoiceEntry> choices)
	{
		m_choices = std::move(choices);
		m_itemProgress.assign(m_choices.size(), 0.0f);
		m_focusedIndex = findFirstEnabled(0);
		m_hoveredIndex = -1;
		m_selectedIndex = -1;
	}

	/// @brief Register selection callback.
	void onSelected(ChoiceSelectedCallback cb) { m_onSelected = std::move(cb); }

	/// @brief Register timeout callback.
	void onTimeout(ChoiceTimeoutCallback cb) { m_onTimeout = std::move(cb); }

	/// @brief Set text render callback.
	void setTextRenderer(ChoiceTextRenderer cb) { m_textRenderer = std::move(cb); }

	// ── Commands ─────────────────────────────────────────────

	/// @brief Show choices with entry animation.
	void show()
	{
		if (m_choices.empty()) return;

		m_state = (m_config.entryAnimation == ChoiceAnimation::None)
			? ChoiceState::Active
			: ChoiceState::Appearing;

		m_animTimer = 0.0f;
		m_exitTimer = 0.0f;
		m_exitAlpha = 1.0f;
		m_countdownTimer = 0.0f;
		m_selectedIndex = -1;
		m_focusedIndex = findFirstEnabled(0);

		if (m_config.entryAnimation == ChoiceAnimation::None)
		{
			std::fill(m_itemProgress.begin(), m_itemProgress.end(), 1.0f);
		}
		else
		{
			std::fill(m_itemProgress.begin(), m_itemProgress.end(), 0.0f);
		}
	}

	/// @brief Hide choices (typically after selection).
	void dismiss()
	{
		if (m_state == ChoiceState::Hidden) return;
		m_state = ChoiceState::Disappearing;
		m_exitTimer = 0.0f;
	}

	// ── Input ────────────────────────────────────────────────

	/// @brief Navigate focus up (keyboard/d-pad).
	void focusUp()
	{
		if (!isActive()) return;
		const int prev = findPreviousEnabled(m_focusedIndex);
		if (prev >= 0) m_focusedIndex = prev;
	}

	/// @brief Navigate focus down (keyboard/d-pad).
	void focusDown()
	{
		if (!isActive()) return;
		const int next = findNextEnabled(m_focusedIndex);
		if (next >= 0) m_focusedIndex = next;
	}

	/// @brief Confirm the focused choice (Enter/A button).
	void confirm()
	{
		if (!isActive()) return;
		if (m_focusedIndex < 0
		    || static_cast<std::size_t>(m_focusedIndex) >= m_choices.size())
		{
			return;
		}
		if (!m_choices[static_cast<std::size_t>(m_focusedIndex)].isEnabled())
		{
			return;
		}

		selectChoice(static_cast<std::size_t>(m_focusedIndex));
	}

	/// @brief Handle mouse movement for hover detection.
	/// @param screenX Mouse X in screen space.
	/// @param screenY Mouse Y in screen space.
	void onMouseMove(float screenX, float screenY)
	{
		if (!isActive()) return;

		m_hoveredIndex = -1;
		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			const sgc::Rectf rect = computeButtonRect(i);
			if (screenX >= rect.x() && screenX < rect.x() + rect.width()
			    && screenY >= rect.y() && screenY < rect.y() + rect.height())
			{
				m_hoveredIndex = static_cast<int>(i);
				if (m_choices[i].isEnabled())
				{
					m_focusedIndex = static_cast<int>(i);
				}
				break;
			}
		}
	}

	/// @brief Handle mouse click.
	/// @param screenX Click X in screen space.
	/// @param screenY Click Y in screen space.
	void onMouseClick(float screenX, float screenY)
	{
		if (!isActive()) return;

		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			const sgc::Rectf rect = computeButtonRect(i);
			if (screenX >= rect.x() && screenX < rect.x() + rect.width()
			    && screenY >= rect.y() && screenY < rect.y() + rect.height())
			{
				if (m_choices[i].isEnabled())
				{
					selectChoice(i);
				}
				break;
			}
		}
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief Update animation and timer state.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		switch (m_state)
		{
		case ChoiceState::Hidden:
			break;

		case ChoiceState::Appearing:
			updateEntryAnimation(dt);
			break;

		case ChoiceState::Active:
			updateCountdown(dt);
			break;

		case ChoiceState::Selected:
			// Brief flash, then auto-dismiss.
			m_exitTimer += dt;
			if (m_exitTimer >= 0.15f)
			{
				dismiss();
			}
			break;

		case ChoiceState::Disappearing:
			updateExitAnimation(dt);
			break;
		}
	}

	// ── Rendering ────────────────────────────────────────────

	/// @brief Draw the choice UI into a SpriteBatch.
	/// @param batch SpriteBatch (must be between begin/end).
	void draw(render::SpriteBatch& batch) const
	{
		if (m_state == ChoiceState::Hidden) return;

		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			drawChoiceButton(batch, i);
		}

		// Timer bar.
		if (m_config.timedChoice && m_state == ChoiceState::Active)
		{
			drawTimerBar(batch);
		}
	}

private:
	// ── Selection ────────────────────────────────────────────

	void selectChoice(std::size_t index)
	{
		m_selectedIndex = static_cast<int>(index);
		m_state = ChoiceState::Selected;
		m_exitTimer = 0.0f;

		if (m_onSelected)
		{
			m_onSelected(index);
		}
	}

	// ── Animation ────────────────────────────────────────────

	void updateEntryAnimation(float dt)
	{
		m_animTimer += dt;

		bool allComplete = true;
		for (std::size_t i = 0; i < m_choices.size(); ++i)
		{
			const float itemStart = static_cast<float>(i)
				* m_config.perItemDelaySec;
			const float elapsed = m_animTimer - itemStart;

			if (elapsed <= 0.0f)
			{
				m_itemProgress[i] = 0.0f;
				allComplete = false;
			}
			else if (elapsed < m_config.animDurationSec)
			{
				m_itemProgress[i] = elapsed / m_config.animDurationSec;
				allComplete = false;
			}
			else
			{
				m_itemProgress[i] = 1.0f;
			}
		}

		if (allComplete)
		{
			m_state = ChoiceState::Active;
		}
	}

	void updateExitAnimation(float dt)
	{
		m_exitTimer += dt;
		m_exitAlpha = 1.0f - std::min(1.0f,
			m_exitTimer / std::max(0.01f, m_config.exitDurationSec));

		if (m_exitAlpha <= 0.0f)
		{
			m_state = ChoiceState::Hidden;
		}
	}

	void updateCountdown(float dt)
	{
		if (!m_config.timedChoice) return;

		m_countdownTimer += dt;
		if (m_countdownTimer >= m_config.timeoutSec)
		{
			const std::size_t def = std::min(
				m_config.defaultChoice, m_choices.size() - 1);

			if (m_onTimeout)
			{
				m_onTimeout(def);
			}
			selectChoice(def);
		}
	}

	// ── Layout ───────────────────────────────────────────────

	/// @brief Compute the screen rect for a choice button by index.
	[[nodiscard]] sgc::Rectf computeButtonRect(std::size_t index) const noexcept
	{
		const auto& cb = m_config.containerBounds;
		const float bw = m_config.buttonWidth;
		const float bh = m_config.buttonHeight;
		const float sp = m_config.spacing;

		float x = cb.x();
		float y = cb.y();

		switch (m_config.layout)
		{
		case ChoiceLayout::Vertical:
			{
				const float totalH = static_cast<float>(m_choices.size())
					* bh + static_cast<float>(m_choices.size() - 1) * sp;

				// Vertical centering within container.
				const float startY = cb.y()
					+ (cb.height() - totalH) * 0.5f;

				y = startY + static_cast<float>(index) * (bh + sp);

				// Horizontal alignment.
				switch (m_config.alignment)
				{
				case ChoiceAlignment::Left:
					x = cb.x();
					break;
				case ChoiceAlignment::Center:
					x = cb.x() + (cb.width() - bw) * 0.5f;
					break;
				case ChoiceAlignment::Right:
					x = cb.x() + cb.width() - bw;
					break;
				}
			}
			break;

		case ChoiceLayout::Horizontal:
			{
				const float totalW = static_cast<float>(m_choices.size())
					* bw + static_cast<float>(m_choices.size() - 1) * sp;
				const float startX = cb.x()
					+ (cb.width() - totalW) * 0.5f;
				x = startX + static_cast<float>(index) * (bw + sp);
				y = cb.y() + (cb.height() - bh) * 0.5f;
			}
			break;

		case ChoiceLayout::Grid:
			{
				const int cols = std::max(1, m_config.gridColumns);
				const int row = static_cast<int>(index) / cols;
				const int col = static_cast<int>(index) % cols;
				const float totalW = static_cast<float>(cols) * bw
					+ static_cast<float>(cols - 1) * sp;
				const float startX = cb.x()
					+ (cb.width() - totalW) * 0.5f;
				x = startX + static_cast<float>(col) * (bw + sp);
				y = cb.y() + static_cast<float>(row) * (bh + sp);
			}
			break;
		}

		return sgc::Rectf{x, y, bw, bh};
	}

	// ── Drawing helpers ──────────────────────────────────────

	void drawChoiceButton(render::SpriteBatch& batch, std::size_t index) const
	{
		const auto& entry = m_choices[index];
		const sgc::Rectf rect = computeButtonRect(index);
		const auto& style = m_config.buttonStyle;

		// Per-item animation alpha.
		float itemAlpha = (index < m_itemProgress.size())
			? m_itemProgress[index] : 1.0f;

		// Global exit alpha.
		itemAlpha *= m_exitAlpha;

		if (itemAlpha <= 0.0f) return;

		// Determine button colour.
		const bool isEnabled = entry.isEnabled();
		const bool isFocused = (static_cast<int>(index) == m_focusedIndex);
		const bool isHovered = (static_cast<int>(index) == m_hoveredIndex);
		const bool isSelected = (static_cast<int>(index) == m_selectedIndex);

		sgc::Colorf bgColor;
		sgc::Colorf txtColor;

		if (!isEnabled)
		{
			bgColor = style.disabledColor;
			txtColor = style.disabledTextColor;
		}
		else if (isSelected)
		{
			bgColor = style.selectedColor;
			txtColor = style.textColor;
		}
		else if (isFocused || isHovered)
		{
			bgColor = style.hoverColor;
			txtColor = style.textColor;
		}
		else
		{
			bgColor = style.normalColor;
			txtColor = style.textColor;
		}

		bgColor.a *= itemAlpha;
		txtColor.a *= itemAlpha;

		// Apply slide-in offset if animating.
		sgc::Rectf drawRect = rect;
		if (m_config.entryAnimation == ChoiceAnimation::SlideIn
		    && itemAlpha < 1.0f)
		{
			const float offset = (1.0f - itemAlpha) * 50.0f;
			drawRect = sgc::Rectf{
				rect.x() + offset, rect.y(),
				rect.width(), rect.height()};
		}

		// Background.
		batch.drawRect(drawRect, bgColor);

		// Border.
		if (style.borderWidth > 0.0f)
		{
			auto borderCol = style.borderColor;
			borderCol.a *= itemAlpha;
			batch.drawRectFrame(drawRect, borderCol, style.borderWidth);
		}

		// Text via callback.
		if (m_textRenderer)
		{
			const sgc::Rectf textArea{
				drawRect.x() + style.paddingH,
				drawRect.y() + style.paddingV,
				drawRect.width() - style.paddingH * 2.0f,
				drawRect.height() - style.paddingV * 2.0f
			};
			m_textRenderer(batch, entry.text, textArea, txtColor, style.fontSize);
		}
	}

	void drawTimerBar(render::SpriteBatch& batch) const
	{
		const float progress = 1.0f
			- std::min(1.0f, m_countdownTimer / m_config.timeoutSec);
		const auto& cb = m_config.containerBounds;

		// Timer bar across the top of the container.
		const float barW = cb.width() * progress;
		const sgc::Rectf barRect{cb.x(), cb.y() - m_config.timerBarHeight - 2.0f,
		                         barW, m_config.timerBarHeight};

		batch.drawRect(barRect, m_config.timerBarColor);
	}

	// ── Navigation helpers ───────────────────────────────────

	[[nodiscard]] int findFirstEnabled(int from) const noexcept
	{
		for (std::size_t i = static_cast<std::size_t>(std::max(0, from));
		     i < m_choices.size(); ++i)
		{
			if (m_choices[i].isEnabled())
			{
				return static_cast<int>(i);
			}
		}
		return 0;
	}

	[[nodiscard]] int findNextEnabled(int from) const noexcept
	{
		for (std::size_t i = static_cast<std::size_t>(from) + 1;
		     i < m_choices.size(); ++i)
		{
			if (m_choices[i].isEnabled())
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	[[nodiscard]] int findPreviousEnabled(int from) const noexcept
	{
		if (from <= 0) return -1;
		for (int i = from - 1; i >= 0; --i)
		{
			if (m_choices[static_cast<std::size_t>(i)].isEnabled())
			{
				return i;
			}
		}
		return -1;
	}
};

} // namespace mitiru::vn
