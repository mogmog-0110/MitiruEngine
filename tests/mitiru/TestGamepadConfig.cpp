/// @file TestGamepadConfig.cpp
/// @brief ゲームパッド関連のユニットテスト（プラットフォーム非依存部分）
/// @details GamepadButtonId enum、デッドゾーン計算、振動クランプのテスト。
///          実XInput APIは使用しない。

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "mitiru/input/InputMapper.hpp"

// ============================================================
// GamepadButtonId enum tests
// ============================================================

TEST_CASE("GamepadConfig - GamepadButtonId has correct XInput values", "[mitiru][input][gamepad]")
{
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::DPadUp) == 0x0001);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::DPadDown) == 0x0002);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::DPadLeft) == 0x0004);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::DPadRight) == 0x0008);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::Start) == 0x0010);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::Back) == 0x0020);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::LS) == 0x0040);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::RS) == 0x0080);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::LB) == 0x0100);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::RB) == 0x0200);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::A) == 0x1000);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::B) == 0x2000);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::X) == 0x4000);
	CHECK(static_cast<std::uint16_t>(mitiru::GamepadButtonId::Y) == 0x8000);
}

// ============================================================
// Dead zone calculation tests (pure math, no XInput)
// ============================================================

/// @brief テスト用デッドゾーン適用関数（GamepadInput::applyDeadZoneと同一ロジック）
static float testApplyDeadZone(float value, float threshold)
{
	const float absVal = std::abs(value);
	if (absVal < threshold)
	{
		return 0.0f;
	}
	const float sign = (value >= 0.0f) ? 1.0f : -1.0f;
	return sign * (absVal - threshold) / (1.0f - threshold);
}

TEST_CASE("GamepadConfig - dead zone suppresses small values", "[mitiru][input][gamepad]")
{
	const float threshold = 0.25f;

	CHECK(testApplyDeadZone(0.0f, threshold) == 0.0f);
	CHECK(testApplyDeadZone(0.1f, threshold) == 0.0f);
	CHECK(testApplyDeadZone(-0.1f, threshold) == 0.0f);
	CHECK(testApplyDeadZone(0.24f, threshold) == 0.0f);
	CHECK(testApplyDeadZone(-0.24f, threshold) == 0.0f);
}

TEST_CASE("GamepadConfig - dead zone passes through values above threshold", "[mitiru][input][gamepad]")
{
	const float threshold = 0.25f;

	/// 閾値ちょうどで0に近い値
	const float atThreshold = testApplyDeadZone(0.25f, threshold);
	CHECK(atThreshold == Catch::Approx(0.0f).margin(0.01f));

	/// 最大値で1.0
	CHECK(testApplyDeadZone(1.0f, threshold) == Catch::Approx(1.0f));
	CHECK(testApplyDeadZone(-1.0f, threshold) == Catch::Approx(-1.0f));

	/// 中間値のリマッピング
	const float mid = testApplyDeadZone(0.625f, threshold);
	CHECK(mid == Catch::Approx(0.5f));

	/// 負の中間値
	const float negMid = testApplyDeadZone(-0.625f, threshold);
	CHECK(negMid == Catch::Approx(-0.5f));
}

TEST_CASE("GamepadConfig - dead zone with zero threshold is passthrough", "[mitiru][input][gamepad]")
{
	CHECK(testApplyDeadZone(0.5f, 0.0f) == Catch::Approx(0.5f));
	CHECK(testApplyDeadZone(-0.3f, 0.0f) == Catch::Approx(-0.3f));
	CHECK(testApplyDeadZone(0.0f, 0.0f) == 0.0f);
}

// ============================================================
// Stick normalization tests (pure math)
// ============================================================

/// @brief テスト用スティック正規化（GamepadInput::normalizeStickと同一ロジック）
static float testNormalizeStick(short raw)
{
	if (raw >= 0)
	{
		return static_cast<float>(raw) / 32767.0f;
	}
	return static_cast<float>(raw) / 32768.0f;
}

/// @brief テスト用トリガー正規化（GamepadInput::normalizeTriggerと同一ロジック）
static float testNormalizeTrigger(std::uint8_t raw)
{
	return static_cast<float>(raw) / 255.0f;
}

TEST_CASE("GamepadConfig - stick normalization range", "[mitiru][input][gamepad]")
{
	CHECK(testNormalizeStick(0) == 0.0f);
	CHECK(testNormalizeStick(32767) == Catch::Approx(1.0f));
	CHECK(testNormalizeStick(-32768) == Catch::Approx(-1.0f));
	CHECK(testNormalizeStick(16383) == Catch::Approx(0.5f).margin(0.001f));
}

TEST_CASE("GamepadConfig - trigger normalization range", "[mitiru][input][gamepad]")
{
	CHECK(testNormalizeTrigger(0) == 0.0f);
	CHECK(testNormalizeTrigger(255) == Catch::Approx(1.0f));
	CHECK(testNormalizeTrigger(127) == Catch::Approx(0.498f).margin(0.01f));
}

// ============================================================
// Vibration clamping tests (pure math)
// ============================================================

TEST_CASE("GamepadConfig - vibration values clamp to 0-1 range", "[mitiru][input][gamepad]")
{
	/// std::clampで [0, 1] にクランプされることを確認
	CHECK(std::clamp(-0.5f, 0.0f, 1.0f) == 0.0f);
	CHECK(std::clamp(0.5f, 0.0f, 1.0f) == 0.5f);
	CHECK(std::clamp(1.5f, 0.0f, 1.0f) == 1.0f);
	CHECK(std::clamp(0.0f, 0.0f, 1.0f) == 0.0f);
	CHECK(std::clamp(1.0f, 0.0f, 1.0f) == 1.0f);
}

TEST_CASE("GamepadConfig - vibration motor speed conversion to uint16", "[mitiru][input][gamepad]")
{
	/// [0.0, 1.0] → [0, 65535] の変換テスト
	auto toMotorSpeed = [](float value) -> std::uint16_t
	{
		return static_cast<std::uint16_t>(
			std::clamp(value, 0.0f, 1.0f) * 65535.0f);
	};

	CHECK(toMotorSpeed(0.0f) == 0);
	CHECK(toMotorSpeed(1.0f) == 65535);
	CHECK(toMotorSpeed(0.5f) == 32767);
	CHECK(toMotorSpeed(-1.0f) == 0);
	CHECK(toMotorSpeed(2.0f) == 65535);
}
