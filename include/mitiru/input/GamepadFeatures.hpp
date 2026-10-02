#pragma once

/// @file GamepadFeatures.hpp
/// @brief InputSnapshot に載らないパッドの機能 (機種、電池、ジャイロ、タッチパッド、ライトバー、アダプティブトリガー) の型。
/// @details ゲーム DLL へは ABI v48 (ADR 0056) の InputSnapshot::gamepadsExt と FrameIntents::padOut で渡す。

#include <array>
#include <cstddef>
#include <cstdint>

namespace mitiru::input
{

/// ボタンの絵 (A/B か ×/○ か) を選ぶための機種。A ビットは機種によらず下のボタン (位置で決まる)
enum class PadKind : std::uint8_t
{
	Unknown, Standard, Xbox360, XboxOne, PS3, PS4, PS5,
	SwitchPro, JoyConLeft, JoyConRight, JoyConPair, GameCube,
};

enum class PadPower : std::uint8_t
{
	Unknown,
	OnBattery,
	Wired,      ///< 電池を持たない (有線専用)
	Charging,
	Charged,
};

struct PadInfo
{
	PadKind       kind = PadKind::Unknown;
	std::uint16_t vendorId = 0;
	std::uint16_t productId = 0;
	PadPower      power = PadPower::Unknown;
	std::int8_t   batteryPercent = -1;   ///< 分からなければ -1
	std::uint8_t  touchpadCount = 0;
	bool          hasGyro = false;
	bool          hasAccel = false;
	bool          hasRgbLed = false;
	bool          hasRumble = false;
	bool          hasTriggerRumble = false;
	bool          hasAdaptiveTriggers = false;
};

/// 単位は SDL のまま (ジャイロ rad/s、加速度 m/s^2)。読むには setMotionEnabled で先に有効にする
struct PadMotion
{
	std::array<float, 3> gyro{};
	std::array<float, 3> accel{};
	bool hasGyro = false;
	bool hasAccel = false;
};

/// x, y は 0..1 (左上が 0)
struct PadTouchFinger
{
	bool  down = false;
	float x = 0.0f;
	float y = 0.0f;
	float pressure = 0.0f;
};

enum class TriggerSide : std::uint8_t { Left, Right };

/// DualSense のアダプティブトリガー。Resistance と Vibration は SDL の testcontroller と同じ単純な型を使う
struct TriggerEffect
{
	enum class Mode : std::uint8_t { Off, Resistance, Vibration, Raw };

	Mode         mode = Mode::Off;
	std::uint8_t start = 0;       ///< 効き始める押し込み位置 0..255
	std::uint8_t strength = 0;    ///< Resistance は抵抗の強さ、Vibration は振幅
	std::uint8_t frequency = 0;   ///< Vibration だけ使う
	std::array<std::uint8_t, 11> raw{};  ///< Raw の時にそのまま送る

	[[nodiscard]] static TriggerEffect off() noexcept { return {}; }
	[[nodiscard]] static TriggerEffect resistance(std::uint8_t start, std::uint8_t strength) noexcept
	{
		TriggerEffect e;
		e.mode = Mode::Resistance;
		e.start = start;
		e.strength = strength;
		return e;
	}
	[[nodiscard]] static TriggerEffect vibration(std::uint8_t start, std::uint8_t amplitude,
	                                             std::uint8_t frequency) noexcept
	{
		TriggerEffect e;
		e.mode = Mode::Vibration;
		e.start = start;
		e.strength = amplitude;
		e.frequency = frequency;
		return e;
	}
};

/// DualSense の出力レポートでトリガー 1 本分が占める 11 バイト
[[nodiscard]] inline std::array<std::uint8_t, 11> encodeTriggerEffect(const TriggerEffect& e) noexcept
{
	std::array<std::uint8_t, 11> b{};
	switch (e.mode)
	{
	case TriggerEffect::Mode::Off:        b[0] = 0x05; break;
	case TriggerEffect::Mode::Resistance: b[0] = 0x01; b[1] = e.start; b[2] = e.strength; break;
	case TriggerEffect::Mode::Vibration:  b[0] = 0x06; b[1] = e.frequency; b[2] = e.strength; b[3] = e.start; break;
	case TriggerEffect::Mode::Raw:        b = e.raw; break;
	}
	return b;
}

/// SDL_SendGamepadEffect へ渡す DualSense の効果ブロック (SDL の DS5EffectsState_t、47 バイト)。
/// 指定した側のトリガーだけを書き換える印を立て、ライトや振動など他の欄には触れない
inline constexpr std::size_t kDualSenseEffectsSize = 47;
inline constexpr std::size_t kDualSenseRightTriggerOffset = 10;
inline constexpr std::size_t kDualSenseLeftTriggerOffset = 21;

[[nodiscard]] inline std::array<std::uint8_t, kDualSenseEffectsSize>
dualSenseTriggerPacket(TriggerSide side, const TriggerEffect& e) noexcept
{
	std::array<std::uint8_t, kDualSenseEffectsSize> p{};
	const bool right = side == TriggerSide::Right;
	p[0] = right ? 0x04 : 0x08;
	const auto bytes = encodeTriggerEffect(e);
	const std::size_t at = right ? kDualSenseRightTriggerOffset : kDualSenseLeftTriggerOffset;
	for (std::size_t i = 0; i < bytes.size(); ++i) { p[at + i] = bytes[i]; }
	return p;
}

}  // namespace mitiru::input
