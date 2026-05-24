#pragma once

/// @file MockGamepadProvider.hpp
/// @brief テスト用ゲームパッドプロバイダー
///
/// プログラム的にゲームパッド状態を設定できるモック実装。
/// ユニットテストやAI制御シミュレーションに使用する。
///
/// @code
/// sgc::input::MockGamepadProvider mock;
/// mock.setConnected(0, true);
/// mock.setButtonDown(sgc::input::GamepadButton::A, true, 0);
/// mock.setAxis(sgc::input::GamepadAxis::LeftStickX, 0.8f, 0);
/// mock.update();
///
/// REQUIRE(mock.isConnected(0));
/// REQUIRE(mock.isButtonDown(sgc::input::GamepadButton::A, 0));
/// @endcode

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "sgc/input/IGamepadProvider.hpp"

namespace sgc::input
{

/// @brief テスト用モックゲームパッドプロバイダー
///
/// 最大4台のコントローラーをシミュレートする。
/// setXxx()メソッドで状態を設定し、update()で前フレーム状態を更新する。
class MockGamepadProvider : public IGamepadProvider
{
public:
	/// @brief サポートする最大コントローラー数
	static constexpr int32_t MAX_GAMEPADS = 4;

	/// @brief 状態を更新する（前フレーム状態を保存）
	void update() override
	{
		m_prevStates = m_states;
	}

	/// @brief 接続状態を取得する
	[[nodiscard]] bool isConnected(int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return false;
		return m_states[static_cast<size_t>(index)].connected;
	}

	/// @brief ボタン押下中か
	[[nodiscard]] bool isButtonDown(GamepadButton button, int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return false;
		return m_states[static_cast<size_t>(index)].buttons[static_cast<size_t>(button)];
	}

	/// @brief ボタンがこのフレームで押されたか（前フレームで非押下→今フレームで押下）
	[[nodiscard]] bool isButtonPressed(GamepadButton button, int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return false;
		const auto idx = static_cast<size_t>(index);
		const auto btn = static_cast<size_t>(button);
		return m_states[idx].buttons[btn] && !m_prevStates[idx].buttons[btn];
	}

	/// @brief 軸の値を取得する（デッドゾーン適用済み）
	[[nodiscard]] float getAxis(GamepadAxis axis, int32_t index = 0) const override
	{
		if (!isValidIndex(index)) return 0.0f;
		const auto& state = m_states[static_cast<size_t>(index)];
		const float raw = state.axes[static_cast<size_t>(axis)];
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
			getAxis(GamepadAxis::LeftStickX, index),
			getAxis(GamepadAxis::LeftStickY, index)
		};
	}

	/// @brief 右スティックの入力ベクトルを取得する
	[[nodiscard]] Vec2f getRightStick(int32_t index = 0) const override
	{
		return Vec2f{
			getAxis(GamepadAxis::RightStickX, index),
			getAxis(GamepadAxis::RightStickY, index)
		};
	}

	/// @brief バイブレーションを設定する（モックでは値を保存するのみ）
	void setVibration(float left, float right, int32_t index = 0) override
	{
		if (!isValidIndex(index)) return;
		const auto idx = static_cast<size_t>(index);
		m_vibrationLeft[idx] = std::clamp(left, 0.0f, 1.0f);
		m_vibrationRight[idx] = std::clamp(right, 0.0f, 1.0f);
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

	// ─── テスト用設定メソッド ───

	/// @brief 接続状態を設定する
	/// @param index コントローラーインデックス
	/// @param connected 接続状態
	void setConnected(int32_t index, bool connected)
	{
		if (!isValidIndex(index)) return;
		m_states[static_cast<size_t>(index)].connected = connected;
	}

	/// @brief ボタン押下状態を設定する
	/// @param button ボタン識別子
	/// @param down 押下状態
	/// @param index コントローラーインデックス
	void setButtonDown(GamepadButton button, bool down, int32_t index = 0)
	{
		if (!isValidIndex(index)) return;
		m_states[static_cast<size_t>(index)].buttons[static_cast<size_t>(button)] = down;
	}

	/// @brief 軸の値を設定する
	/// @param axis 軸識別子
	/// @param value 軸の値
	/// @param index コントローラーインデックス
	void setAxis(GamepadAxis axis, float value, int32_t index = 0)
	{
		if (!isValidIndex(index)) return;
		m_states[static_cast<size_t>(index)].axes[static_cast<size_t>(axis)] = value;
	}

	/// @brief デッドゾーンを設定する
	/// @param dz デッドゾーン閾値
	/// @param index コントローラーインデックス
	void setDeadzone(float dz, int32_t index = 0)
	{
		if (!isValidIndex(index)) return;
		m_states[static_cast<size_t>(index)].deadzone = dz;
	}

	/// @brief 設定されたバイブレーション値を取得する（テスト検証用）
	/// @param index コントローラーインデックス
	/// @return {左モーター, 右モーター}
	[[nodiscard]] Vec2f getVibration(int32_t index = 0) const
	{
		if (!isValidIndex(index)) return Vec2f{0.0f, 0.0f};
		const auto idx = static_cast<size_t>(index);
		return Vec2f{m_vibrationLeft[idx], m_vibrationRight[idx]};
	}

private:
	/// @brief インデックスが有効範囲内か判定する
	[[nodiscard]] static bool isValidIndex(int32_t index) noexcept
	{
		return index >= 0 && index < MAX_GAMEPADS;
	}

	std::array<GamepadState, MAX_GAMEPADS> m_states{};
	std::array<GamepadState, MAX_GAMEPADS> m_prevStates{};
	std::array<float, MAX_GAMEPADS> m_vibrationLeft{};
	std::array<float, MAX_GAMEPADS> m_vibrationRight{};
};

} // namespace sgc::input
