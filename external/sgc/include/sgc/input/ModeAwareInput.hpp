#pragma once

/// @file ModeAwareInput.hpp
/// @brief モード対応入力ラッパー
///
/// IInputProviderとInputModeManagerを組み合わせ、
/// 現在のモードに応じて入力を自動フィルタリングするラッパークラス。

#include "sgc/input/IInputProvider.hpp"
#include "sgc/input/InputModeManager.hpp"

namespace sgc
{

/// @brief モード対応の入力フィルタリングラッパー
///
/// IInputProviderの上にInputModeManagerを重ね、
/// ゲームプレイモード以外ではゲーム入力を無効化する。
///
/// @code
/// sgc::ModeAwareInput input(&rawProvider);
/// input.pushMode(sgc::InputMode::TextInput);
/// // isGameplayKeyDown() は false を返す
/// // isKeyDown() は通常通り動作
/// input.popMode();
/// @endcode
class ModeAwareInput
{
public:
	/// @brief コンストラクタ
	/// @param provider 基底の入力プロバイダー（非所有ポインタ）
	explicit ModeAwareInput(IInputProvider* provider) noexcept
		: m_provider{provider}
	{
	}

	/// @brief モードをプッシュする
	/// @param mode 追加するモード
	/// @return プッシュ成功ならtrue
	bool pushMode(InputMode mode)
	{
		return m_modes.pushMode(mode);
	}

	/// @brief 最上位モードをポップする
	void popMode() noexcept
	{
		m_modes.popMode();
	}

	/// @brief 現在のモードを直接設定する
	/// @param mode 設定するモード
	void setMode(InputMode mode) noexcept
	{
		m_modes.setMode(mode);
	}

	/// @brief 現在のモードを取得する
	[[nodiscard]] InputMode currentMode() const noexcept
	{
		return m_modes.currentMode();
	}

	/// @brief モードマネージャへの参照を取得する
	[[nodiscard]] const InputModeManager& modeManager() const noexcept
	{
		return m_modes;
	}

	/// @brief モードマネージャへの参照を取得する（変更用）
	[[nodiscard]] InputModeManager& modeManager() noexcept
	{
		return m_modes;
	}

	/// @brief 基底プロバイダーからキー状態を直接取得する（モード無視）
	/// @param keyCode キーコード
	/// @return 押下中ならtrue
	[[nodiscard]] bool isKeyDown(int keyCode) const noexcept
	{
		return m_provider ? m_provider->isKeyDown(keyCode) : false;
	}

	/// @brief ゲームプレイモード時のみキー状態を返す
	/// @param keyCode キーコード
	/// @return ゲームプレイモードかつ押下中ならtrue
	[[nodiscard]] bool isGameplayKeyDown(int keyCode) const noexcept
	{
		if (!m_modes.isGameplayInputEnabled()) return false;
		return m_provider ? m_provider->isKeyDown(keyCode) : false;
	}

	/// @brief ゲームプレイモード時のみキー押下イベントを返す
	/// @param keyCode キーコード
	/// @return ゲームプレイモードかつこのフレームで押されたならtrue
	[[nodiscard]] bool isGameplayKeyJustPressed(int keyCode) const noexcept
	{
		if (!m_modes.isGameplayInputEnabled()) return false;
		return m_provider ? m_provider->isKeyJustPressed(keyCode) : false;
	}

	/// @brief メニューモード時のみキー状態を返す
	/// @param keyCode キーコード
	/// @return メニューモードかつ押下中ならtrue
	[[nodiscard]] bool isMenuKeyDown(int keyCode) const noexcept
	{
		if (!m_modes.isMenu()) return false;
		return m_provider ? m_provider->isKeyDown(keyCode) : false;
	}

	/// @brief マウス座標を取得する（モードに関わらず常に取得可能）
	[[nodiscard]] Vec2f mousePosition() const noexcept
	{
		return m_provider ? m_provider->mousePosition() : Vec2f{0.0f, 0.0f};
	}

	/// @brief マウスボタンの押下状態を取得する
	/// @param button マウスボタン番号
	/// @return 押下中ならtrue
	[[nodiscard]] bool isMouseButtonDown(int button) const noexcept
	{
		return m_provider ? m_provider->isMouseButtonDown(button) : false;
	}

	/// @brief マウスボタンがこのフレームで押されたか取得する
	/// @param button マウスボタン番号
	/// @return このフレームで押されたならtrue
	[[nodiscard]] bool isMouseButtonPressed(int button) const noexcept
	{
		return m_provider ? m_provider->isMouseButtonPressed(button) : false;
	}

	/// @brief 基底プロバイダーを取得する
	[[nodiscard]] IInputProvider* provider() const noexcept
	{
		return m_provider;
	}

	/// @brief モードスタックをリセットしてGameplayに戻す
	void reset() noexcept
	{
		m_modes.reset();
	}

private:
	IInputProvider* m_provider = nullptr;
	InputModeManager m_modes;
};

} // namespace sgc
