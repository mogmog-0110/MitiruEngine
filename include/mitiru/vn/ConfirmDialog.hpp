#pragma once

/// @file ConfirmDialog.hpp
/// @brief 再利用可能なモーダル確認ダイアログ
/// @details Yes/No/Cancel形式の確認ダイアログを提供する。
///          アニメーション付きの表示/非表示、キーボード操作をサポート。

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <mitiru/ui/UITheme.hpp>

namespace mitiru::vn
{

/// @brief ダイアログの結果
enum class DialogResult : std::uint8_t
{
	None   = 0,  ///< 未決定
	Ok     = 1,  ///< OK / Yes
	Cancel = 2,  ///< Cancel / No
	Third  = 3,  ///< 3番目のボタン（例: キャンセル）
};

/// @brief ダイアログのボタン配置プリセット
enum class DialogButtons : std::uint8_t
{
	OkCancel    = 0,  ///< OK / Cancel の2ボタン
	YesNo       = 1,  ///< Yes / No の2ボタン
	YesNoCancel = 2,  ///< Yes / No / Cancel の3ボタン
};

/// @brief ダイアログの表示状態
enum class DialogState : std::uint8_t
{
	Hidden   = 0,  ///< 非表示
	FadingIn = 1,  ///< フェードイン中
	Visible  = 2,  ///< 表示中
	FadingOut = 3, ///< フェードアウト中
};

/// @brief ダイアログ生成用パラメータ
struct DialogParams
{
	std::string title;                                ///< タイトルテキスト
	std::string message;                              ///< メッセージテキスト
	DialogButtons buttons = DialogButtons::OkCancel;  ///< ボタン配置
	float fadeInDuration  = 0.15f;                    ///< フェードイン時間（秒）
	float fadeOutDuration = 0.1f;                     ///< フェードアウト時間（秒）
};

/// @brief ダイアログ内のボタン情報
struct DialogButton
{
	std::string label;          ///< ボタンラベル
	DialogResult result;        ///< 押下時の結果
	bool focused = false;       ///< フォーカス状態
};

/// @brief 再利用可能なモーダル確認ダイアログ
/// @details 表示/非表示のアニメーション、キーボード操作、
///          コールバックによる結果通知を提供する。
///
/// @code
/// mitiru::vn::ConfirmDialog dialog;
/// dialog.setOnResult([](mitiru::vn::DialogResult r) {
///     if (r == mitiru::vn::DialogResult::Ok) {
///         // confirmed
///     }
/// });
///
/// mitiru::vn::DialogParams params;
/// params.title = "Overwrite?";
/// params.message = "Slot 3 already has data. Overwrite?";
/// params.buttons = mitiru::vn::DialogButtons::YesNo;
/// dialog.show(params);
///
/// // 毎フレーム
/// dialog.update(dt);
/// @endcode
class ConfirmDialog
{
public:
	/// @brief コンストラクタ
	ConfirmDialog() noexcept = default;

	/// @brief テーマ指定付きコンストラクタ
	/// @param theme UIテーマ
	explicit ConfirmDialog(const ui::UITheme& theme) noexcept
		: m_theme(theme)
	{
	}

	// ── 表示制御 ──────────────────────────────────────────

	/// @brief ダイアログを表示する
	/// @param params 表示パラメータ
	void show(DialogParams params)
	{
		m_params = std::move(params);
		m_state = DialogState::FadingIn;
		m_animProgress = 0.0f;
		m_result = DialogResult::None;
		m_focusIndex = 0;
		rebuildButtons();
	}

	/// @brief ダイアログを閉じる（フェードアウト開始）
	/// @param result 確定した結果
	void dismiss(DialogResult result) noexcept
	{
		if (m_state == DialogState::Hidden || m_state == DialogState::FadingOut)
		{
			return;
		}
		m_result = result;
		m_state = DialogState::FadingOut;
		m_animProgress = 0.0f;
	}

	/// @brief ダイアログを即座に非表示にする
	void hide() noexcept
	{
		m_state = DialogState::Hidden;
		m_animProgress = 0.0f;
		m_result = DialogResult::None;
	}

	// ── 入力処理 ──────────────────────────────────────────

	/// @brief 確認キー（Enter）押下を処理する
	void onConfirmPressed() noexcept
	{
		if (m_state != DialogState::Visible || m_buttons.empty())
		{
			return;
		}
		dismiss(m_buttons[static_cast<std::size_t>(m_focusIndex)].result);
	}

	/// @brief キャンセルキー（Escape）押下を処理する
	void onCancelPressed() noexcept
	{
		if (m_state != DialogState::Visible)
		{
			return;
		}
		dismiss(DialogResult::Cancel);
	}

	/// @brief フォーカスを左に移動する
	void onFocusLeft() noexcept
	{
		if (m_state != DialogState::Visible || m_buttons.empty())
		{
			return;
		}
		m_buttons[static_cast<std::size_t>(m_focusIndex)].focused = false;
		m_focusIndex = (m_focusIndex - 1 + static_cast<int>(m_buttons.size()))
		               % static_cast<int>(m_buttons.size());
		m_buttons[static_cast<std::size_t>(m_focusIndex)].focused = true;
	}

	/// @brief フォーカスを右に移動する
	void onFocusRight() noexcept
	{
		if (m_state != DialogState::Visible || m_buttons.empty())
		{
			return;
		}
		m_buttons[static_cast<std::size_t>(m_focusIndex)].focused = false;
		m_focusIndex = (m_focusIndex + 1) % static_cast<int>(m_buttons.size());
		m_buttons[static_cast<std::size_t>(m_focusIndex)].focused = true;
	}

	/// @brief ボタンインデックス指定でクリックする
	/// @param index ボタンインデックス
	void onButtonClicked(int index) noexcept
	{
		if (m_state != DialogState::Visible)
		{
			return;
		}
		if (index < 0 || index >= static_cast<int>(m_buttons.size()))
		{
			return;
		}
		dismiss(m_buttons[static_cast<std::size_t>(index)].result);
	}

	// ── 更新 ────────────────────────────────────────────────

	/// @brief 毎フレーム更新
	/// @param deltaTime 前フレームからの経過時間（秒）
	void update(float deltaTime) noexcept
	{
		switch (m_state)
		{
		case DialogState::Hidden:
			break;

		case DialogState::FadingIn:
		{
			const float duration = std::max(m_params.fadeInDuration, 0.001f);
			m_animProgress += deltaTime / duration;
			if (m_animProgress >= 1.0f)
			{
				m_animProgress = 1.0f;
				m_state = DialogState::Visible;
			}
			break;
		}

		case DialogState::Visible:
			break;

		case DialogState::FadingOut:
		{
			const float duration = std::max(m_params.fadeOutDuration, 0.001f);
			m_animProgress += deltaTime / duration;
			if (m_animProgress >= 1.0f)
			{
				m_animProgress = 1.0f;
				m_state = DialogState::Hidden;
				if (m_onResult)
				{
					m_onResult(m_result);
				}
			}
			break;
		}
		}
	}

	// ── コールバック ──────────────────────────────────────

	/// @brief 結果コールバックを設定する
	/// @param fn 結果通知関数
	void setOnResult(std::function<void(DialogResult)> fn)
	{
		m_onResult = std::move(fn);
	}

	// ── テーマ ──────────────────────────────────────────────

	/// @brief テーマを設定する
	/// @param theme UIテーマ
	void setTheme(const ui::UITheme& theme) noexcept { m_theme = theme; }

	/// @brief テーマを取得する
	[[nodiscard]] const ui::UITheme& theme() const noexcept { return m_theme; }

	// ── 状態クエリ ──────────────────────────────────────────

	/// @brief 表示状態を取得する
	[[nodiscard]] DialogState state() const noexcept { return m_state; }

	/// @brief 表示中（Hidden以外）かどうか
	[[nodiscard]] bool isVisible() const noexcept
	{
		return m_state != DialogState::Hidden;
	}

	/// @brief モーダルブロッキング中かどうか（入力を遮断すべきか）
	[[nodiscard]] bool isModal() const noexcept
	{
		return m_state != DialogState::Hidden;
	}

	/// @brief 結果を取得する
	[[nodiscard]] DialogResult result() const noexcept { return m_result; }

	/// @brief アニメーション進行度を取得する [0.0, 1.0]
	[[nodiscard]] float animProgress() const noexcept { return m_animProgress; }

	/// @brief 表示スケール（フェード中はイージング適用）を取得する
	[[nodiscard]] float displayScale() const noexcept
	{
		switch (m_state)
		{
		case DialogState::FadingIn:
			return easeOutBack(m_animProgress);
		case DialogState::FadingOut:
			return 1.0f - m_animProgress;
		case DialogState::Visible:
			return 1.0f;
		default:
			return 0.0f;
		}
	}

	/// @brief 表示アルファ（フェード中は線形補間）を取得する
	[[nodiscard]] float displayAlpha() const noexcept
	{
		switch (m_state)
		{
		case DialogState::FadingIn:
			return m_animProgress;
		case DialogState::FadingOut:
			return 1.0f - m_animProgress;
		case DialogState::Visible:
			return 1.0f;
		default:
			return 0.0f;
		}
	}

	/// @brief パラメータを取得する
	[[nodiscard]] const DialogParams& params() const noexcept { return m_params; }

	/// @brief ボタン一覧を取得する
	[[nodiscard]] const std::vector<DialogButton>& buttons() const noexcept
	{
		return m_buttons;
	}

	/// @brief フォーカス中のボタンインデックスを取得する
	[[nodiscard]] int focusIndex() const noexcept { return m_focusIndex; }

private:
	/// @brief ボタン配列を構築する
	void rebuildButtons()
	{
		m_buttons.clear();

		switch (m_params.buttons)
		{
		case DialogButtons::OkCancel:
			m_buttons.push_back(DialogButton{"OK", DialogResult::Ok, true});
			m_buttons.push_back(DialogButton{"Cancel", DialogResult::Cancel, false});
			break;

		case DialogButtons::YesNo:
			m_buttons.push_back(DialogButton{"Yes", DialogResult::Ok, true});
			m_buttons.push_back(DialogButton{"No", DialogResult::Cancel, false});
			break;

		case DialogButtons::YesNoCancel:
			m_buttons.push_back(DialogButton{"Yes", DialogResult::Ok, true});
			m_buttons.push_back(DialogButton{"No", DialogResult::Cancel, false});
			m_buttons.push_back(DialogButton{"Cancel", DialogResult::Third, false});
			break;
		}

		m_focusIndex = 0;
	}

	/// @brief easeOutBackイージング関数
	/// @param t 進行度 [0.0, 1.0]
	/// @return イージング適用後の値
	[[nodiscard]] static float easeOutBack(float t) noexcept
	{
		constexpr float c1 = 1.70158f;
		constexpr float c3 = c1 + 1.0f;
		const float t1 = t - 1.0f;
		return 1.0f + c3 * t1 * t1 * t1 + c1 * t1 * t1;
	}

	DialogParams m_params;                                    ///< 表示パラメータ
	DialogState m_state = DialogState::Hidden;                ///< 表示状態
	DialogResult m_result = DialogResult::None;               ///< 確定結果
	float m_animProgress = 0.0f;                              ///< アニメーション進行度
	int m_focusIndex = 0;                                     ///< フォーカス中のボタンインデックス
	std::vector<DialogButton> m_buttons;                      ///< ボタン配列
	ui::UITheme m_theme;                                      ///< UIテーマ
	std::function<void(DialogResult)> m_onResult;             ///< 結果コールバック
};

} // namespace mitiru::vn
