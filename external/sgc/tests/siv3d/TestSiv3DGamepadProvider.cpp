#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/siv3d/Siv3DGamepadProvider.hpp"

using sgc::input::GamepadAxis;
using sgc::input::GamepadButton;
using sgc::siv3d::Siv3DGamepadProvider;

namespace
{

/// @brief テスト前にXInputスタブをリセットするヘルパー
struct XInputFixture
{
	XInputFixture()
	{
		siv3d_stub::reset();
		siv3d_stub::resetXInput();
	}
};

} // namespace

TEST_CASE("Siv3DGamepadProvider - default state is disconnected", "[siv3d][gamepad]")
{
	XInputFixture fix;
	Siv3DGamepadProvider pad;
	pad.update();

	REQUIRE_FALSE(pad.isConnected(0));
	REQUIRE(pad.connectedCount() == 0);
}

TEST_CASE("Siv3DGamepadProvider - detects connected controller", "[siv3d][gamepad]")
{
	XInputFixture fix;
	s3d::XInput(0).m_connected = true;
	s3d::XInput(2).m_connected = true;

	Siv3DGamepadProvider pad;
	pad.update();

	REQUIRE(pad.isConnected(0));
	REQUIRE_FALSE(pad.isConnected(1));
	REQUIRE(pad.isConnected(2));
	REQUIRE(pad.connectedCount() == 2);
}

TEST_CASE("Siv3DGamepadProvider - reads button state", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;
	siv3d_stub::keyPressed()[xi.buttonA.id] = true;
	siv3d_stub::keyPressed()[xi.buttonX.id] = true;

	Siv3DGamepadProvider pad;
	pad.update();

	REQUIRE(pad.isButtonDown(GamepadButton::A, 0));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::B, 0));
	REQUIRE(pad.isButtonDown(GamepadButton::X, 0));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::Y, 0));
}

TEST_CASE("Siv3DGamepadProvider - detects button press edge", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;

	Siv3DGamepadProvider pad;
	pad.update(); // フレーム0: ボタンなし

	// フレーム1: Aボタンを押す
	siv3d_stub::keyPressed()[xi.buttonA.id] = true;
	pad.update();

	REQUIRE(pad.isButtonPressed(GamepadButton::A, 0));

	// フレーム2: Aボタン継続押下
	pad.update();

	REQUIRE_FALSE(pad.isButtonPressed(GamepadButton::A, 0));
	REQUIRE(pad.isButtonDown(GamepadButton::A, 0));
}

TEST_CASE("Siv3DGamepadProvider - reads stick axes", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;
	xi.leftThumbX = 0.75;
	xi.leftThumbY = -0.5;
	xi.rightThumbX = 1.0;
	xi.rightThumbY = 0.0;

	Siv3DGamepadProvider pad;
	pad.update();

	auto left = pad.getLeftStick(0);
	REQUIRE_THAT(left.x, Catch::Matchers::WithinAbs(0.75f, 0.01f));
	REQUIRE_THAT(left.y, Catch::Matchers::WithinAbs(-0.5f, 0.01f));

	auto right = pad.getRightStick(0);
	REQUIRE_THAT(right.x, Catch::Matchers::WithinAbs(1.0f, 0.01f));
	REQUIRE_THAT(right.y, Catch::Matchers::WithinAbs(0.0f, 0.01f));
}

TEST_CASE("Siv3DGamepadProvider - deadzone filters small values", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;
	xi.leftThumbX = 0.1; // Below default deadzone (0.15)

	Siv3DGamepadProvider pad;
	pad.update();

	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 0) == 0.0f);

	// Above deadzone
	xi.leftThumbX = 0.5;
	pad.update();

	REQUIRE_THAT(pad.getAxis(GamepadAxis::LeftStickX, 0),
		Catch::Matchers::WithinAbs(0.5f, 0.01f));
}

TEST_CASE("Siv3DGamepadProvider - custom deadzone", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;
	xi.leftThumbX = 0.25;

	Siv3DGamepadProvider pad;
	pad.setDeadzone(0.3f, 0);
	pad.update();

	// 0.25 < 0.3 deadzone => filtered to 0
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 0) == 0.0f);
}

TEST_CASE("Siv3DGamepadProvider - reads triggers", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;
	xi.leftTrigger = 0.8;
	xi.rightTrigger = 0.3;

	Siv3DGamepadProvider pad;
	pad.update();

	REQUIRE_THAT(pad.getAxis(GamepadAxis::LeftTrigger, 0),
		Catch::Matchers::WithinAbs(0.8f, 0.01f));
	REQUIRE_THAT(pad.getAxis(GamepadAxis::RightTrigger, 0),
		Catch::Matchers::WithinAbs(0.3f, 0.01f));
}

TEST_CASE("Siv3DGamepadProvider - vibration forwards to XInput", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;

	Siv3DGamepadProvider pad;
	pad.setVibration(0.5f, 0.8f, 0);

	REQUIRE_THAT(xi.m_vibLeft, Catch::Matchers::WithinAbs(0.5, 0.01));
	REQUIRE_THAT(xi.m_vibRight, Catch::Matchers::WithinAbs(0.8, 0.01));

	// Clamping test
	pad.setVibration(1.5f, -0.5f, 0);
	REQUIRE_THAT(xi.m_vibLeft, Catch::Matchers::WithinAbs(1.0, 0.01));
	REQUIRE_THAT(xi.m_vibRight, Catch::Matchers::WithinAbs(0.0, 0.01));
}

TEST_CASE("Siv3DGamepadProvider - dpad buttons", "[siv3d][gamepad]")
{
	XInputFixture fix;
	auto& xi = s3d::XInput(0);
	xi.m_connected = true;
	siv3d_stub::keyPressed()[xi.buttonUp.id] = true;
	siv3d_stub::keyPressed()[xi.buttonRight.id] = true;

	Siv3DGamepadProvider pad;
	pad.update();

	REQUIRE(pad.isButtonDown(GamepadButton::DpadUp, 0));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::DpadDown, 0));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::DpadLeft, 0));
	REQUIRE(pad.isButtonDown(GamepadButton::DpadRight, 0));
}

TEST_CASE("Siv3DGamepadProvider - invalid index returns safe defaults", "[siv3d][gamepad]")
{
	XInputFixture fix;
	Siv3DGamepadProvider pad;

	REQUIRE_FALSE(pad.isConnected(-1));
	REQUIRE_FALSE(pad.isConnected(4));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::A, 5));
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, -1) == 0.0f);

	auto stick = pad.getLeftStick(10);
	REQUIRE(stick.x == 0.0f);
	REQUIRE(stick.y == 0.0f);
}
