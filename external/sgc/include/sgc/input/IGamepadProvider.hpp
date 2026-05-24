#pragma once

/// @file IGamepadProvider.hpp
/// @brief ゲームパッド入力プロバイダーインターフェース
///
/// フレームワーク非依存のゲームパッド/コントローラーAPIを定義する。
/// Xbox、PlayStation等の各種コントローラーに対応可能な抽象インターフェース。
///
/// @code
/// class XInputGamepad : public sgc::input::IGamepadProvider {
/// public:
///     void update() override { /* XInputの状態をポーリング */ }
///     bool isConnected(int32_t index) const override { return m_states[index].connected; }
///     // ...
/// };
/// @endcode

#include <array>
#include <cstdint>

#include "sgc/math/Vec2.hpp"

namespace sgc::input
{

/// @brief ゲームパッドのボタン識別子
enum class GamepadButton : int32_t
{
	A,             ///< Aボタン（Xbox） / Xボタン（PlayStation）
	B,             ///< Bボタン（Xbox） / ○ボタン（PlayStation）
	X,             ///< Xボタン（Xbox） / □ボタン（PlayStation）
	Y,             ///< Yボタン（Xbox） / △ボタン（PlayStation）
	LeftBumper,    ///< 左バンパー / L1
	RightBumper,   ///< 右バンパー / R1
	Back,          ///< Backボタン / Select
	Start,         ///< Startボタン / Options
	Guide,         ///< ガイドボタン / PSボタン
	LeftStick,     ///< 左スティック押し込み / L3
	RightStick,    ///< 右スティック押し込み / R3
	DpadUp,        ///< 十字キー上
	DpadDown,      ///< 十字キー下
	DpadLeft,      ///< 十字キー左
	DpadRight,     ///< 十字キー右
	Count          ///< ボタン数（番兵値）
};

/// @brief ゲームパッドの軸識別子
enum class GamepadAxis : int32_t
{
	LeftStickX,    ///< 左スティックX軸（-1.0〜+1.0）
	LeftStickY,    ///< 左スティックY軸（-1.0〜+1.0）
	RightStickX,   ///< 右スティックX軸（-1.0〜+1.0）
	RightStickY,   ///< 右スティックY軸（-1.0〜+1.0）
	LeftTrigger,   ///< 左トリガー（0.0〜+1.0）
	RightTrigger,  ///< 右トリガー（0.0〜+1.0）
	Count          ///< 軸数（番兵値）
};

/// @brief ゲームパッドの状態を保持する構造体
struct GamepadState
{
	/// @brief コントローラーが接続されているか
	bool connected = false;

	/// @brief 各ボタンの押下状態
	std::array<bool, static_cast<size_t>(GamepadButton::Count)> buttons{};

	/// @brief 各軸の値
	std::array<float, static_cast<size_t>(GamepadAxis::Count)> axes{};

	/// @brief デッドゾーンの閾値（この値以下の軸入力は0として扱う）
	float deadzone = 0.15f;
};

/// @brief ゲームパッド入力の抽象インターフェース
///
/// フレームワーク固有のゲームパッド実装（XInput、DirectInput、SDL等）は
/// このインターフェースを実装する。最大4台のコントローラーに対応。
class IGamepadProvider
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IGamepadProvider() = default;

	/// @brief ゲームパッドの状態を更新する（毎フレーム呼び出す）
	virtual void update() = 0;

	/// @brief 指定インデックスのコントローラーが接続されているか
	/// @param index コントローラーインデックス（0〜3）
	/// @return 接続されていればtrue
	[[nodiscard]] virtual bool isConnected(int32_t index = 0) const = 0;

	/// @brief ボタンが押下中か
	/// @param button ボタン識別子
	/// @param index コントローラーインデックス
	/// @return 押下中ならtrue
	[[nodiscard]] virtual bool isButtonDown(GamepadButton button, int32_t index = 0) const = 0;

	/// @brief ボタンがこのフレームで新たに押されたか
	/// @param button ボタン識別子
	/// @param index コントローラーインデックス
	/// @return このフレームで押されたならtrue
	[[nodiscard]] virtual bool isButtonPressed(GamepadButton button, int32_t index = 0) const = 0;

	/// @brief 軸の値を取得する（デッドゾーン適用済み）
	/// @param axis 軸識別子
	/// @param index コントローラーインデックス
	/// @return 軸の値（-1.0〜+1.0、トリガーは0.0〜+1.0）
	[[nodiscard]] virtual float getAxis(GamepadAxis axis, int32_t index = 0) const = 0;

	/// @brief 左スティックの入力を2Dベクトルで取得する
	/// @param index コントローラーインデックス
	/// @return 左スティックの入力ベクトル
	[[nodiscard]] virtual Vec2f getLeftStick(int32_t index = 0) const = 0;

	/// @brief 右スティックの入力を2Dベクトルで取得する
	/// @param index コントローラーインデックス
	/// @return 右スティックの入力ベクトル
	[[nodiscard]] virtual Vec2f getRightStick(int32_t index = 0) const = 0;

	/// @brief バイブレーションを設定する
	/// @param left 左モーターの強度（0.0〜1.0）
	/// @param right 右モーターの強度（0.0〜1.0）
	/// @param index コントローラーインデックス
	virtual void setVibration(float left, float right, int32_t index = 0) = 0;

	/// @brief 接続中のコントローラー数を取得する
	/// @return 接続台数
	[[nodiscard]] virtual int32_t connectedCount() const = 0;
};

} // namespace sgc::input
