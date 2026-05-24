#pragma once

/// @file DxLibGamepadProvider.hpp
/// @brief IGamepadProvider の DxLib XInput 実装
///
/// DxLibのXInput APIを使用してゲームパッド入力を提供する。
/// Xbox系コントローラーを最大4台サポートする。
///
/// @note このファイルはDxLib SDKに依存する。
///
/// @code
/// sgc::dxlib::DxLibGamepadProvider gamepad;
/// // メインループ内で:
/// gamepad.update();
/// if (gamepad.isButtonPressed(sgc::input::GamepadButton::A))
/// {
///     // ジャンプ処理
/// }
/// auto stick = gamepad.getLeftStick();
/// // stick.x, stick.y で移動処理
/// @endcode

#include "DxLib.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "sgc/input/IGamepadProvider.hpp"

namespace sgc::dxlib
{

/// @brief IGamepadProvider の DxLib XInput 実装
///
/// DxLibのGetJoypadXInputState APIをラップし、sgcのゲームパッドインターフェースに変換する。
/// update()を毎フレーム呼び出して状態を更新する必要がある。
class DxLibGamepadProvider : public input::IGamepadProvider
{
public:
	/// @brief サポートする最大コントローラー数
	static constexpr int32_t MAX_GAMEPADS = 4;

	/// @brief デフォルトのデッドゾーン閾値
	static constexpr float DEFAULT_DEADZONE = 0.15f;

	/// @brief 状態を更新する（毎フレーム呼び出す）
	///
	/// 前フレームの状態を保存し、XInputから最新の状態を取得する。
	void update() override
	{
		m_prevStates = m_states;

		for (int32_t i = 0; i < MAX_GAMEPADS; ++i)
		{
			auto& state = m_states[static_cast<size_t>(i)];
			XINPUT_STATE xinput{};
			const int result = GetJoypadXInputState(i + 1, &xinput);

			state.connected = (result == 0);
			if (!state.connected) continue;

			// ボタン状態の取得
			state.buttons[idx(input::GamepadButton::A)] = (xinput.Buttons[XINPUT_BUTTON_A] != 0);
			state.buttons[idx(input::GamepadButton::B)] = (xinput.Buttons[XINPUT_BUTTON_B] != 0);
			state.buttons[idx(input::GamepadButton::X)] = (xinput.Buttons[XINPUT_BUTTON_X] != 0);
			state.buttons[idx(input::GamepadButton::Y)] = (xinput.Buttons[XINPUT_BUTTON_Y] != 0);
			state.buttons[idx(input::GamepadButton::LeftBumper)] = (xinput.Buttons[XINPUT_BUTTON_LEFT_SHOULDER] != 0);
			state.buttons[idx(input::GamepadButton::RightBumper)] = (xinput.Buttons[XINPUT_BUTTON_RIGHT_SHOULDER] != 0);
			state.buttons[idx(input::GamepadButton::Back)] = (xinput.Buttons[XINPUT_BUTTON_BACK] != 0);
			state.buttons[idx(input::GamepadButton::Start)] = (xinput.Buttons[XINPUT_BUTTON_START] != 0);
			state.buttons[idx(input::GamepadButton::Guide)] = false;  // DxLib XInputではガイドボタン非対応
			state.buttons[idx(input::GamepadButton::LeftStick)] = (xinput.Buttons[XINPUT_BUTTON_LEFT_THUMB] != 0);
			state.buttons[idx(input::GamepadButton::RightStick)] = (xinput.Buttons[XINPUT_BUTTON_RIGHT_THUMB] != 0);
			state.buttons[idx(input::GamepadButton::DpadUp)] = (xinput.Buttons[XINPUT_BUTTON_DPAD_UP] != 0);
			state.buttons[idx(input::GamepadButton::DpadDown)] = (xinput.Buttons[XINPUT_BUTTON_DPAD_DOWN] != 0);
			state.buttons[idx(input::GamepadButton::DpadLeft)] = (xinput.Buttons[XINPUT_BUTTON_DPAD_LEFT] != 0);
			state.buttons[idx(input::GamepadButton::DpadRight)] = (xinput.Buttons[XINPUT_BUTTON_DPAD_RIGHT] != 0);

			// 軸の値を取得（-32768〜32767を-1.0〜1.0に正規化）
			constexpr float STICK_MAX = 32767.0f;
			state.axes[idx(input::GamepadAxis::LeftStickX)] = static_cast<float>(xinput.ThumbLX) / STICK_MAX;
			state.axes[idx(input::GamepadAxis::LeftStickY)] = static_cast<float>(xinput.ThumbLY) / STICK_MAX;
			state.axes[idx(input::GamepadAxis::RightStickX)] = static_cast<float>(xinput.ThumbRX) / STICK_MAX;
			state.axes[idx(input::GamepadAxis::RightStickY)] = static_cast<float>(xinput.ThumbRY) / STICK_MAX;

			// トリガー（0〜255を0.0〜1.0に正規化）
			constexpr float TRIGGER_MAX = 255.0f;
			state.axes[idx(input::GamepadAxis::LeftTrigger)] = static_cast<float>(xinput.LeftTrigger) / TRIGGER_MAX;
			state.axes[idx(input::GamepadAxis::RightTrigger)] = static_cast<float>(xinput.RightTrigger) / TRIGGER_MAX;
		}
	}

	/// @brief 接続状態を取得する
	[[nodiscard]] bool isConnected(int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return false;
		return m_states[static_cast<size_t>(index)].connected;
	}

	/// @brief ボタン押下中か
	[[nodiscard]] bool isButtonDown(input::GamepadButton button, int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return false;
		return m_states[static_cast<size_t>(index)].buttons[idx(button)];
	}

	/// @brief ボタンがこのフレームで押されたか
	[[nodiscard]] bool isButtonPressed(input::GamepadButton button, int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return false;
		const auto i = static_cast<size_t>(index);
		const auto b = idx(button);
		return m_states[i].buttons[b] && !m_prevStates[i].buttons[b];
	}

	/// @brief 軸の値を取得する（デッドゾーン適用済み）
	[[nodiscard]] float getAxis(input::GamepadAxis axis, int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return 0.0f;
		const auto& state = m_states[static_cast<size_t>(index)];
		const float raw = state.axes[idx(axis)];
		if (std::abs(raw) < state.deadzone)
		{
			return 0.0f;
		}
		return raw;
	}

	/// @brief 左スティックの入力ベクトルを取得する
	[[nodiscard]] Vec2f getLeftStick(int32_t index = 0) const override
	{
		return Vec2f{
			getAxis(input::GamepadAxis::LeftStickX, index),
			getAxis(input::GamepadAxis::LeftStickY, index)
		};
	}

	/// @brief 右スティックの入力ベクトルを取得する
	[[nodiscard]] Vec2f getRightStick(int32_t index = 0) const override
	{
		return Vec2f{
			getAxis(input::GamepadAxis::RightStickX, index),
			getAxis(input::GamepadAxis::RightStickY, index)
		};
	}

	/// @brief バイブレーションを設定する
	void setVibration(float left, float right, int32_t index = 0) override
	{
		if (!isValidIndex(index)) return;
		const int padId = index + 1;
		StartJoypadVibration(padId,
			static_cast<int>(std::clamp(left, 0.0f, 1.0f) * 1000.0f),
			-1);
		// 注: DxLibは左右モーター個別制御を標準APIで提供しないため、
		// ここでは左モーターの強度のみ使用する
		(void)right;
	}

	/// @brief 接続中のコントローラー数を取得する
	[[nodiscard]] int32_t connectedCount() const override
	{
		int32_t count = 0;
		for (const auto& s : m_states)
		{
			if (s.connected) ++count;
		}
		return count;
	}

	/// @brief デッドゾーンを設定する
	/// @param dz デッドゾーン閾値（0.0〜1.0）
	/// @param index コントローラーインデックス
	void setDeadzone(float dz, int32_t index = 0)
	{
		if (!isValidIndex(index)) return;
		m_states[static_cast<size_t>(index)].deadzone = std::clamp(dz, 0.0f, 1.0f);
	}

private:
	/// @brief インデックスが有効範囲内か判定する
	[[nodiscard]] static bool isValidIndex(int32_t index) noexcept
	{
		return index >= 0 && index < MAX_GAMEPADS;
	}

	/// @brief GamepadButtonの数値インデックスを取得する
	[[nodiscard]] static constexpr size_t idx(input::GamepadButton b) noexcept
	{
		return static_cast<size_t>(b);
	}

	/// @brief GamepadAxisの数値インデックスを取得する
	[[nodiscard]] static constexpr size_t idx(input::GamepadAxis a) noexcept
	{
		return static_cast<size_t>(a);
	}

	std::array<input::GamepadState, MAX_GAMEPADS> m_states{};
	std::array<input::GamepadState, MAX_GAMEPADS> m_prevStates{};
};

} // namespace sgc::dxlib
