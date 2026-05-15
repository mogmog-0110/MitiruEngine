#pragma once

/// @file BacklogUI.hpp
/// @brief Scrollable text history viewer for visual novels.
/// @details Displays all previously shown dialogue with speaker names,
///          supports smooth scrolling, voice replay, jump-to, and read/unread
///          indicators. Uses ScrollContainer internally for scroll physics.

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
#include <mitiru/vn/ScrollContainer.hpp>

namespace mitiru::vn
{

// ── State machine ────────────────────────────────────────────

/// @brief Backlog overlay visibility state.
enum class BacklogState : std::uint8_t
{
	Hidden,       ///< Not visible.
	ScrollingIn,  ///< Opening animation.
	Active,       ///< Visible and interactive.
	ScrollingOut  ///< Closing animation.
};

// ── Backlog entry ────────────────────────────────────────────

/// @brief A single entry in the dialogue backlog.
struct BacklogEntry
{
	std::string speaker;        ///< Speaker name (empty for narration).
	std::string text;           ///< Dialogue text.
	std::string voiceId;        ///< Voice clip identifier (empty if none).
	float timestamp = 0.0f;     ///< Game time when this was displayed.
	bool isChoice = false;      ///< Whether this entry represents a choice result.
	std::string choiceText;     ///< The choice that was selected (if isChoice).
	bool isRead = true;         ///< Whether the player has seen this entry.
};

// ── Callbacks ────────────────────────────────────────────────

/// @brief Called when the player requests voice replay.
/// @param voiceId Voice clip identifier.
using VoiceReplayCallback = std::function<void(const std::string& voiceId)>;

/// @brief Called when the player jumps to a backlog entry.
/// @param entryIndex Zero-based index of the entry.
using JumpToCallback = std::function<void(std::size_t entryIndex)>;

/// @brief Text render callback for backlog entries.
using BacklogTextRenderer = std::function<
	void(render::SpriteBatch& batch,
	     const std::string& text,
	     const sgc::Rectf& area,
	     const sgc::Colorf& color,
	     float fontSize)>;

// ── Styling ──────────────────────────────────────────────────

/// @brief Visual style for the backlog overlay.
struct BacklogStyle
{
	sgc::Colorf overlayColor{0.0f, 0.0f, 0.0f, 0.85f};       ///< Background dimming.
	sgc::Colorf speakerColor{0.3f, 0.7f, 1.0f, 1.0f};        ///< Speaker name colour.
	sgc::Colorf textColor{1.0f, 1.0f, 1.0f, 1.0f};           ///< Body text colour.
	sgc::Colorf choiceColor{1.0f, 0.8f, 0.2f, 1.0f};         ///< Choice indicator colour.
	sgc::Colorf unreadMark{1.0f, 0.3f, 0.3f, 0.8f};          ///< Unread dot colour.
	sgc::Colorf voiceButtonColor{0.2f, 0.6f, 0.9f, 0.8f};    ///< Voice replay button.
	sgc::Colorf entryHoverColor{0.2f, 0.2f, 0.3f, 0.4f};     ///< Hover highlight.
	sgc::Colorf separatorColor{0.3f, 0.3f, 0.3f, 0.5f};      ///< Line between entries.

	float speakerFontSize = 16.0f;
	float textFontSize    = 18.0f;
	float entryPadding    = 12.0f;    ///< Padding inside each entry.
	float entrySpacing    = 4.0f;     ///< Space between entries.
	float speakerHeight   = 24.0f;    ///< Height reserved for speaker line.
	float textLineHeight  = 22.0f;    ///< Height per text line (estimate).
	float voiceButtonSize = 20.0f;    ///< Voice replay button size.
	float unreadDotSize   = 6.0f;     ///< Unread indicator dot size.
	float marginLeft      = 60.0f;    ///< Left margin (for indicators).
	float marginRight     = 30.0f;    ///< Right margin.
};

// ── Configuration ────────────────────────────────────────────

/// @brief Full configuration for the backlog UI.
struct BacklogUIConfig
{
	sgc::Rectf bounds{0.0f, 0.0f, 1920.0f, 1080.0f};  ///< Overlay bounds.
	std::size_t maxEntries = 500;                        ///< Maximum stored entries.
	float animDurationSec  = 0.3f;                       ///< Open/close animation.

	BacklogStyle style;
	ScrollConfig scrollConfig;
};

// ── BacklogUI class ──────────────────────────────────────────

/// @brief Scrollable text history viewer overlay.
///
/// @code
/// mitiru::vn::BacklogUI backlog;
/// backlog.addEntry({"Alice", "Hello!", "voice_001", 1.0f});
/// backlog.addEntry({"Bob",   "Hi there!", "", 2.0f});
///
/// backlog.show();
///
/// // In game loop:
/// backlog.update(dt);
/// batch.begin();
/// backlog.draw(batch);
/// batch.end();
/// @endcode
class BacklogUI
{
	BacklogUIConfig m_config;
	BacklogState m_state = BacklogState::Hidden;

	std::vector<BacklogEntry> m_entries;
	ScrollContainer m_scroll;

	// Animation
	float m_animTimer = 0.0f;
	float m_overlayAlpha = 0.0f;

	// Interaction
	int m_hoveredEntry = -1;

	// Callbacks
	VoiceReplayCallback m_onVoiceReplay;
	JumpToCallback m_onJumpTo;
	BacklogTextRenderer m_textRenderer;

public:
	/// @brief Construct with default configuration.
	/// @param config Backlog configuration.
	explicit BacklogUI(BacklogUIConfig config = {})
		: m_config(std::move(config))
		, m_scroll(m_config.bounds, 0.0f, 0.0f, m_config.scrollConfig)
	{
	}

	// ── State ────────────────────────────────────────────────

	/// @brief Current state.
	[[nodiscard]] BacklogState state() const noexcept { return m_state; }

	/// @brief Whether the backlog is visible.
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_state != BacklogState::Hidden;
	}

	/// @brief Number of entries in the backlog.
	[[nodiscard]] std::size_t entryCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief Access the entry list.
	[[nodiscard]] const std::vector<BacklogEntry>& entries() const noexcept
	{
		return m_entries;
	}

	/// @brief Access an entry by index.
	/// @param index Zero-based index.
	/// @return Pointer to the entry, or nullptr if out of range.
	[[nodiscard]] const BacklogEntry* entryAt(std::size_t index) const noexcept
	{
		return (index < m_entries.size()) ? &m_entries[index] : nullptr;
	}

	/// @brief Access configuration.
	[[nodiscard]] const BacklogUIConfig& config() const noexcept { return m_config; }

	/// @brief Access the internal scroll container.
	[[nodiscard]] const ScrollContainer& scrollContainer() const noexcept
	{
		return m_scroll;
	}

	// ── Setup ────────────────────────────────────────────────

	/// @brief Replace configuration.
	void setConfig(BacklogUIConfig config)
	{
		m_config = std::move(config);
		m_scroll.setViewport(m_config.bounds);
		m_scroll.setConfig(m_config.scrollConfig);
	}

	/// @brief Register voice replay callback.
	void onVoiceReplay(VoiceReplayCallback cb) { m_onVoiceReplay = std::move(cb); }

	/// @brief Register jump-to callback.
	void onJumpTo(JumpToCallback cb) { m_onJumpTo = std::move(cb); }

	/// @brief Set text render callback.
	void setTextRenderer(BacklogTextRenderer cb) { m_textRenderer = std::move(cb); }

	// ── Entry management ─────────────────────────────────────

	/// @brief Add a dialogue entry to the backlog.
	/// @param entry The entry to add.
	void addEntry(BacklogEntry entry)
	{
		m_entries.push_back(std::move(entry));

		// Enforce max entries.
		while (m_entries.size() > m_config.maxEntries)
		{
			m_entries.erase(m_entries.begin());
		}

		recalculateContentHeight();
	}

	/// @brief Clear all entries.
	void clearEntries()
	{
		m_entries.clear();
		m_scroll.setContentHeight(0.0f);
		m_scroll.scrollToTop();
	}

	/// @brief Mark all entries as read.
	void markAllRead()
	{
		for (auto& entry : m_entries)
		{
			entry.isRead = true;
		}
	}

	// ── Commands ─────────────────────────────────────────────

	/// @brief Show the backlog overlay.
	void show()
	{
		if (m_state != BacklogState::Hidden) return;

		recalculateContentHeight();

		m_state = BacklogState::ScrollingIn;
		m_animTimer = 0.0f;

		// Start scrolled to bottom (most recent).
		m_scroll.scrollToBottom();
	}

	/// @brief Hide the backlog overlay.
	void hide()
	{
		if (m_state == BacklogState::Hidden
		    || m_state == BacklogState::ScrollingOut)
		{
			return;
		}

		m_state = BacklogState::ScrollingOut;
		m_animTimer = 0.0f;
	}

	/// @brief Toggle visibility.
	void toggle()
	{
		if (m_state == BacklogState::Hidden)
		{
			show();
		}
		else if (m_state == BacklogState::Active)
		{
			hide();
		}
	}

	// ── Input ────────────────────────────────────────────────

	/// @brief Handle mouse wheel.
	/// @param delta Wheel delta (negative = scroll down).
	void onMouseWheel(float delta)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onMouseWheel(delta);
		}
	}

	/// @brief Handle key scroll (up/down).
	/// @param deltaPixels Positive = scroll down, negative = scroll up.
	void onKeyScroll(float deltaPixels)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onMouseWheel(-deltaPixels / m_config.scrollConfig.wheelMultiplier);
		}
	}

	/// @brief Handle mouse move for hover detection.
	/// @param screenX Mouse X.
	/// @param screenY Mouse Y.
	void onMouseMove(float screenX, float screenY)
	{
		if (m_state != BacklogState::Active) return;

		m_hoveredEntry = -1;
		const float scrollY = m_scroll.scrollY();

		float yPos = m_config.bounds.y() - scrollY;
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			const float entryH = estimateEntryHeight(i);
			const sgc::Rectf entryRect{
				m_config.bounds.x(), yPos,
				m_config.bounds.width(), entryH};

			if (screenY >= entryRect.y()
			    && screenY < entryRect.y() + entryRect.height()
			    && screenX >= entryRect.x()
			    && screenX < entryRect.x() + entryRect.width())
			{
				// Only register if within the viewport.
				if (entryRect.y() + entryH > m_config.bounds.y()
				    && entryRect.y() < m_config.bounds.y() + m_config.bounds.height())
				{
					m_hoveredEntry = static_cast<int>(i);
				}
				break;
			}
			yPos += entryH + m_config.style.entrySpacing;
		}
	}

	/// @brief Handle click on a backlog entry.
	/// @param screenX Click X.
	/// @param screenY Click Y.
	void onMouseClick(float screenX, float screenY)
	{
		if (m_state != BacklogState::Active || m_hoveredEntry < 0) return;

		const auto idx = static_cast<std::size_t>(m_hoveredEntry);
		if (idx >= m_entries.size()) return;

		const auto& entry = m_entries[idx];

		// Check if click is on the voice replay button area.
		const float scrollY = m_scroll.scrollY();
		float yPos = m_config.bounds.y() - scrollY;
		for (std::size_t i = 0; i < idx; ++i)
		{
			yPos += estimateEntryHeight(i) + m_config.style.entrySpacing;
		}

		const float voiceX = m_config.bounds.x() + 10.0f;
		const float voiceY = yPos + m_config.style.entryPadding;
		const float voiceSize = m_config.style.voiceButtonSize;

		if (!entry.voiceId.empty()
		    && screenX >= voiceX && screenX < voiceX + voiceSize
		    && screenY >= voiceY && screenY < voiceY + voiceSize)
		{
			if (m_onVoiceReplay)
			{
				m_onVoiceReplay(entry.voiceId);
			}
		}
		else
		{
			// Jump to this dialogue point.
			if (m_onJumpTo)
			{
				m_onJumpTo(idx);
			}
		}
	}

	/// @brief Handle drag start.
	void onDragBegin(float x, float y)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onDragBegin(x, y);
		}
	}

	/// @brief Handle drag move.
	void onDragMove(float x, float y)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onDragMove(x, y);
		}
	}

	/// @brief Handle drag end.
	void onDragEnd(float velX = 0.0f, float velY = 0.0f)
	{
		if (m_state == BacklogState::Active)
		{
			m_scroll.onDragEnd(velX, velY);
		}
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief Update animation and scroll physics.
	/// @param dt Delta time in seconds.
	void update(float dt)
	{
		switch (m_state)
		{
		case BacklogState::Hidden:
			m_overlayAlpha = 0.0f;
			break;

		case BacklogState::ScrollingIn:
			m_animTimer += dt;
			m_overlayAlpha = std::min(1.0f,
				m_animTimer / std::max(0.01f, m_config.animDurationSec));
			if (m_overlayAlpha >= 1.0f)
			{
				m_state = BacklogState::Active;
				m_overlayAlpha = 1.0f;
			}
			m_scroll.update(dt);
			break;

		case BacklogState::Active:
			m_overlayAlpha = 1.0f;
			m_scroll.update(dt);
			break;

		case BacklogState::ScrollingOut:
			m_animTimer += dt;
			m_overlayAlpha = 1.0f - std::min(1.0f,
				m_animTimer / std::max(0.01f, m_config.animDurationSec));
			if (m_overlayAlpha <= 0.0f)
			{
				m_state = BacklogState::Hidden;
				m_overlayAlpha = 0.0f;
			}
			m_scroll.update(dt);
			break;
		}
	}

	// ── Rendering ────────────────────────────────────────────

	/// @brief Draw the backlog overlay into a SpriteBatch.
	/// @param batch SpriteBatch (must be between begin/end).
	void draw(render::SpriteBatch& batch) const
	{
		if (m_state == BacklogState::Hidden || m_overlayAlpha <= 0.0f)
		{
			return;
		}

		// Background overlay.
		auto overlayCol = m_config.style.overlayColor;
		overlayCol.a *= m_overlayAlpha;
		batch.drawRect(m_config.bounds, overlayCol);

		// Draw entries.
		const float scrollY = m_scroll.scrollY();
		const float viewTop = m_config.bounds.y();
		const float viewBottom = viewTop + m_config.bounds.height();

		float yPos = m_config.bounds.y() - scrollY;

		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			const float entryH = estimateEntryHeight(i);
			const float entryBottom = yPos + entryH;

			// Skip entries entirely above or below the viewport.
			if (entryBottom > viewTop && yPos < viewBottom)
			{
				drawEntry(batch, i, yPos, entryH);
			}

			yPos += entryH + m_config.style.entrySpacing;

			// Early exit if below viewport.
			if (yPos > viewBottom) break;
		}

		// Scroll bar.
		m_scroll.drawScrollBar(batch);
	}

private:
	// ── Entry layout helpers ─────────────────────────────────

	/// @brief Estimate the rendered height of an entry.
	[[nodiscard]] float estimateEntryHeight(std::size_t index) const noexcept
	{
		if (index >= m_entries.size()) return 0.0f;

		const auto& style = m_config.style;
		float h = style.entryPadding * 2.0f;

		// Speaker line.
		if (!m_entries[index].speaker.empty())
		{
			h += style.speakerHeight;
		}

		// Text lines (rough estimate based on character count and available width).
		const float availW = m_config.bounds.width()
			- style.marginLeft - style.marginRight;
		const float charsPerLine = std::max(1.0f, availW / (style.textFontSize * 0.6f));
		const auto textLen = static_cast<float>(m_entries[index].text.size());
		const float lines = std::max(1.0f, std::ceil(textLen / charsPerLine));
		h += lines * style.textLineHeight;

		// Choice indicator.
		if (m_entries[index].isChoice)
		{
			h += style.textLineHeight;
		}

		return h;
	}

	/// @brief Recalculate total content height for the scroll container.
	void recalculateContentHeight()
	{
		float totalH = 0.0f;
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			totalH += estimateEntryHeight(i) + m_config.style.entrySpacing;
		}
		m_scroll.setContentHeight(totalH);
	}

	// ── Entry drawing ────────────────────────────────────────

	void drawEntry(render::SpriteBatch& batch,
	               std::size_t index, float yPos, float entryH) const
	{
		const auto& entry = m_entries[index];
		const auto& style = m_config.style;
		const float alpha = m_overlayAlpha;

		const sgc::Rectf entryRect{
			m_config.bounds.x(), yPos,
			m_config.bounds.width(), entryH};

		// Hover highlight.
		if (static_cast<int>(index) == m_hoveredEntry)
		{
			auto hoverCol = style.entryHoverColor;
			hoverCol.a *= alpha;
			batch.drawRect(entryRect, hoverCol);
		}

		// Separator line.
		if (index > 0)
		{
			auto sepCol = style.separatorColor;
			sepCol.a *= alpha;
			batch.drawRect(
				sgc::Rectf{entryRect.x() + style.marginLeft, yPos,
				           entryRect.width() - style.marginLeft - style.marginRight,
				           1.0f},
				sepCol);
		}

		// Unread indicator.
		if (!entry.isRead)
		{
			auto dotCol = style.unreadMark;
			dotCol.a *= alpha;
			const float dotX = m_config.bounds.x() + 10.0f;
			const float dotY = yPos + style.entryPadding + 4.0f;
			batch.drawRect(
				sgc::Rectf{dotX, dotY, style.unreadDotSize, style.unreadDotSize},
				dotCol);
		}

		// Voice replay button.
		if (!entry.voiceId.empty())
		{
			auto voiceCol = style.voiceButtonColor;
			voiceCol.a *= alpha;
			const float btnX = m_config.bounds.x() + 30.0f;
			const float btnY = yPos + style.entryPadding;
			batch.drawRect(
				sgc::Rectf{btnX, btnY, style.voiceButtonSize, style.voiceButtonSize},
				voiceCol);
		}

		// Text content area.
		const float textX = m_config.bounds.x() + style.marginLeft;
		const float textW = m_config.bounds.width()
			- style.marginLeft - style.marginRight;
		float textY = yPos + style.entryPadding;

		// Speaker name.
		if (!entry.speaker.empty() && m_textRenderer)
		{
			auto speakerCol = style.speakerColor;
			speakerCol.a *= alpha;
			m_textRenderer(batch, entry.speaker,
				sgc::Rectf{textX, textY, textW, style.speakerHeight},
				speakerCol, style.speakerFontSize);
			textY += style.speakerHeight;
		}

		// Body text.
		if (m_textRenderer)
		{
			auto textCol = style.textColor;
			textCol.a *= alpha;
			const float remainH = entryH - (textY - yPos) - style.entryPadding;
			m_textRenderer(batch, entry.text,
				sgc::Rectf{textX, textY, textW, remainH},
				textCol, style.textFontSize);
			textY += remainH;
		}

		// Choice indicator.
		if (entry.isChoice && !entry.choiceText.empty() && m_textRenderer)
		{
			auto choiceCol = style.choiceColor;
			choiceCol.a *= alpha;
			const std::string choiceLabel = "> " + entry.choiceText;
			m_textRenderer(batch, choiceLabel,
				sgc::Rectf{textX + 12.0f, textY - style.textLineHeight,
				           textW - 12.0f, style.textLineHeight},
				choiceCol, style.textFontSize);
		}
	}
};

} // namespace mitiru::vn
