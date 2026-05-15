#pragma once

/// @file MessageWindow.hpp
/// @brief Visual novel message window component.
/// @details Full-featured dialogue display with ADV/NVL modes, speaker name plate,
///          character-by-character text reveal, click-wait indicator, window skins
///          (solid/9-slice/custom), and show/hide animations.

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
#include <mitiru/vn/NineSlice.hpp>

namespace mitiru::vn
{

// ── State machine ────────────────────────────────────────────

/// @brief Message window visibility state.
enum class MessageWindowState : std::uint8_t
{
	Hidden,        ///< Not visible.
	Appearing,     ///< Show animation in progress.
	Idle,          ///< Visible but no text being displayed.
	Displaying,    ///< Character-by-character reveal in progress.
	WaitingClick,  ///< All text shown, waiting for player click.
	Disappearing   ///< Hide animation in progress.
};

/// @brief Display mode for the message window.
enum class MessageMode : std::uint8_t
{
	ADV,   ///< Bottom text box (standard adventure game).
	NVL    ///< Full-screen text with scrolling (novel mode).
};

/// @brief Show/hide animation type.
enum class WindowAnimation : std::uint8_t
{
	None,      ///< Instant show/hide.
	Fade,      ///< Alpha fade.
	SlideUp    ///< Slide up from bottom.
};

// ── Window skin ──────────────────────────────────────────────

/// @brief Window skin type selector.
enum class WindowSkinType : std::uint8_t
{
	SolidColor,     ///< Flat colour with border and alpha.
	Image9Slice,    ///< 9-slice scalable background.
	Custom          ///< User-provided render callback.
};

/// @brief Solid-colour skin parameters.
struct SolidColorSkin
{
	sgc::Colorf fillColor{0.0f, 0.0f, 0.0f, 0.75f};
	sgc::Colorf borderColor{0.4f, 0.4f, 0.4f, 1.0f};
	float borderWidth = 2.0f;
	float cornerRadius = 0.0f;  ///< Reserved for future rounded corners.
};

/// @brief Custom render callback signature.
/// @param batch  SpriteBatch to draw into.
/// @param rect   Window rectangle.
/// @param alpha  Current alpha (from animation).
using CustomSkinRenderer = std::function<
	void(render::SpriteBatch& batch, const sgc::Rectf& rect, float alpha)>;

/// @brief Window skin configuration.
struct WindowSkin
{
	WindowSkinType type = WindowSkinType::SolidColor;
	SolidColorSkin solidColor;
	NineSliceConfig nineSlice;
	CustomSkinRenderer customRenderer;
};

// ── Click-wait indicator ─────────────────────────────────────

/// @brief Configuration for the click-wait indicator glyph.
struct ClickWaitIndicator
{
	bool enabled         = true;
	float size           = 12.0f;        ///< Indicator size in pixels.
	float offsetX        = -20.0f;       ///< Offset from right edge.
	float offsetY        = -20.0f;       ///< Offset from bottom edge.
	float blinkSpeed     = 3.0f;         ///< Blinks per second.
	sgc::Colorf color{1.0f, 1.0f, 1.0f, 1.0f};
};

// ── Text reveal callback ─────────────────────────────────────

/// @brief Callback for rendering text.
/// @param batch  SpriteBatch.
/// @param text   Full text string.
/// @param visibleChars Number of characters to show (0 = all).
/// @param area   Text drawing area.
/// @param color  Text colour.
/// @param fontSize Text size.
using TextRenderCallback = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& text,
	     std::size_t visibleChars,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

/// @brief Callback for rendering the speaker name.
using NameRenderCallback = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& name,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

// ── Configuration ────────────────────────────────────────────

/// @brief Full configuration for the message window.
struct MessageWindowConfig
{
	// Layout
	sgc::Rectf bounds{0.0f, 700.0f, 1920.0f, 300.0f};  ///< Window bounds.
	float paddingLeft   = 24.0f;
	float paddingRight  = 24.0f;
	float paddingTop    = 20.0f;
	float paddingBottom = 20.0f;

	// Name plate
	bool showNamePlate          = true;
	sgc::Rectf namePlateBounds{24.0f, 660.0f, 220.0f, 40.0f};
	sgc::Colorf namePlateColor{0.0f, 0.0f, 0.0f, 0.85f};
	sgc::Colorf namePlateBorder{0.6f, 0.6f, 0.6f, 1.0f};
	sgc::Colorf nameTextColor{1.0f, 1.0f, 1.0f, 1.0f};
	float nameFontSize = 18.0f;

	// Text
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};
	float fontSize       = 22.0f;
	float charsPerSecond = 30.0f;  ///< Character reveal speed.

	// Mode
	MessageMode mode = MessageMode::ADV;

	// NVL-specific
	float nvlMaxLines    = 20;           ///< Maximum visible lines in NVL mode.
	sgc::Rectf nvlBounds{100.0f, 50.0f, 1720.0f, 980.0f};

	// Skin
	WindowSkin skin;

	// Animation
	WindowAnimation showAnimation = WindowAnimation::Fade;
	WindowAnimation hideAnimation = WindowAnimation::Fade;
	float animationDurationSec    = 0.25f;

	// Click-wait indicator
	ClickWaitIndicator clickWait;
};

// ── NVL line entry ───────────────────────────────────────────

/// @brief A single line in NVL mode's accumulated text buffer.
struct NvlLine
{
	std::string speaker;  ///< Speaker name (empty for narration).
	std::string text;     ///< Line text.
};

// ── MessageWindow class ──────────────────────────────────────

/// @brief Main dialogue display component for visual novels.
///
/// @code
/// mitiru::vn::MessageWindowConfig cfg;
/// cfg.bounds = {0, 700, 1920, 300};
/// mitiru::vn::MessageWindow window(cfg);
///
/// window.show();
/// window.setText("Alice", "Hello! Nice to meet you.");
///
/// // In game loop:
/// window.update(dt);
/// batch.begin();
/// window.draw(batch);
/// batch.end();
///
/// if (window.state() == mitiru::vn::MessageWindowState::WaitingClick)
/// {
///     if (clicked) window.advance();
/// }
/// @endcode
class MessageWindow
{
	MessageWindowConfig m_config;
	MessageWindowState m_state = MessageWindowState::Hidden;

	// Text state
	std::string m_speaker;
	std::string m_text;
	std::size_t m_visibleChars = 0;
	float m_charTimer = 0.0f;

	// NVL accumulated lines
	std::vector<NvlLine> m_nvlLines;
	bool m_nvlNewLineRevealing = false;

	// Animation state
	float m_animProgress = 0.0f;  ///< 0 = start, 1 = complete.
	float m_alpha = 0.0f;         ///< Current effective alpha.

	// Click-wait indicator
	float m_indicatorTimer = 0.0f;

	// Page history for in-window scroll (current page only)
	std::vector<std::string> m_pageHistory;
	int m_pageHistoryIndex = -1;

	// 9-slice renderer (lazy-initialized)
	NineSlice m_nineSlice{NineSliceConfig{}};

	// Render callbacks
	TextRenderCallback m_textRenderer;
	NameRenderCallback m_nameRenderer;

public:
	/// @brief Construct with the given configuration.
	/// @param config Window configuration.
	explicit MessageWindow(MessageWindowConfig config = {})
		: m_config(std::move(config))
		, m_nineSlice(m_config.skin.nineSlice)
	{
	}

	// ── State queries ────────────────────────────────────────

	/// @brief Current state.
	[[nodiscard]] MessageWindowState state() const noexcept { return m_state; }

	/// @brief Current display mode.
	[[nodiscard]] MessageMode mode() const noexcept { return m_config.mode; }

	/// @brief Current effective alpha (0-1).
	[[nodiscard]] float alpha() const noexcept { return m_alpha; }

	/// @brief Whether all text is fully revealed.
	[[nodiscard]] bool isTextComplete() const noexcept
	{
		return m_visibleChars >= m_text.size();
	}

	/// @brief Whether the window is visible (any state except Hidden).
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_state != MessageWindowState::Hidden;
	}

	/// @brief Current speaker name.
	[[nodiscard]] const std::string& speaker() const noexcept { return m_speaker; }

	/// @brief Current full text.
	[[nodiscard]] const std::string& text() const noexcept { return m_text; }

	/// @brief Number of visible characters.
	[[nodiscard]] std::size_t visibleChars() const noexcept { return m_visibleChars; }

	/// @brief Access configuration.
	[[nodiscard]] const MessageWindowConfig& config() const noexcept { return m_config; }

	/// @brief Active window bounds (accounts for ADV/NVL mode).
	[[nodiscard]] const sgc::Rectf& activeBounds() const noexcept
	{
		return (m_config.mode == MessageMode::NVL)
			? m_config.nvlBounds
			: m_config.bounds;
	}

	// ── Configuration ────────────────────────────────────────

	/// @brief Replace the full configuration.
	void setConfig(MessageWindowConfig config)
	{
		m_config = std::move(config);
		m_nineSlice.setConfig(m_config.skin.nineSlice);
	}

	/// @brief Set display mode.
	void setMode(MessageMode mode) noexcept { m_config.mode = mode; }

	/// @brief Set text render callback.
	void setTextRenderer(TextRenderCallback cb) { m_textRenderer = std::move(cb); }

	/// @brief Set name render callback.
	void setNameRenderer(NameRenderCallback cb) { m_nameRenderer = std::move(cb); }

	// ── Commands ─────────────────────────────────────────────

	/// @brief Show the window with animation.
	void show()
	{
		if (m_state != MessageWindowState::Hidden
		    && m_state != MessageWindowState::Disappearing)
		{
			return;
		}

		if (m_config.showAnimation == WindowAnimation::None)
		{
			m_state = MessageWindowState::Idle;
			m_alpha = 1.0f;
			m_animProgress = 1.0f;
		}
		else
		{
			m_state = MessageWindowState::Appearing;
			m_animProgress = 0.0f;
		}
	}

	/// @brief Hide the window with animation.
	void hide()
	{
		if (m_state == MessageWindowState::Hidden
		    || m_state == MessageWindowState::Disappearing)
		{
			return;
		}

		if (m_config.hideAnimation == WindowAnimation::None)
		{
			m_state = MessageWindowState::Hidden;
			m_alpha = 0.0f;
			m_animProgress = 0.0f;
		}
		else
		{
			m_state = MessageWindowState::Disappearing;
			m_animProgress = 0.0f;
		}
	}

	/// @brief Set new dialogue text.
	/// @param speaker Speaker name (empty for narration).
	/// @param text Dialogue text.
	void setText(const std::string& speaker, const std::string& text)
	{
		if (m_config.mode == MessageMode::NVL)
		{
			m_nvlLines.push_back(NvlLine{speaker, text});
			m_nvlNewLineRevealing = true;

			// Trim old lines beyond limit.
			const auto maxLines = static_cast<std::size_t>(m_config.nvlMaxLines);
			while (m_nvlLines.size() > maxLines)
			{
				m_nvlLines.erase(m_nvlLines.begin());
			}
		}

		// Save to page history.
		if (!m_text.empty())
		{
			m_pageHistory.push_back(m_text);
		}
		m_pageHistoryIndex = -1;

		m_speaker = speaker;
		m_text = text;
		m_visibleChars = 0;
		m_charTimer = 0.0f;

		if (m_state == MessageWindowState::Idle
		    || m_state == MessageWindowState::WaitingClick)
		{
			m_state = MessageWindowState::Displaying;
		}
	}

	/// @brief Instantly reveal all remaining text.
	void revealAll() noexcept
	{
		m_visibleChars = m_text.size();
		if (m_state == MessageWindowState::Displaying)
		{
			m_state = MessageWindowState::WaitingClick;
		}
		m_nvlNewLineRevealing = false;
	}

	/// @brief Advance past click-wait (call when player clicks).
	void advance()
	{
		if (m_state == MessageWindowState::Displaying)
		{
			// First click: instant reveal.
			revealAll();
		}
		else if (m_state == MessageWindowState::WaitingClick)
		{
			// Second click: ready for next text.
			m_state = MessageWindowState::Idle;
		}
	}

	/// @brief Clear NVL mode accumulated text.
	void clearNvl()
	{
		m_nvlLines.clear();
		m_nvlNewLineRevealing = false;
	}

	/// @brief Scroll page history up (show previous text).
	/// @return true if scrolled, false if at beginning.
	[[nodiscard]] bool scrollHistoryUp()
	{
		if (m_pageHistory.empty()) return false;

		if (m_pageHistoryIndex < 0)
		{
			m_pageHistoryIndex = static_cast<int>(m_pageHistory.size()) - 1;
		}
		else if (m_pageHistoryIndex > 0)
		{
			--m_pageHistoryIndex;
		}
		else
		{
			return false;
		}
		return true;
	}

	/// @brief Scroll page history down (show next text).
	/// @return true if scrolled back to current, false if not in history.
	[[nodiscard]] bool scrollHistoryDown()
	{
		if (m_pageHistoryIndex < 0) return false;

		if (m_pageHistoryIndex < static_cast<int>(m_pageHistory.size()) - 1)
		{
			++m_pageHistoryIndex;
		}
		else
		{
			m_pageHistoryIndex = -1;  // Return to current text.
		}
		return true;
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief Update state and animations.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		switch (m_state)
		{
		case MessageWindowState::Hidden:
			m_alpha = 0.0f;
			break;

		case MessageWindowState::Appearing:
			updateAnimation(dt, true);
			break;

		case MessageWindowState::Idle:
			m_alpha = 1.0f;
			break;

		case MessageWindowState::Displaying:
			m_alpha = 1.0f;
			updateTextReveal(dt);
			break;

		case MessageWindowState::WaitingClick:
			m_alpha = 1.0f;
			m_indicatorTimer += dt;
			break;

		case MessageWindowState::Disappearing:
			updateAnimation(dt, false);
			break;
		}
	}

	// ── Rendering ────────────────────────────────────────────

	/// @brief Draw the message window into a SpriteBatch.
	/// @param batch SpriteBatch (must be between begin/end).
	void draw(render::SpriteBatch& batch) const
	{
		if (m_state == MessageWindowState::Hidden || m_alpha <= 0.0f)
		{
			return;
		}

		const sgc::Rectf& bounds = activeBounds();
		const float yOffset = computeSlideOffset();

		const sgc::Rectf drawBounds{
			bounds.x(),
			bounds.y() + yOffset,
			bounds.width(),
			bounds.height()
		};

		// Draw window background.
		drawSkin(batch, drawBounds);

		// Draw name plate (ADV mode only).
		if (m_config.mode == MessageMode::ADV && m_config.showNamePlate
		    && !m_speaker.empty())
		{
			drawNamePlate(batch, yOffset);
		}

		// Draw text.
		drawText(batch, drawBounds);

		// Draw click-wait indicator.
		if (m_state == MessageWindowState::WaitingClick
		    && m_config.clickWait.enabled)
		{
			drawClickWaitIndicator(batch, drawBounds);
		}
	}

private:
	// ── Animation helpers ────────────────────────────────────

	void updateAnimation(float dt, bool appearing)
	{
		const float duration = m_config.animationDurationSec;
		if (duration <= 0.0f)
		{
			m_animProgress = 1.0f;
		}
		else
		{
			m_animProgress += dt / duration;
			m_animProgress = std::min(1.0f, m_animProgress);
		}

		const float t = smoothstep(m_animProgress);

		if (appearing)
		{
			m_alpha = t;
			if (m_animProgress >= 1.0f)
			{
				m_state = MessageWindowState::Idle;
				m_alpha = 1.0f;
			}
		}
		else
		{
			m_alpha = 1.0f - t;
			if (m_animProgress >= 1.0f)
			{
				m_state = MessageWindowState::Hidden;
				m_alpha = 0.0f;
			}
		}
	}

	void updateTextReveal(float dt)
	{
		if (m_config.charsPerSecond <= 0.0f || m_visibleChars >= m_text.size())
		{
			m_visibleChars = m_text.size();
			m_state = MessageWindowState::WaitingClick;
			m_nvlNewLineRevealing = false;
			return;
		}

		m_charTimer += dt;
		const float interval = 1.0f / m_config.charsPerSecond;

		while (m_charTimer >= interval && m_visibleChars < m_text.size())
		{
			m_charTimer -= interval;
			++m_visibleChars;
		}

		if (m_visibleChars >= m_text.size())
		{
			m_state = MessageWindowState::WaitingClick;
			m_nvlNewLineRevealing = false;
		}
	}

	/// @brief Compute vertical offset for slide animation.
	[[nodiscard]] float computeSlideOffset() const noexcept
	{
		const bool isAppearing = (m_state == MessageWindowState::Appearing);
		const bool isDisappearing = (m_state == MessageWindowState::Disappearing);

		WindowAnimation anim = isAppearing
			? m_config.showAnimation
			: (isDisappearing ? m_config.hideAnimation : WindowAnimation::None);

		if (anim != WindowAnimation::SlideUp)
		{
			return 0.0f;
		}

		const float slideDistance = activeBounds().height();

		if (isAppearing)
		{
			return slideDistance * (1.0f - smoothstep(m_animProgress));
		}
		return slideDistance * smoothstep(m_animProgress);
	}

	// ── Skin drawing ─────────────────────────────────────────

	void drawSkin(render::SpriteBatch& batch, const sgc::Rectf& rect) const
	{
		switch (m_config.skin.type)
		{
		case WindowSkinType::SolidColor:
			drawSolidSkin(batch, rect);
			break;

		case WindowSkinType::Image9Slice:
			{
				auto tint = sgc::Colorf{1.0f, 1.0f, 1.0f, m_alpha};
				m_nineSlice.draw(batch, rect, tint);
			}
			break;

		case WindowSkinType::Custom:
			if (m_config.skin.customRenderer)
			{
				m_config.skin.customRenderer(batch, rect, m_alpha);
			}
			break;
		}
	}

	void drawSolidSkin(render::SpriteBatch& batch, const sgc::Rectf& rect) const
	{
		const auto& skin = m_config.skin.solidColor;

		// Fill.
		auto fill = skin.fillColor;
		fill.a *= m_alpha;
		batch.drawRect(rect, fill);

		// Border.
		if (skin.borderWidth > 0.0f)
		{
			auto border = skin.borderColor;
			border.a *= m_alpha;
			batch.drawRectFrame(rect, border, skin.borderWidth);
		}
	}

	// ── Name plate drawing ───────────────────────────────────

	void drawNamePlate(render::SpriteBatch& batch, float yOffset) const
	{
		const sgc::Rectf npRect{
			m_config.namePlateBounds.x(),
			m_config.namePlateBounds.y() + yOffset,
			m_config.namePlateBounds.width(),
			m_config.namePlateBounds.height()
		};

		// Background.
		auto bg = m_config.namePlateColor;
		bg.a *= m_alpha;
		batch.drawRect(npRect, bg);

		// Border.
		auto border = m_config.namePlateBorder;
		border.a *= m_alpha;
		batch.drawRectFrame(npRect, border, 1.0f);

		// Name text via callback.
		if (m_nameRenderer)
		{
			auto col = m_config.nameTextColor;
			col.a *= m_alpha;
			const sgc::Rectf textArea{
				npRect.x() + 8.0f,
				npRect.y() + 4.0f,
				npRect.width() - 16.0f,
				npRect.height() - 8.0f
			};
			m_nameRenderer(batch, m_speaker, textArea, col, m_config.nameFontSize);
		}
	}

	// ── Text drawing ─────────────────────────────────────────

	void drawText(render::SpriteBatch& batch, const sgc::Rectf& bounds) const
	{
		if (!m_textRenderer) return;

		const sgc::Rectf textArea{
			bounds.x() + m_config.paddingLeft,
			bounds.y() + m_config.paddingTop,
			bounds.width() - m_config.paddingLeft - m_config.paddingRight,
			bounds.height() - m_config.paddingTop - m_config.paddingBottom
		};

		auto col = m_config.textColor;
		col.a *= m_alpha;

		if (m_config.mode == MessageMode::NVL)
		{
			drawNvlText(batch, textArea, col);
		}
		else
		{
			// ADV mode: show current text or history page.
			const std::string& displayText = (m_pageHistoryIndex >= 0
				&& m_pageHistoryIndex < static_cast<int>(m_pageHistory.size()))
				? m_pageHistory[static_cast<std::size_t>(m_pageHistoryIndex)]
				: m_text;

			const std::size_t chars = (m_pageHistoryIndex >= 0)
				? displayText.size()
				: m_visibleChars;

			m_textRenderer(batch, displayText, chars, textArea,
			               col, m_config.fontSize);
		}
	}

	void drawNvlText(render::SpriteBatch& batch,
	                 const sgc::Rectf& area,
	                 const sgc::Colorf& col) const
	{
		// In NVL mode, concatenate all lines and render as one block.
		// The last line may be partially revealed.
		std::string fullText;
		for (std::size_t i = 0; i < m_nvlLines.size(); ++i)
		{
			const auto& line = m_nvlLines[i];
			if (!line.speaker.empty())
			{
				fullText += line.speaker + ": ";
			}
			fullText += line.text;
			if (i + 1 < m_nvlLines.size())
			{
				fullText += '\n';
			}
		}

		// All characters visible unless the last line is being revealed.
		std::size_t totalVisible = fullText.size();
		if (m_nvlNewLineRevealing && !m_nvlLines.empty())
		{
			// Only the last line is partially revealed.
			const std::size_t precedingLen = fullText.size() - m_text.size();
			totalVisible = precedingLen + m_visibleChars;
		}

		m_textRenderer(batch, fullText, totalVisible, area,
		               col, m_config.fontSize);
	}

	// ── Click-wait indicator ─────────────────────────────────

	void drawClickWaitIndicator(render::SpriteBatch& batch,
	                            const sgc::Rectf& bounds) const
	{
		const auto& cw = m_config.clickWait;
		const float blinkAlpha = (std::sin(m_indicatorTimer * cw.blinkSpeed
		                                   * 6.2831853f) + 1.0f) * 0.5f;

		auto col = cw.color;
		col.a *= m_alpha * blinkAlpha;

		const float x = bounds.x() + bounds.width() + cw.offsetX;
		const float y = bounds.y() + bounds.height() + cw.offsetY;

		// Draw as a small triangle pointing down.
		const sgc::Rectf indicator{x, y, cw.size, cw.size};
		batch.drawRect(indicator, col);
	}

	// ── Math utilities ───────────────────────────────────────

	/// @brief Smooth-step interpolation (ease in-out).
	[[nodiscard]] static float smoothstep(float t) noexcept
	{
		t = std::max(0.0f, std::min(1.0f, t));
		return t * t * (3.0f - 2.0f * t);
	}
};

} // namespace mitiru::vn
