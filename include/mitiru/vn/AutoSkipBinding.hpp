#pragma once

/// @file AutoSkipBinding.hpp
/// @brief AutoSkipController と MessageWindow を結線する glue helper
///
/// **Why.** `AutoSkipController` は「テキスト表示完了」「行開始」を
/// `notifyTextComplete()` / `notifyNewLine()` で明示的に知らされないと
/// 何もしない。`MessageWindow` はこれらの通知を自動発火しない。
/// 従ってゲーム側で毎フレーム両方の状態を見て適切なタイミングで通知し、
/// `update()` が返した `AdvanceAction` に応じて `window.advance()` を
/// 呼ぶ必要があった。このボイラープレートを消す。
///
/// **Usage:**
/// ```cpp
///   mitiru::vn::MessageWindow window(...);
///   mitiru::vn::AutoSkipController ctrl;
///   mitiru::vn::AutoSkipBinding binding(ctrl, window);
///
///   // 毎フレーム
///   window.update(dt);
///   const auto action = binding.tick(dt);
///   if (action == mitiru::vn::AdvanceAction::Advance) {
///       // binding が既に window.advance() を呼んでいる。game 側で追加処理あれば
///   }
///
///   // setText するときは直接呼ばず bind 経由で
///   binding.setText("アリス", "こんにちは！");
/// ```

#include <string>

#include <mitiru/vn/AutoSkipController.hpp>
#include <mitiru/vn/MessageWindow.hpp>

namespace mitiru::vn
{

/// @brief AutoSkipController ↔ MessageWindow 結線
/// @details 参照で両方を保持する非所有 binding。tick(dt) が
///          状態遷移検出 → 通知 → update → advance の全 glue を担当する。
class AutoSkipBinding
{
public:
	/// @brief コンストラクタ
	/// @param controller AutoSkipController (非所有)
	/// @param window MessageWindow (非所有)
	AutoSkipBinding(AutoSkipController& controller, MessageWindow& window) noexcept
		: m_controller(&controller)
		, m_window(&window)
		, m_lastState(window.state())
	{
	}

	/// @brief 毎フレーム呼ぶ
	/// @param deltaTime 前フレームからの経過時間 (秒)
	/// @return controller.update() が返した action。Advance 時は window.advance()
	///         を既に呼んであるので、game 側で重ねて呼ぶ必要はない。
	AdvanceAction tick(float deltaTime) noexcept
	{
		// 状態遷移検出: Displaying → WaitingClick で「テキスト完了」
		const auto curState = m_window->state();
		if (curState != m_lastState)
		{
			if (curState == MessageWindowState::WaitingClick)
			{
				m_controller->notifyTextComplete();
			}
			m_lastState = curState;
		}

		const auto action = m_controller->update(deltaTime);
		if (action == AdvanceAction::Advance || action == AdvanceAction::Skip)
		{
			m_window->advance();
		}
		return action;
	}

	/// @brief テキストを表示開始する (controller へ notifyNewLine を自動送信)
	/// @param speaker 話者名
	/// @param text テキスト
	/// @param label スクリプト上の現在ラベル (既読判定用、任意)
	/// @param lineIndex 現在行番号 (既読判定用、任意)
	void setText(const std::string& speaker, const std::string& text,
		const std::string& label = {}, int lineIndex = -1)
	{
		m_window->setText(speaker, text);
		m_controller->notifyNewLine(text.size(), label, lineIndex);
		m_lastState = m_window->state();
	}

	/// @brief 選択肢表示 (controller に通知して自動送りを止める)
	void notifyChoiceAppeared() noexcept
	{
		m_controller->notifyChoiceAppeared();
	}

	/// @brief 選択肢消失 (自動送りを再開可能にする)
	void notifyChoiceDismissed() noexcept
	{
		m_controller->notifyChoiceDismissed();
	}

	/// @brief プレイヤー入力でキャンセル (auto/skip を Idle に戻す)
	void cancelByUserInput() noexcept
	{
		m_controller->cancelByUserInput();
	}

	/// @brief 直近キャッシュされた window 状態を取得する (テスト用)
	[[nodiscard]] MessageWindowState lastObservedState() const noexcept
	{
		return m_lastState;
	}

	/// @brief controller にアクセスする
	[[nodiscard]] AutoSkipController& controller() noexcept { return *m_controller; }

	/// @brief window にアクセスする
	[[nodiscard]] MessageWindow& window() noexcept { return *m_window; }

private:
	AutoSkipController* m_controller = nullptr;   ///< 非所有
	MessageWindow* m_window = nullptr;             ///< 非所有
	MessageWindowState m_lastState;                ///< 前フレームの window 状態 (遷移検出用)
};

} // namespace mitiru::vn
