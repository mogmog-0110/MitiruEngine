#pragma once

/// @file DialogueBox.hpp
/// @brief ADV 風 dialogue box: 1 文字ずつ reveal + クリック cooldown +
///        複数選択肢の分岐 (G-02)。
///
/// Renderer 非依存。このクラスは state のみを持つ — native 描画 (`mitiru::Screen`
/// 経由) するか、`StateStore::set("dialogue", box.toJson())` で state を CEF page
/// へ送るかはゲーム側が決める。CJK の行分割 / 禁則処理 はここでは意図的に
/// 行わない。native renderer が layout を持ち、CEF page は
/// `word-break: keep-all` + 日本語フォントに頼ればよい。
///
/// Lifecycle:
///   1. `setLines({ {speaker, text}, ... })`  — 新しいシーケンスを開始
///   2. `setChoices({...})` (optional)        — 最終行の後に提示される
///   3. 毎フレーム: `update(dt, primaryActionPressed)`
///   4. `visibleText()`, `currentLine()`, `awaitingChoice()` 等を読む
///   5. `awaitingChoice()` が true のとき、player の選択で `selectChoice(id)`
///      を呼ぶ。すると `isComplete()` が true になる。
///
/// クリックの意味:
///   - reveal 中のクリック → 現在行の末尾まで一気に表示。
///   - 全表示済み かつ 最終行でないときのクリック → 次の行へ進む。
///   - 最終行でのクリック → choices があれば表示、無ければ完了とする。
///   - cooldown 中のクリックは無視する (誤操作による二重送りを防ぐ)。

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
	std::string                portraitKey;          ///< ゲームが Texture に解決する不透明な key
	std::optional<float>       revealSpeedOverride;  ///< この行の chars/sec。nullopt → default を使う
};

struct DialogueChoice
{
	std::string label;
	int         id = 0;
};

/// @brief 見た目のヒントデータ。Engine は解釈せず renderer へ渡すだけ。
struct DialogueBoxStyle
{
	/// Hex `#RRGGBB` または `#RRGGBBAA` の色文字列。color 型を引き込まずに
	/// この struct を JSON へ serialize できるようにするため。
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

	/// @brief 新しい行シーケンスを開始する。reveal / index / choices を reset。
	void setLines(std::vector<DialogueLine> lines)
	{
		m_lines              = std::move(lines);
		m_lineIndex          = 0;
		m_revealProgress     = 0.0f;
		m_waitingForAdvance  = m_lines.empty();  // 空シーケンス → 即完了
		m_awaitingChoice     = false;
		m_selectedChoiceId   = std::nullopt;
		m_clickCooldownLeft  = 0.0f;
		m_isComplete         = m_lines.empty() && m_choices.empty();
	}

	void setChoices(std::vector<DialogueChoice> choices)
	{
		m_choices = std::move(choices);
		/// 空 lines + choices という edge case のため完了状態を再評価する。
		if (m_lines.empty() && !m_choices.empty())
		{
			m_awaitingChoice = true;
			m_isComplete     = false;
		}
	}

	void setStyle(DialogueBoxStyle style) noexcept { m_style = std::move(style); }
	const DialogueBoxStyle& style() const noexcept { return m_style; }

	/// @brief default の reveal 速度 (chars/sec)。行ごとの override が優先される。
	void setDefaultRevealSpeed(float charsPerSec) noexcept
	{
		m_defaultRevealSpeed = charsPerSec > 0.0f ? charsPerSec : 1.0f;
	}

	/// @brief クリック受付後にクリックを無視する秒数 (default 0.5)。
	void setClickCooldown(float seconds) noexcept
	{
		m_clickCooldownDuration = seconds < 0.0f ? 0.0f : seconds;
	}

	// ── per-frame ─────────────────────────────────────────────

	/// @brief state machine を 1 フレーム進める。
	/// @param dt                   前回 tick からの経過秒数
	/// @param primaryActionPressed 送り入力 (mouse button / Space / A) の
	///                             立ち上がり (押した瞬間) で true
	void update(float dt, bool primaryActionPressed)
	{
		if (m_clickCooldownLeft > 0.0f)
		{
			m_clickCooldownLeft -= dt;
			if (m_clickCooldownLeft < 0.0f) { m_clickCooldownLeft = 0.0f; }
		}

		if (m_isComplete || m_awaitingChoice) { return; }

		// 文字の reveal
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

		// クリック処理
		if (primaryActionPressed && m_clickCooldownLeft <= 0.0f)
		{
			m_clickCooldownLeft = m_clickCooldownDuration;
			advance();
		}
	}

	/// @brief プログラムからの送り — 適切なタイミングのクリックと等価。
	void advance()
	{
		if (m_isComplete || m_awaitingChoice) { return; }
		if (m_lineIndex >= m_lines.size())    { return; }

		const auto& line = m_lines[m_lineIndex];
		const float full = static_cast<float>(line.text.size());

		if (m_revealProgress < full)
		{
			// reveal 途中: 現在行の末尾まで一気に進める。
			m_revealProgress    = full;
			m_waitingForAdvance = true;
			return;
		}

		// 行を全表示済み。次の行へ、または終了へ。
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

	/// @brief player が `id` で選択肢を選んだ。完了とする。
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

	/// @brief 設定 (lines/choices) は消さずに全 state を reset する。
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

	/// @brief 現在行の、reveal 進捗までの UTF-8 バイト境界に安全な prefix。
	/// @note 進捗のバイト index で切る。CJK のマルチバイト文字では境界の
	///       途中で切れることがある。呼び出し側は (a) 途中切れを許容する
	///       text engine で描画する (CEF は許容) か、(b) utf-8 codepoint
	///       walker と組み合わせること。安価で renderer 非依存なので
	///       バイト単位 API のまま残している。
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

	/// @brief `StateStore::set("dialogue", box.toJson())` 用に state を serialize。
	/// @details JS 側は `mitiru.onStateChange('dialogue', state => ...)` で
	///          購読し、CSS で描画できる (色は `style` サブオブジェクトを使う)。
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
	bool                m_waitingForAdvance    = true;    ///< 構築時は true (空シーケンス)
	bool                m_awaitingChoice       = false;
	bool                m_isComplete           = true;    ///< 行が未設定 → 既に「完了」
	std::optional<int>  m_selectedChoiceId;
};

} // namespace mitiru::ui
