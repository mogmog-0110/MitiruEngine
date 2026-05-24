#pragma once

/// @file PendingAction.hpp
/// @brief Immediate GUI統合パターン用のペンディングアクションユーティリティ
///
/// Siv3DのSimpleGUIのようにdraw()内で入力と描画を同時に行うIMGUIライブラリと、
/// sgcのupdate()/draw()分離アーキテクチャを橋渡しするパターンを提供する。
///
/// ## 設計意図
///
/// sgcのAppSceneは `update(dt)` で状態更新、`draw() const` で描画を行う。
/// しかしIMGUIはdraw()内でボタンクリック等の入力を処理する。
/// この不一致を解決するために、draw()内で発生したアクションを
/// `mutable` なPendingActionに記録し、次フレームのupdate()で消費するパターンを使う。
///
/// @code
/// class MyScene : public AppScene {
///     mutable sgc::ui::PendingAction<> m_confirm;
///     mutable sgc::ui::PendingAction<std::string> m_textSubmit;
///
///     void update(float dt) override {
///         if (m_confirm.consume()) { doConfirm(); }
///         if (auto text = m_textSubmit.consumeValue()) {
///             processText(*text);
///         }
///     }
///
///     void draw() const override {
///         if (SimpleGUI::Button(U"OK", pos)) {
///             m_confirm.trigger();
///         }
///         if (KeyEnter.down()) {
///             m_textSubmit.trigger(m_input.text.toUTF8());
///         }
///     }
/// };
/// @endcode

#include <optional>
#include <utility>

namespace sgc::ui
{

/// @brief 値なしペンディングアクション（ボタン押下等のシンプルなトリガー）
///
/// draw()内で`trigger()`し、update()内で`consume()`するパターンを
/// 型安全に表現する。mutableメンバとして宣言して使う。
///
/// @code
/// mutable sgc::ui::PendingAction<> m_confirm;
/// // draw() 内:  m_confirm.trigger();
/// // update() 内: if (m_confirm.consume()) { ... }
/// @endcode
template <typename T = void>
class PendingAction;

/// @brief void特殊化: 値を持たないシンプルなトリガー
///
/// @note このクラスはシングルスレッド前提で設計されている。マルチスレッド環境では外部同期が必要。
template <>
class PendingAction<void>
{
public:
	/// @brief アクションをトリガーする（draw()内で呼ぶ、constメソッド）
	void trigger() const noexcept { m_triggered = true; }

	/// @brief トリガーを消費する（update()内で呼ぶ）
	/// @return トリガーされていたらtrue
	[[nodiscard]] bool consume() noexcept
	{
		if (!m_triggered) return false;
		m_triggered = false;
		return true;
	}

	/// @brief トリガー済みか確認する（消費しない）
	[[nodiscard]] bool isPending() const noexcept { return m_triggered; }

	/// @brief トリガーをリセットする
	void reset() noexcept { m_triggered = false; }

private:
	mutable bool m_triggered = false;
};

/// @brief 値付きペンディングアクション（テキスト送信等）
///
/// draw()内で値と共に`trigger(value)`し、
/// update()内で`consumeValue()`で取り出す。
///
/// @tparam T 保持する値の型
///
/// @note このクラスはシングルスレッド前提で設計されている。マルチスレッド環境では外部同期が必要。
///
/// @code
/// mutable sgc::ui::PendingAction<std::string> m_submit;
/// // draw() 内:  m_submit.trigger(inputText);
/// // update() 内: if (auto val = m_submit.consumeValue()) { process(*val); }
/// @endcode
template <typename T>
class PendingAction
{
public:
	/// @brief 値と共にアクションをトリガーする（draw()内で呼ぶ、constメソッド）
	/// @param value 送信する値
	void trigger(T value) const { m_value = std::move(value); }

	/// @brief トリガーを消費して値を取得する（update()内で呼ぶ）
	/// @return トリガーされていたら値を返す
	[[nodiscard]] std::optional<T> consumeValue()
	{
		if (!m_value.has_value()) return std::nullopt;
		std::optional<T> result = std::move(m_value);
		m_value.reset();
		return result;
	}

	/// @brief トリガー済みか確認する（消費しない）
	[[nodiscard]] bool isPending() const noexcept { return m_value.has_value(); }

	/// @brief トリガーをリセットする
	void reset() { m_value.reset(); }

private:
	mutable std::optional<T> m_value;
};

} // namespace sgc::ui
