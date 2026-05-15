#pragma once

/// @file DialogueBox.hpp
/// @brief ADV-style dialogue box: char-by-char reveal + click cooldown +
///        multiple-choice branching (G-02).
///
/// Renderer-agnostic. The class owns state only — the game decides whether
/// to draw natively (via `mitiru::Screen`) or ship state to a CEF page via
/// `StateStore::set("dialogue", box.toJson())`. CJK line-breaking / 禁則処理
/// is intentionally NOT done here; native renderers own layout, and CEF
/// pages can lean on `word-break: keep-all` + Japanese fonts.
///
/// Lifecycle:
///   1. `setLines({ {speaker, text}, ... })`  — start a new sequence
///   2. `setChoices({...})` (optional)        — presented after the last line
///   3. Each frame: `update(dt, primaryActionPressed)`
///   4. Read `visibleText()`, `currentLine()`, `awaitingChoice()`, ...
///   5. When `awaitingChoice()` is true, call `selectChoice(id)` with the
///      player's pick; then `isComplete()` becomes true.
///
/// Click semantics:
///   - Click *while revealing* jumps to the end of the current line.
///   - Click *while fully revealed and not the last line* advances to the next line.
///   - Click *on last line* either shows choices (if set) or marks complete.
///   - Clicks during the cooldown window are ignored to prevent accidental
///     double-advance.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace mitiru::ui
{

struct DialogueLine
{
	std::string                speaker;
	std::string                text;
	std::string                portraitKey;          ///< opaque key the game resolves to a Texture
	std::optional<float>       revealSpeedOverride;  ///< chars/sec for this line; nullopt → use default
};

struct DialogueChoice
{
	std::string label;
	int         id = 0;
};

/// @brief Visual hint data. Engine does not interpret — forwarded to renderer.
struct DialogueBoxStyle
{
	/// Hex `#RRGGBB` or `#RRGGBBAA` colour strings so this struct is
	/// serialisable to JSON without pulling a colour type in.
	std::string background = "#000000C8";
	std::string textColor  = "#F0F0F0FF";
	std::string speakerColor = "#FFD95AFF";
	float       padding    = 16.0f;
	std::string fontFamily;
};

class DialogueBox
{
public:
	using json = ::nlohmann::json;

	DialogueBox() = default;

	// ── setup ─────────────────────────────────────────────────

	/// @brief Start a new line sequence. Resets reveal / index / choices.
	void setLines(std::vector<DialogueLine> lines)
	{
		m_lines              = std::move(lines);
		m_lineIndex          = 0;
		m_revealProgress     = 0.0f;
		m_waitingForAdvance  = m_lines.empty();  // empty sequence → done immediately
		m_awaitingChoice     = false;
		m_selectedChoiceId   = std::nullopt;
		m_clickCooldownLeft  = 0.0f;
		m_isComplete         = m_lines.empty() && m_choices.empty();
	}

	void setChoices(std::vector<DialogueChoice> choices)
	{
		m_choices = std::move(choices);
		/// Re-evaluate completion for the edge case of empty-lines + choices.
		if (m_lines.empty() && !m_choices.empty())
		{
			m_awaitingChoice = true;
			m_isComplete     = false;
		}
	}

	void setStyle(DialogueBoxStyle style) noexcept { m_style = std::move(style); }
	const DialogueBoxStyle& style() const noexcept { return m_style; }

	/// @brief Default reveal speed in chars/sec. Per-line overrides win.
	void setDefaultRevealSpeed(float charsPerSec) noexcept
	{
		m_defaultRevealSpeed = charsPerSec > 0.0f ? charsPerSec : 1.0f;
	}

	/// @brief Seconds a click is swallowed after it registers (default 0.5).
	void setClickCooldown(float seconds) noexcept
	{
		m_clickCooldownDuration = seconds < 0.0f ? 0.0f : seconds;
	}

	// ── per-frame ─────────────────────────────────────────────

	/// @brief Advance the state machine one frame.
	/// @param dt                   seconds since last tick
	/// @param primaryActionPressed true on the *edge* (just-pressed) of the
	///                             advance input (mouse button / Space / A)
	void update(float dt, bool primaryActionPressed)
	{
		if (m_clickCooldownLeft > 0.0f)
		{
			m_clickCooldownLeft -= dt;
			if (m_clickCooldownLeft < 0.0f) { m_clickCooldownLeft = 0.0f; }
		}

		if (m_isComplete || m_awaitingChoice) { return; }

		// Reveal
		if (m_lineIndex < m_lines.size())
		{
			const auto&  line  = m_lines[m_lineIndex];
			const float  speed = line.revealSpeedOverride.value_or(m_defaultRevealSpeed);
			const float  full  = static_cast<float>(line.text.size());
			m_revealProgress += dt * speed;
			if (m_revealProgress >= full)
			{
				m_revealProgress    = full;
				m_waitingForAdvance = true;
			}
		}

		// Click
		if (primaryActionPressed && m_clickCooldownLeft <= 0.0f)
		{
			m_clickCooldownLeft = m_clickCooldownDuration;
			advance();
		}
	}

	/// @brief Programmatic advance — identical to a well-timed click.
	void advance()
	{
		if (m_isComplete || m_awaitingChoice) { return; }
		if (m_lineIndex >= m_lines.size())    { return; }

		const auto& line = m_lines[m_lineIndex];
		const float full = static_cast<float>(line.text.size());

		if (m_revealProgress < full)
		{
			// Mid-reveal: jump to end of current line.
			m_revealProgress    = full;
			m_waitingForAdvance = true;
			return;
		}

		// Line fully shown; move to next or end.
		++m_lineIndex;
		if (m_lineIndex >= m_lines.size())
		{
			if (m_choices.empty())
			{
				m_isComplete        = true;
				m_waitingForAdvance = false;
			}
			else
			{
				m_awaitingChoice    = true;
				m_waitingForAdvance = false;
			}
		}
		else
		{
			m_revealProgress    = 0.0f;
			m_waitingForAdvance = false;
		}
	}

	/// @brief Player selected a choice by its `id`. Marks complete.
	bool selectChoice(int id)
	{
		if (!m_awaitingChoice) { return false; }
		for (const auto& c : m_choices)
		{
			if (c.id == id)
			{
				m_selectedChoiceId = id;
				m_awaitingChoice   = false;
				m_isComplete       = true;
				return true;
			}
		}
		return false;
	}

	/// @brief Reset all state without clearing configuration (lines/choices).
	void restart() noexcept
	{
		m_lineIndex          = 0;
		m_revealProgress     = 0.0f;
		m_waitingForAdvance  = m_lines.empty();
		m_awaitingChoice     = m_lines.empty() && !m_choices.empty();
		m_selectedChoiceId   = std::nullopt;
		m_clickCooldownLeft  = 0.0f;
		m_isComplete         = m_lines.empty() && m_choices.empty();
	}

	// ── accessors ─────────────────────────────────────────────

	const DialogueLine* currentLine() const noexcept
	{
		if (m_lineIndex >= m_lines.size()) { return nullptr; }
		return &m_lines[m_lineIndex];
	}

	/// @brief UTF-8 byte-safe prefix of the current line up to reveal progress.
	/// @note Cuts at the progress byte index. For CJK multi-byte chars this
	///       may land mid-sequence; the caller should either (a) render with
	///       a text engine that tolerates truncation (CEF does), or (b) pair
	///       this with a utf-8 codepoint walker. We keep the byte-level API
	///       because it's cheap and renderer-agnostic.
	std::string visibleText() const
	{
		const auto* line = currentLine();
		if (!line) { return {}; }
		const std::size_t n = static_cast<std::size_t>(m_revealProgress);
		const std::size_t capped = n < line->text.size() ? n : line->text.size();
		return line->text.substr(0, capped);
	}

	[[nodiscard]] bool        isComplete() const noexcept     { return m_isComplete; }
	[[nodiscard]] bool        awaitingChoice() const noexcept { return m_awaitingChoice; }
	[[nodiscard]] bool        isRevealing() const noexcept    { return !m_waitingForAdvance && !m_awaitingChoice && !m_isComplete; }
	[[nodiscard]] std::size_t lineIndex() const noexcept      { return m_lineIndex; }
	[[nodiscard]] float       revealProgress() const noexcept { return m_revealProgress; }
	[[nodiscard]] std::optional<int> selectedChoice() const noexcept { return m_selectedChoiceId; }
	[[nodiscard]] const std::vector<DialogueLine>&  lines() const noexcept   { return m_lines; }
	[[nodiscard]] const std::vector<DialogueChoice>& choices() const noexcept { return m_choices; }

	// ── CEF helper ────────────────────────────────────────────

	/// @brief Serialise state for `StateStore::set("dialogue", box.toJson())`.
	/// @details The JS side can subscribe via
	///          `mitiru.onStateChange('dialogue', state => ...)` and render
	///          with CSS (using the `style` sub-object for colours).
	json toJson() const
	{
		json j;
		j["complete"]        = m_isComplete;
		j["awaitingChoice"]  = m_awaitingChoice;
		j["lineIndex"]       = m_lineIndex;
		j["revealProgress"]  = m_revealProgress;
		j["selectedChoice"]  = m_selectedChoiceId.has_value()
		                         ? json(*m_selectedChoiceId) : json(nullptr);
		if (const auto* line = currentLine())
		{
			j["current"] = {
				{"speaker",     line->speaker},
				{"text",        line->text},
				{"visible",     visibleText()},
				{"portraitKey", line->portraitKey},
			};
		}
		else
		{
			j["current"] = nullptr;
		}
		json choices = json::array();
		for (const auto& c : m_choices)
		{
			choices.push_back({{"label", c.label}, {"id", c.id}});
		}
		j["choices"] = std::move(choices);
		j["style"] = {
			{"background",   m_style.background},
			{"textColor",    m_style.textColor},
			{"speakerColor", m_style.speakerColor},
			{"padding",      m_style.padding},
			{"fontFamily",   m_style.fontFamily},
		};
		return j;
	}

private:
	std::vector<DialogueLine>    m_lines;
	std::vector<DialogueChoice>  m_choices;
	DialogueBoxStyle             m_style{};

	std::size_t         m_lineIndex            = 0;
	float               m_revealProgress       = 0.0f;
	float               m_defaultRevealSpeed   = 30.0f;   ///< chars/sec
	float               m_clickCooldownDuration = 0.5f;
	float               m_clickCooldownLeft    = 0.0f;
	bool                m_waitingForAdvance    = true;    ///< true on construction (empty sequence)
	bool                m_awaitingChoice       = false;
	bool                m_isComplete           = true;    ///< no lines configured → already "done"
	std::optional<int>  m_selectedChoiceId;
};

} // namespace mitiru::ui
