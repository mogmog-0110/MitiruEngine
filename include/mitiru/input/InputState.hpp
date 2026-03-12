#pragma once

/// @file InputState.hpp
/// @brief 不変入力状態スナップショット
/// @details あるフレームにおける入力デバイスの状態を保持する。
///          イミュータブルな値型として設計。

#include <array>
#include <cstdint>

namespace mitiru
{

/// @brief マウスボタン識別子
enum class MouseButton : std::uint8_t
{
	Left = 0,    ///< 左ボタン
	Right = 1,   ///< 右ボタン
	Middle = 2   ///< 中ボタン
};

/// @brief 不変の入力状態スナップショット
/// @details フレーム開始時に確定した入力状態を保持する。
///          ゲームロジックはこのスナップショットを参照して処理する。
class InputState
{
public:
	/// @brief キーの最大数
	static constexpr int MAX_KEYS = 256;

	/// @brief マウスボタンの最大数
	static constexpr int MAX_MOUSE_BUTTONS = 3;

	/// @brief デフォルトコンストラクタ（全入力なし状態）
	InputState() noexcept
		: m_keys{}
		, m_prevKeys{}
		, m_mouseButtons{}
		, m_prevMouseButtons{}
		, m_mouseX(0.0f)
		, m_mouseY(0.0f)
	{
	}

	/// @brief 指定キーが押されているか
	/// @param keyCode キーコード（0 ~ MAX_KEYS-1）
	/// @return 押されていれば true
	[[nodiscard]] bool isKeyDown(int keyCode) const noexcept
	{
		if (keyCode < 0 || keyCode >= MAX_KEYS)
		{
			return false;
		}
		return m_keys[static_cast<std::size_t>(keyCode)];
	}

	/// @brief マウス座標を取得する
	/// @return {x, y} のペア
	[[nodiscard]] std::pair<float, float> mousePosition() const noexcept
	{
		return { m_mouseX, m_mouseY };
	}

	/// @brief フレーム開始時に呼び出し、現在の状態を前フレーム状態にコピーする
	/// @details 各フレームの入力処理前に呼ぶことでエッジ検出が正しく機能する。
	void beginFrame() noexcept
	{
		m_prevKeys = m_keys;
		m_prevMouseButtons = m_mouseButtons;
	}

	/// @brief 指定キーがこのフレームで押されたか（エッジ検出）
	/// @param keyCode キーコード（0 ~ MAX_KEYS-1）
	/// @return 今フレーム押下かつ前フレーム非押下なら true
	[[nodiscard]] bool isKeyJustPressed(int keyCode) const noexcept
	{
		if (keyCode < 0 || keyCode >= MAX_KEYS)
		{
			return false;
		}
		const auto idx = static_cast<std::size_t>(keyCode);
		return m_keys[idx] && !m_prevKeys[idx];
	}

	/// @brief 指定キーがこのフレームで離されたか（エッジ検出）
	/// @param keyCode キーコード（0 ~ MAX_KEYS-1）
	/// @return 今フレーム非押下かつ前フレーム押下なら true
	[[nodiscard]] bool isKeyJustReleased(int keyCode) const noexcept
	{
		if (keyCode < 0 || keyCode >= MAX_KEYS)
		{
			return false;
		}
		const auto idx = static_cast<std::size_t>(keyCode);
		return !m_keys[idx] && m_prevKeys[idx];
	}

	/// @brief マウスボタンがこのフレームで押されたか（エッジ検出）
	/// @param button マウスボタン
	/// @return 今フレーム押下かつ前フレーム非押下なら true
	[[nodiscard]] bool isMouseButtonJustPressed(MouseButton button) const noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index >= MAX_MOUSE_BUTTONS)
		{
			return false;
		}
		return m_mouseButtons[index] && !m_prevMouseButtons[index];
	}

	/// @brief マウスボタンがこのフレームで離されたか（エッジ検出）
	/// @param button マウスボタン
	/// @return 今フレーム非押下かつ前フレーム押下なら true
	[[nodiscard]] bool isMouseButtonJustReleased(MouseButton button) const noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index >= MAX_MOUSE_BUTTONS)
		{
			return false;
		}
		return !m_mouseButtons[index] && m_prevMouseButtons[index];
	}

	/// @brief マウスボタンが押されているか
	/// @param button マウスボタン
	/// @return 押されていれば true
	[[nodiscard]] bool isMouseButtonDown(MouseButton button) const noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index >= MAX_MOUSE_BUTTONS)
		{
			return false;
		}
		return m_mouseButtons[index];
	}

	/// @brief キー状態を設定する
	/// @param keyCode キーコード
	/// @param down 押下状態
	void setKeyDown(int keyCode, bool down) noexcept
	{
		if (keyCode >= 0 && keyCode < MAX_KEYS)
		{
			m_keys[static_cast<std::size_t>(keyCode)] = down;
		}
	}

	/// @brief マウス座標を設定する
	/// @param x X座標
	/// @param y Y座標
	void setMousePosition(float x, float y) noexcept
	{
		m_mouseX = x;
		m_mouseY = y;
	}

	/// @brief マウスボタン状態を設定する
	/// @param button マウスボタン
	/// @param down 押下状態
	void setMouseButtonDown(MouseButton button, bool down) noexcept
	{
		const auto index = static_cast<std::size_t>(button);
		if (index < MAX_MOUSE_BUTTONS)
		{
			m_mouseButtons[index] = down;
		}
	}

private:
	std::array<bool, MAX_KEYS> m_keys;                    ///< キー押下状態
	std::array<bool, MAX_KEYS> m_prevKeys;                ///< 前フレームのキー押下状態
	std::array<bool, MAX_MOUSE_BUTTONS> m_mouseButtons;   ///< マウスボタン押下状態
	std::array<bool, MAX_MOUSE_BUTTONS> m_prevMouseButtons; ///< 前フレームのマウスボタン押下状態
	float m_mouseX;   ///< マウスX座標
	float m_mouseY;   ///< マウスY座標
};

} // namespace mitiru
