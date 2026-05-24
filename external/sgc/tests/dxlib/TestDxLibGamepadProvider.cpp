/// @file TestDxLibGamepadProvider.cpp
/// @brief DxLibGamepadProvider adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/dxlib/DxLibGamepadProvider.hpp"

using sgc::input::GamepadAxis;
using sgc::input::GamepadButton;
using sgc::dxlib::DxLibGamepadProvider;
using Catch::Approx;

namespace
{

/// @brief テスト前にDxLibスタブをリセットするフィクスチャ
struct DxLibFixture
{
	DxLibFixture() { dxlib_stub::reset(); }
};

} // anonymous namespace

// ── 接続テスト ──────────────────────────────────────────

TEST_CASE("DxLibGamepadProvider - default state is disconnected", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	DxLibGamepadProvider pad;
	pad.update();

	REQUIRE_FALSE(pad.isConnected(0));
	REQUIRE(pad.connectedCount() == 0);
}

TEST_CASE("DxLibGamepadProvider - detects connected controller", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[2].connected = true;

	DxLibGamepadProvider pad;
	pad.update();

	REQUIRE(pad.isConnected(0));
	REQUIRE_FALSE(pad.isConnected(1));
	REQUIRE(pad.isConnected(2));
	REQUIRE(pad.connectedCount() == 2);
}

TEST_CASE("DxLibGamepadProvider - invalid index returns safe defaults", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	DxLibGamepadProvider pad;

	REQUIRE_FALSE(pad.isConnected(-1));
	REQUIRE_FALSE(pad.isConnected(4));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::A, -1));
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 5) == Approx(0.0f));
}

// ── ボタンテスト ────────────────────────────────────────

TEST_CASE("DxLibGamepadProvider - reads button A state", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].Buttons[XINPUT_BUTTON_A] = 1;

	DxLibGamepadProvider pad;
	pad.update();

	REQUIRE(pad.isButtonDown(GamepadButton::A, 0));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::B, 0));
}

TEST_CASE("DxLibGamepadProvider - reads multiple buttons", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].Buttons[XINPUT_BUTTON_X] = 1;
	dxlib_stub::xinputStates()[0].Buttons[XINPUT_BUTTON_Y] = 1;
	dxlib_stub::xinputStates()[0].Buttons[XINPUT_BUTTON_DPAD_UP] = 1;

	DxLibGamepadProvider pad;
	pad.update();

	REQUIRE(pad.isButtonDown(GamepadButton::X, 0));
	REQUIRE(pad.isButtonDown(GamepadButton::Y, 0));
	REQUIRE(pad.isButtonDown(GamepadButton::DpadUp, 0));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::A, 0));
}

// ── ボタンプレス検出テスト ──────────────────────────────

TEST_CASE("DxLibGamepadProvider - detects button pressed this frame", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;

	DxLibGamepadProvider pad;
	pad.update();  // 初期状態を保存

	// 次フレームでAボタンを押す
	dxlib_stub::xinputStates()[0].Buttons[XINPUT_BUTTON_A] = 1;
	pad.update();

	REQUIRE(pad.isButtonPressed(GamepadButton::A, 0));

	// 次フレームでもAが押されたまま → pressedはfalse
	pad.update();
	REQUIRE_FALSE(pad.isButtonPressed(GamepadButton::A, 0));
}

// ── 軸テスト ────────────────────────────────────────────

TEST_CASE("DxLibGamepadProvider - reads stick axes", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].ThumbLX = 16383;  // ~0.5
	dxlib_stub::xinputStates()[0].ThumbLY = -32767;  // ~-1.0

	DxLibGamepadProvider pad;
	pad.update();

	const float lx = pad.getAxis(GamepadAxis::LeftStickX, 0);
	const float ly = pad.getAxis(GamepadAxis::LeftStickY, 0);

	REQUIRE(lx == Approx(16383.0f / 32767.0f).margin(0.01f));
	REQUIRE(ly == Approx(-1.0f).margin(0.01f));
}

TEST_CASE("DxLibGamepadProvider - deadzone filters small values", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].ThumbLX = 1000;  // ~0.03 (under default 0.15 deadzone)

	DxLibGamepadProvider pad;
	pad.update();

	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 0) == Approx(0.0f));
}

TEST_CASE("DxLibGamepadProvider - reads trigger axes", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].LeftTrigger = 255;
	dxlib_stub::xinputStates()[0].RightTrigger = 128;

	DxLibGamepadProvider pad;
	pad.update();

	REQUIRE(pad.getAxis(GamepadAxis::LeftTrigger, 0) == Approx(1.0f).margin(0.01f));
	REQUIRE(pad.getAxis(GamepadAxis::RightTrigger, 0) == Approx(128.0f / 255.0f).margin(0.01f));
}

// ── スティックベクトルテスト ────────────────────────────

TEST_CASE("DxLibGamepadProvider - getLeftStick returns vector", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].ThumbLX = 32767;
	dxlib_stub::xinputStates()[0].ThumbLY = -32767;

	DxLibGamepadProvider pad;
	pad.update();

	const auto stick = pad.getLeftStick(0);
	REQUIRE(stick.x == Approx(1.0f).margin(0.01f));
	REQUIRE(stick.y == Approx(-1.0f).margin(0.01f));
}

TEST_CASE("DxLibGamepadProvider - getRightStick returns vector", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].ThumbRX = -16383;
	dxlib_stub::xinputStates()[0].ThumbRY = 16383;

	DxLibGamepadProvider pad;
	pad.update();

	const auto stick = pad.getRightStick(0);
	REQUIRE(stick.x == Approx(-0.5f).margin(0.01f));
	REQUIRE(stick.y == Approx(0.5f).margin(0.01f));
}

// ── バイブレーションテスト ──────────────────────────────

TEST_CASE("DxLibGamepadProvider - setVibration records call", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;

	DxLibGamepadProvider pad;
	pad.setVibration(0.5f, 0.8f, 0);

	REQUIRE(dxlib_stub::vibrationRecords().size() == 1);
	REQUIRE(dxlib_stub::vibrationRecords()[0].padId == 1);
	REQUIRE(dxlib_stub::vibrationRecords()[0].power == 500);
}

// ── デッドゾーン設定テスト ──────────────────────────────

TEST_CASE("DxLibGamepadProvider - setDeadzone changes threshold", "[dxlib][gamepad]")
{
	DxLibFixture fix;
	dxlib_stub::xinputStates()[0].connected = true;
	dxlib_stub::xinputStates()[0].ThumbLX = 6553;  // ~0.2

	DxLibGamepadProvider pad;
	pad.setDeadzone(0.3f, 0);
	pad.update();

	// 0.2 < 0.3 deadzone → should be 0
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 0) == Approx(0.0f));

	pad.setDeadzone(0.1f, 0);
	// 0.2 > 0.1 deadzone → should pass through
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 0) == Approx(6553.0f / 32767.0f).margin(0.01f));
}
