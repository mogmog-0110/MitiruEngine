#pragma once

/// @file Modal.hpp
/// @brief UI framework 用の汎用 modal dialog システム。
/// @details 設定可能な button、backdrop、animation、keyboard navigation、
///          および重ね合わせ dialog 用の modal stack manager を備えた、
///          柔軟な modal dialog を提供する。Yes/No/Cancel プリセットに
///          限定される vn::ConfirmDialog と違い、本システムは任意の
///          button 構成をサポートする。

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

/// @brief modal button のスタイルヒント。
enum class ModalButtonStyle : std::uint8_t
{
	Default,    ///< 標準の見た目。
	Primary,    ///< 強調 / 確定 action。
	Danger,     ///< 破壊的 action (削除など)。
	Cancel,     ///< キャンセル / 閉じる action。
};

/// @brief modal dialog 内の 1 個の button。
struct ModalButton
{
	std::string label;                              ///< button テキスト。
	std::string resultId;                           ///< click 時に返される識別子。
	ModalButtonStyle style = ModalButtonStyle::Default; ///< 見た目のスタイルヒント。
	bool focused = false;                           ///< この button が focus 中か。
};

// ════════════════════════════════════════════════════════════════════
//  Modal configuration
// ════════════════════════════════════════════════════════════════════

/// @brief backdrop (modal 背後の overlay) 設定。
struct ModalBackdrop
{
	float opacity         = 0.5f;   ///< backdrop の不透明度 [0, 1]。
	bool dismissOnClick   = false;  ///< backdrop click で閉じる。
};

/// @brief modal の show/hide animation 設定。
struct ModalAnimation
{
	float fadeInDuration  = 0.2f;   ///< fade-in 時間 (秒)。
	float fadeOutDuration = 0.15f;  ///< fade-out 時間 (秒)。
	EasingType showEasing = EasingType::EaseOutBack;  ///< show 時の easing curve。
	EasingType hideEasing = EasingType::EaseInQuad;   ///< hide 時の easing curve。
};

/// @brief modal dialog 生成用の設定。
struct ModalConfig
{
	std::string title;                     ///< dialog タイトル (空 = title bar 無し)。
	std::string content;                   ///< 本文 / メッセージ。
	std::vector<ModalButton> buttons;      ///< button リスト (左から右)。
	ModalBackdrop backdrop;                ///< backdrop 設定。
	ModalAnimation animation;              ///< animation 設定。
	bool escapeToClose    = true;          ///< Escape キーで閉じることを許可。
	std::string escapeResultId = "cancel"; ///< Escape で閉じた時の result ID。
	float width           = 400.0f;        ///< dialog 幅 (px)。
	float minHeight       = 0.0f;          ///< dialog の最小高さ。
};

// ════════════════════════════════════════════════════════════════════
//  Modal state
// ════════════════════════════════════════════════════════════════════

/// @brief modal dialog の表示状態。
enum class ModalState : std::uint8_t
{
	Hidden,     ///< 非表示。
	FadingIn,   ///< 出現中。
	Visible,    ///< 完全表示かつ操作可能。
	FadingOut,  ///< 消失中。
};

// ════════════════════════════════════════════════════════════════════
//  Modal dialog
// ════════════════════════════════════════════════════════════════════

/// @brief 汎用 modal dialog。
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
	/// @brief 空の modal を構築する。
	Modal() noexcept = default;

	/// @brief UITheme を与えて構築する。
	/// @param theme 見た目のスタイル用 theme。
	explicit Modal(const UITheme& theme) noexcept
		: m_theme(theme)
	{
	}

	// ── Show / hide ──────────────────────────────────────────

	/// @brief 指定の設定で modal を表示する。
	/// @param config modal パラメータ。
	void show(ModalConfig config)
	{
		m_config = std::move(config);
		m_state = ModalState::FadingIn;
		m_animProgress = 0.0f;
		m_resultId.clear();
		m_focusIndex = 0;

		// 初期 focus を設定。
		if (!m_config.buttons.empty())
		{
			m_config.buttons[0].focused = true;
		}
	}

	/// @brief 指定の result で modal を閉じる。
	/// @param resultId result 識別子。
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

	/// @brief animation 無しで modal を即座に非表示にする。
	void hide() noexcept
	{
		m_state = ModalState::Hidden;
		m_animProgress = 0.0f;
		m_resultId.clear();
	}

	// ── Input handling ───────────────────────────────────────

	/// @brief 確定キー押下を処理する (Enter / Space)。
	void onConfirmPressed()
	{
		if (m_state != ModalState::Visible || m_config.buttons.empty())
		{
			return;
		}
		const auto& btn = m_config.buttons[static_cast<std::size_t>(m_focusIndex)];
		dismiss(btn.resultId);
	}

	/// @brief Escape キー押下を処理する。
	void onEscapePressed()
	{
		if (m_state != ModalState::Visible || !m_config.escapeToClose)
		{
			return;
		}
		dismiss(m_config.escapeResultId);
	}

	/// @brief backdrop click を処理する。
	void onBackdropClicked()
	{
		if (m_state != ModalState::Visible || !m_config.backdrop.dismissOnClick)
		{
			return;
		}
		dismiss(m_config.escapeResultId);
	}

	/// @brief focus を前の button へ移動する。
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

	/// @brief focus を次の button へ移動する。
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

	/// @brief index 指定で button click を処理する。
	/// @param index button の index。
	void onButtonClicked(int index)
	{
		if (m_state != ModalState::Visible) return;
		if (index < 0 || index >= static_cast<int>(m_config.buttons.size())) return;
		dismiss(m_config.buttons[static_cast<std::size_t>(index)].resultId);
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief modal の animation 状態を更新する。
	/// @param deltaTime frame の delta time (秒)。
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

	/// @brief result callback を設定する。
	/// @param fn modal が閉じる時に resultId 付きで呼ばれる関数。
	void setOnResult(std::function<void(const std::string&)> fn)
	{
		m_onResult = std::move(fn);
	}

	// ── Theme ────────────────────────────────────────────────

	/// @brief UITheme を設定する。
	void setTheme(const UITheme& theme) noexcept { m_theme = theme; }

	/// @brief 現在の UITheme にアクセスする。
	[[nodiscard]] const UITheme& theme() const noexcept { return m_theme; }

	// ── State queries ────────────────────────────────────────

	/// @brief 現在の表示状態。
	[[nodiscard]] ModalState state() const noexcept { return m_state; }

	/// @brief modal が表示中か (Hidden 以外か)。
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_state != ModalState::Hidden;
	}

	/// @brief modal が下層への入力を block すべきか。
	[[nodiscard]] bool isModal() const noexcept
	{
		return m_state != ModalState::Hidden;
	}

	/// @brief 直近の dismiss で得た result 識別子。
	[[nodiscard]] const std::string& resultId() const noexcept { return m_resultId; }

	/// @brief animation の進捗 [0.0, 1.0]。
	[[nodiscard]] float animProgress() const noexcept { return m_animProgress; }

	/// @brief 表示 scale (show/hide animation に応じて easing 済み)。
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

	/// @brief 表示 alpha (線形 fade)。
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

	/// @brief backdrop alpha (表示 alpha と backdrop 不透明度を加味)。
	[[nodiscard]] float backdropAlpha() const noexcept
	{
		return displayAlpha() * m_config.backdrop.opacity;
	}

	/// @brief 設定にアクセスする。
	[[nodiscard]] const ModalConfig& config() const noexcept { return m_config; }

	/// @brief button リストにアクセスする (現在の focus 状態付き)。
	[[nodiscard]] const std::vector<ModalButton>& buttons() const noexcept
	{
		return m_config.buttons;
	}

	/// @brief 現在の focus index。
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

/// @brief 入力 block 付きでアクティブな modal の stack を管理する。
/// @details 最上位の modal のみが入力を受け取る。下位の modal は表示は
///          残るが非アクティブ。最上位が閉じられると、stack の次の
///          modal がアクティブになる。
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
	/// @brief 新しい modal を stack に push する。
	/// @param config modal の設定。
	/// @param onResult この modal が閉じられた時の callback。
	void push(ModalConfig config,
	          std::function<void(const std::string&)> onResult = nullptr)
	{
		auto& entry = m_stack.emplace_back();
		entry.modal.show(std::move(config));
		entry.onResult = std::move(onResult);
	}

	/// @brief アクティブな modal があるか。
	[[nodiscard]] bool hasActiveModal() const noexcept
	{
		return !m_stack.empty();
	}

	/// @brief アクティブな modal の数。
	[[nodiscard]] std::size_t count() const noexcept { return m_stack.size(); }

	/// @brief 最上位の modal にアクセスする (あれば)。
	/// @return 最上位 modal への pointer。stack が空なら nullptr。
	[[nodiscard]] const Modal* top() const noexcept
	{
		return m_stack.empty() ? nullptr : &m_stack.back().modal;
	}

	/// @brief 最上位の modal にアクセスする (mutable)。
	[[nodiscard]] Modal* top() noexcept
	{
		return m_stack.empty() ? nullptr : &m_stack.back().modal;
	}

	// ── Input forwarding ─────────────────────────────────────

	/// @brief 確定キーを最上位 modal へ転送する。
	void onConfirmPressed()
	{
		if (!m_stack.empty()) m_stack.back().modal.onConfirmPressed();
	}

	/// @brief Escape キーを最上位 modal へ転送する。
	void onEscapePressed()
	{
		if (!m_stack.empty()) m_stack.back().modal.onEscapePressed();
	}

	/// @brief focus navigation を最上位 modal へ転送する。
	void onFocusPrev()
	{
		if (!m_stack.empty()) m_stack.back().modal.onFocusPrev();
	}

	/// @brief focus navigation を最上位 modal へ転送する。
	void onFocusNext()
	{
		if (!m_stack.empty()) m_stack.back().modal.onFocusNext();
	}

	/// @brief button click を最上位 modal へ転送する。
	void onButtonClicked(int index)
	{
		if (!m_stack.empty()) m_stack.back().modal.onButtonClicked(index);
	}

	/// @brief backdrop click を最上位 modal へ転送する。
	void onBackdropClicked()
	{
		if (!m_stack.empty()) m_stack.back().modal.onBackdropClicked();
	}

	// ── Update ───────────────────────────────────────────────

	/// @brief 全 modal を更新し、完了したものを除去する。
	/// @param deltaTime frame の delta time (秒)。
	void update(float deltaTime)
	{
		for (auto& entry : m_stack)
		{
			entry.modal.update(deltaTime);
		}

		// hide が完了した modal を除去する。
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

	/// @brief stack 内の全 modal にアクセスする (下から上へ)。
	[[nodiscard]] std::size_t modalCount() const noexcept { return m_stack.size(); }

	/// @brief stack index 指定で modal にアクセスする (0 = 最下位)。
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
