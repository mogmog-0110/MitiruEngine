#pragma once

/// @file GamepadInput.hpp
/// @brief XInputゲームパッド入力
/// @details XInput APIを使用したゲームパッド入力の取得・管理。
///          最大4プレイヤー対応、デッドゾーン処理、振動制御を提供する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mitiru
{

/// @brief ゲームパッドボタン
enum class GamepadButton : std::uint16_t
{
	DPadUp    = XINPUT_GAMEPAD_DPAD_UP,       ///< 十字キー上
	DPadDown  = XINPUT_GAMEPAD_DPAD_DOWN,     ///< 十字キー下
	DPadLeft  = XINPUT_GAMEPAD_DPAD_LEFT,     ///< 十字キー左
	DPadRight = XINPUT_GAMEPAD_DPAD_RIGHT,    ///< 十字キー右
	Start     = XINPUT_GAMEPAD_START,         ///< Startボタン
	Back      = XINPUT_GAMEPAD_BACK,          ///< Backボタン
	LS        = XINPUT_GAMEPAD_LEFT_THUMB,    ///< 左スティック押し込み
	RS        = XINPUT_GAMEPAD_RIGHT_THUMB,   ///< 右スティック押し込み
	LB        = XINPUT_GAMEPAD_LEFT_SHOULDER, ///< 左バンパー
	RB        = XINPUT_GAMEPAD_RIGHT_SHOULDER,///< 右バンパー
	A         = XINPUT_GAMEPAD_A,             ///< Aボタン
	B         = XINPUT_GAMEPAD_B,             ///< Bボタン
	X         = XINPUT_GAMEPAD_X,             ///< Xボタン
	Y         = XINPUT_GAMEPAD_Y              ///< Yボタン
};

/// @brief ゲームパッド軸
enum class GamepadAxis : std::uint8_t
{
	LeftStickX = 0,    ///< 左スティックX軸
	LeftStickY = 1,    ///< 左スティックY軸
	RightStickX = 2,   ///< 右スティックX軸
	RightStickY = 3,   ///< 右スティックY軸
	LeftTrigger = 4,   ///< 左トリガー
	RightTrigger = 5   ///< 右トリガー
};

/// @brief XInputゲームパッド入力ハンドラ
/// @details 最大4プレイヤーのゲームパッド状態を毎フレームポーリングする。
///          デッドゾーン処理とボタンエッジ検出（pressed/released）を提供する。
///
/// @code
/// mitiru::GamepadInput gamepad;
/// gamepad.setDeadZone(0.2f);
///
/// // 毎フレーム呼ぶ
/// gamepad.update();
///
/// if (gamepad.isConnected(0))
/// {
///     float lx = gamepad.getAxis(0, mitiru::GamepadAxis::LeftStickX);
///     if (gamepad.isButtonDown(0, mitiru::GamepadButton::A))
///         jump();
/// }
/// @endcode
class GamepadInput
{
public:
	/// @brief 最大プレイヤー数（XInputの上限）
	static constexpr int MAX_PLAYERS = 4;

	/// @brief デフォルトデッドゾーン閾値
	static constexpr float DEFAULT_DEAD_ZONE = 0.24f;

	/// @brief デフォルトコンストラクタ
	GamepadInput() noexcept = default;

	/// @brief 全プレイヤーの状態をポーリングする
	/// @details 毎フレームのゲームループ先頭で呼び出す。
	void update() noexcept
	{
		for (int i = 0; i < MAX_PLAYERS; ++i)
		{
			m_previousState[static_cast<std::size_t>(i)] =
				m_currentState[static_cast<std::size_t>(i)];

			XINPUT_STATE state = {};
			const DWORD result = XInputGetState(
				static_cast<DWORD>(i), &state);

			const auto idx = static_cast<std::size_t>(i);
			m_connected[idx] = (result == ERROR_SUCCESS);

			if (m_connected[idx])
			{
				m_currentState[idx] = state;
			}
			else
			{
				m_currentState[idx] = {};
			}
		}
	}

	/// @brief 指定プレイヤーのゲームパッドが接続されているか
	/// @param playerIndex プレイヤーインデックス（0 ~ 3）
	/// @return 接続されていれば true
	[[nodiscard]] bool isConnected(int playerIndex) const noexcept
	{
		if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return false;
		return m_connected[static_cast<std::size_t>(playerIndex)];
	}

	/// @brief 軸の値を取得する
	/// @param playerIndex プレイヤーインデックス（0 ~ 3）
	/// @param axis 軸の種類
	/// @return 軸の値 [-1.0, 1.0]（トリガーは [0.0, 1.0]）
	[[nodiscard]] float getAxis(int playerIndex, GamepadAxis axis) const noexcept
	{
		if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return 0.0f;
		if (!m_connected[static_cast<std::size_t>(playerIndex)]) return 0.0f;

		const auto& gp = m_currentState[static_cast<std::size_t>(playerIndex)].Gamepad;

		switch (axis)
		{
		case GamepadAxis::LeftStickX:
			return applyDeadZone(
				normalizeStick(gp.sThumbLX), m_deadZone);
		case GamepadAxis::LeftStickY:
			return applyDeadZone(
				normalizeStick(gp.sThumbLY), m_deadZone);
		case GamepadAxis::RightStickX:
			return applyDeadZone(
				normalizeStick(gp.sThumbRX), m_deadZone);
		case GamepadAxis::RightStickY:
			return applyDeadZone(
				normalizeStick(gp.sThumbRY), m_deadZone);
		case GamepadAxis::LeftTrigger:
			return normalizeTrigger(gp.bLeftTrigger);
		case GamepadAxis::RightTrigger:
			return normalizeTrigger(gp.bRightTrigger);
		default:
			return 0.0f;
		}
	}

	/// @brief ボタンが押されているか
	/// @param playerIndex プレイヤーインデックス（0 ~ 3）
	/// @param button ボタン
	/// @return 押されていれば true
	[[nodiscard]] bool isButtonDown(int playerIndex, GamepadButton button) const noexcept
	{
		if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return false;
		if (!m_connected[static_cast<std::size_t>(playerIndex)]) return false;

		const auto& gp = m_currentState[static_cast<std::size_t>(playerIndex)].Gamepad;
		return (gp.wButtons & static_cast<WORD>(button)) != 0;
	}

	/// @brief ボタンが今フレームで押されたか（エッジ検出）
	/// @param playerIndex プレイヤーインデックス（0 ~ 3）
	/// @param button ボタン
	/// @return 今フレーム押下 かつ 前フレーム非押下なら true
	[[nodiscard]] bool isButtonPressed(int playerIndex, GamepadButton button) const noexcept
	{
		if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return false;
		const auto idx = static_cast<std::size_t>(playerIndex);
		if (!m_connected[idx]) return false;

		const WORD mask = static_cast<WORD>(button);
		const bool current = (m_currentState[idx].Gamepad.wButtons & mask) != 0;
		const bool previous = (m_previousState[idx].Gamepad.wButtons & mask) != 0;
		return current && !previous;
	}

	/// @brief ボタンが今フレームで離されたか（エッジ検出）
	/// @param playerIndex プレイヤーインデックス（0 ~ 3）
	/// @param button ボタン
	/// @return 今フレーム非押下 かつ 前フレーム押下なら true
	[[nodiscard]] bool isButtonReleased(int playerIndex, GamepadButton button) const noexcept
	{
		if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return false;
		const auto idx = static_cast<std::size_t>(playerIndex);
		if (!m_connected[idx]) return false;

		const WORD mask = static_cast<WORD>(button);
		const bool current = (m_currentState[idx].Gamepad.wButtons & mask) != 0;
		const bool previous = (m_previousState[idx].Gamepad.wButtons & mask) != 0;
		return !current && previous;
	}

	/// @brief 振動を設定する
	/// @param playerIndex プレイヤーインデックス（0 ~ 3）
	/// @param leftMotor 左モーター強度 [0.0, 1.0]
	/// @param rightMotor 右モーター強度 [0.0, 1.0]
	void setVibration(int playerIndex, float leftMotor, float rightMotor) noexcept
	{
		if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return;

		XINPUT_VIBRATION vibration = {};
		vibration.wLeftMotorSpeed = static_cast<WORD>(
			std::clamp(leftMotor, 0.0f, 1.0f) * 65535.0f);
		vibration.wRightMotorSpeed = static_cast<WORD>(
			std::clamp(rightMotor, 0.0f, 1.0f) * 65535.0f);
		XInputSetState(static_cast<DWORD>(playerIndex), &vibration);
	}

	/// @brief デッドゾーン閾値を設定する
	/// @param threshold デッドゾーン閾値 [0.0, 1.0]
	void setDeadZone(float threshold) noexcept
	{
		m_deadZone = std::clamp(threshold, 0.0f, 1.0f);
	}

	/// @brief 現在のデッドゾーン閾値を取得する
	/// @return デッドゾーン閾値 [0.0, 1.0]
	[[nodiscard]] float deadZone() const noexcept
	{
		return m_deadZone;
	}

	// ── 静的ユーティリティ（テスト用に公開） ────────────

	/// @brief スティック値を [-1.0, 1.0] に正規化する
	/// @param raw 生のスティック値（SHORT: -32768 ~ 32767）
	/// @return 正規化された値
	[[nodiscard]] static float normalizeStick(SHORT raw) noexcept
	{
		if (raw >= 0)
		{
			return static_cast<float>(raw) / 32767.0f;
		}
		return static_cast<float>(raw) / 32768.0f;
	}

	/// @brief トリガー値を [0.0, 1.0] に正規化する
	/// @param raw 生のトリガー値（BYTE: 0 ~ 255）
	/// @return 正規化された値
	[[nodiscard]] static float normalizeTrigger(BYTE raw) noexcept
	{
		return static_cast<float>(raw) / 255.0f;
	}

	/// @brief デッドゾーンを適用する
	/// @param value 入力値 [-1.0, 1.0]
	/// @param threshold デッドゾーン閾値
	/// @return デッドゾーン適用後の値
	[[nodiscard]] static float applyDeadZone(float value, float threshold) noexcept
	{
		const float absVal = std::abs(value);
		if (absVal < threshold)
		{
			return 0.0f;
		}
		/// デッドゾーン外の範囲を [0, 1] に再マッピング
		const float sign = (value >= 0.0f) ? 1.0f : -1.0f;
		return sign * (absVal - threshold) / (1.0f - threshold);
	}

private:
	std::array<XINPUT_STATE, MAX_PLAYERS> m_currentState{};   ///< 現在のゲームパッド状態
	std::array<XINPUT_STATE, MAX_PLAYERS> m_previousState{};  ///< 前フレームの状態
	std::array<bool, MAX_PLAYERS> m_connected{};              ///< 接続状態
	float m_deadZone = DEFAULT_DEAD_ZONE;                     ///< デッドゾーン閾値
};

} // namespace mitiru

#endif // _WIN32
