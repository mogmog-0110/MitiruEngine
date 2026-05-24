#pragma once

/// @file InputModeManager.hpp
/// @brief 入力モードスタック管理
///
/// ゲームプレイ中のテキスト入力、メニュー操作、ポーズ画面など
/// 複数の入力モードを安全に切り替えるためのスタック型マネージャ。
///
/// @code
/// sgc::InputModeManager modes;
/// modes.setMode(sgc::InputMode::Gameplay);
///
/// // テキストフィールドにフォーカス:
/// modes.pushMode(sgc::InputMode::TextInput);
/// assert(!modes.isGameplay());
///
/// // テキスト入力完了:
/// modes.popMode();
/// assert(modes.isGameplay());
/// @endcode

#include <cstdint>
#include <vector>

namespace sgc
{

/// @brief 入力モードの種類
enum class InputMode : std::uint8_t
{
	Gameplay,    ///< ゲームプレイ（移動、ジャンプ等）
	TextInput,   ///< テキスト入力（キーボードをテキストフィールドに使用）
	Menu,        ///< メニュー操作（カーソル移動、選択）
	Cutscene,    ///< カットシーン（入力無効または限定）
	Disabled,    ///< 入力完全無効
};

/// @brief 入力モードスタックマネージャ
///
/// モードをスタックで管理し、一時的なモード切替（メニューを開く等）と
/// 復帰（メニューを閉じる等）を安全に行う。
///
/// スタックが空の場合、デフォルトモード（Gameplay）が返る。
class InputModeManager
{
public:
	/// @brief スタックの最大深さ
	static constexpr std::size_t MAX_DEPTH = 16;

	/// @brief 現在のモードを直接設定する（スタックをクリアする）
	/// @param mode 設定するモード
	void setMode(InputMode mode) noexcept
	{
		m_stack.clear();
		m_stack.push_back(mode);
	}

	/// @brief モードをスタックにプッシュする
	///
	/// スタックの深さがMAX_DEPTHに達している場合はプッシュしない。
	///
	/// @param mode 追加するモード
	/// @return プッシュに成功した場合true、深さ上限に達していた場合false
	bool pushMode(InputMode mode)
	{
		if (m_stack.size() >= MAX_DEPTH)
		{
			return false;
		}
		m_stack.push_back(mode);
		return true;
	}

	/// @brief 最上位のモードをポップする
	///
	/// スタックが空（または残り1つ）の場合はポップしない。
	void popMode() noexcept
	{
		if (m_stack.size() > 1)
		{
			m_stack.pop_back();
		}
	}

	/// @brief 現在のモードを取得する
	/// @return スタック最上位のモード（空ならGameplay）
	[[nodiscard]] InputMode currentMode() const noexcept
	{
		return m_stack.empty() ? InputMode::Gameplay : m_stack.back();
	}

	/// @brief ゲームプレイモードか
	[[nodiscard]] bool isGameplay() const noexcept
	{
		return currentMode() == InputMode::Gameplay;
	}

	/// @brief テキスト入力モードか
	[[nodiscard]] bool isTextInput() const noexcept
	{
		return currentMode() == InputMode::TextInput;
	}

	/// @brief メニューモードか
	[[nodiscard]] bool isMenu() const noexcept
	{
		return currentMode() == InputMode::Menu;
	}

	/// @brief 入力が有効か（Disabled以外）
	[[nodiscard]] bool isInputEnabled() const noexcept
	{
		return currentMode() != InputMode::Disabled;
	}

	/// @brief ゲーム操作が有効か（Gameplay時のみ）
	[[nodiscard]] bool isGameplayInputEnabled() const noexcept
	{
		return currentMode() == InputMode::Gameplay;
	}

	/// @brief スタックの深さを取得する
	[[nodiscard]] std::size_t depth() const noexcept
	{
		return m_stack.size();
	}

	/// @brief スタックをクリアしてデフォルト状態に戻す
	void reset() noexcept
	{
		m_stack.clear();
		m_stack.push_back(InputMode::Gameplay);
	}

private:
	std::vector<InputMode> m_stack{InputMode::Gameplay};
};

} // namespace sgc
