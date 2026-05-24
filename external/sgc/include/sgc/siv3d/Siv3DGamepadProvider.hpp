#pragma once

/// @file Siv3DGamepadProvider.hpp
/// @brief IGamepadProvider の Siv3D XInput 実装
///
/// Siv3DのXInput APIを使用してゲームパッド入力を提供する。
/// Xbox系コントローラーを最大4台サポートする。
///
/// @note このファイルはSiv3D SDKに依存するため、CI対象外。
///
/// @code
/// sgc::siv3d::Siv3DGamepadProvider gamepad;
/// // メインループ内で:
/// gamepad.update();
/// if (gamepad.isButtonPressed(sgc::input::GamepadButton::A))
/// {
///     // ジャンプ処理
/// }
/// auto stick = gamepad.getLeftStick();
/// // stick.x, stick.y で移動処理
/// @endcode

#include <Siv3D.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "sgc/input/IGamepadProvider.hpp"

namespace sgc::siv3d
{

/// @brief IGamepadProvider の Siv3D XInput 実装
///
/// Siv3DのXInput APIをラップし、sgcのゲームパッドインターフェースに変換する。
/// update()を毎フレーム呼び出して状態を更新する必要がある。
class Siv3DGamepadProvider : public input::IGamepadProvider
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
			const auto& xi = s3d::XInput(i);
			auto& state = m_states[static_cast<size_t>(i)];
			state.connected = xi.isConnected();

			if (!state.connected) continue;

			// ボタン状態の取得
			state.buttons[idx(input::GamepadButton::A)] = xi.buttonA.pressed();
			state.buttons[idx(input::GamepadButton::B)] = xi.buttonB.pressed();
			state.buttons[idx(input::GamepadButton::X)] = xi.buttonX.pressed();
			state.buttons[idx(input::GamepadButton::Y)] = xi.buttonY.pressed();
			state.buttons[idx(input::GamepadButton::LeftBumper)] = xi.buttonLB.pressed();
			state.buttons[idx(input::GamepadButton::RightBumper)] = xi.buttonRB.pressed();
			state.buttons[idx(input::GamepadButton::Back)] = xi.buttonBack.pressed();
			state.buttons[idx(input::GamepadButton::Start)] = xi.buttonStart.pressed();
			state.buttons[idx(input::GamepadButton::Guide)] = xi.buttonMenu.pressed();
			state.buttons[idx(input::GamepadButton::LeftStick)] = xi.buttonLThumb.pressed();
			state.buttons[idx(input::GamepadButton::RightStick)] = xi.buttonRThumb.pressed();
			state.buttons[idx(input::GamepadButton::DpadUp)] = xi.buttonUp.pressed();
			state.buttons[idx(input::GamepadButton::DpadDown)] = xi.buttonDown.pressed();
			state.buttons[idx(input::GamepadButton::DpadLeft)] = xi.buttonLeft.pressed();
			state.buttons[idx(input::GamepadButton::DpadRight)] = xi.buttonRight.pressed();

			// 軸の値を取得
			state.axes[idx(input::GamepadAxis::LeftStickX)] = static_cast<float>(xi.leftThumbX);
			state.axes[idx(input::GamepadAxis::LeftStickY)] = static_cast<float>(xi.leftThumbY);
			state.axes[idx(input::GamepadAxis::RightStickX)] = static_cast<float>(xi.rightThumbX);
			state.axes[idx(input::GamepadAxis::RightStickY)] = static_cast<float>(xi.rightThumbY);
			state.axes[idx(input::GamepadAxis::LeftTrigger)] = static_cast<float>(xi.leftTrigger);
			state.axes[idx(input::GamepadAxis::RightTrigger)] = static_cast<float>(xi.rightTrigger);
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
		s3d::XInput(index).setLeftVibration(static_cast<double>(std::clamp(left, 0.0f, 1.0f)));
		s3d::XInput(index).setRightVibration(static_cast<double>(std::clamp(right, 0.0f, 1.0f)));
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

} // namespace sgc::siv3d
