#include <catch2/catch_test_macros.hpp>

#include "sgc/input/MockGamepadProvider.hpp"

using sgc::input::GamepadAxis;
using sgc::input::GamepadButton;
using sgc::input::MockGamepadProvider;

TEST_CASE("MockGamepadProvider - default state is disconnected", "[input][gamepad]")
{
	MockGamepadProvider pad;
	REQUIRE_FALSE(pad.isConnected(0));
	REQUIRE(pad.connectedCount() == 0);
}

TEST_CASE("MockGamepadProvider - connect and disconnect", "[input][gamepad]")
{
	MockGamepadProvider pad;
	pad.setConnected(0, true);
	pad.setConnected(2, true);

	REQUIRE(pad.isConnected(0));
	REQUIRE_FALSE(pad.isConnected(1));
	REQUIRE(pad.isConnected(2));
	REQUIRE(pad.connectedCount() == 2);

	pad.setConnected(0, false);
	REQUIRE_FALSE(pad.isConnected(0));
	REQUIRE(pad.connectedCount() == 1);
}

TEST_CASE("MockGamepadProvider - button down", "[input][gamepad]")
{
	MockGamepadProvider pad;
	pad.setConnected(0, true);
	pad.setButtonDown(GamepadButton::A, true, 0);
	pad.setButtonDown(GamepadButton::X, true, 0);

	REQUIRE(pad.isButtonDown(GamepadButton::A, 0));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::B, 0));
	REQUIRE(pad.isButtonDown(GamepadButton::X, 0));
}

TEST_CASE("MockGamepadProvider - button pressed detection", "[input][gamepad]")
{
	MockGamepadProvider pad;
	pad.setConnected(0, true);
	pad.update();  // save initial state as prev

	pad.setButtonDown(GamepadButton::A, true, 0);

	// A was not down last frame, is down now => pressed
	REQUIRE(pad.isButtonPressed(GamepadButton::A, 0));

	pad.update();  // now prev also has A down

	// A was down last frame too => not "just pressed"
	REQUIRE_FALSE(pad.isButtonPressed(GamepadButton::A, 0));
	REQUIRE(pad.isButtonDown(GamepadButton::A, 0));
}

TEST_CASE("MockGamepadProvider - axis with deadzone", "[input][gamepad]")
{
	MockGamepadProvider pad;
	pad.setConnected(0, true);
	pad.setDeadzone(0.2f, 0);

	// Value below deadzone => 0
	pad.setAxis(GamepadAxis::LeftStickX, 0.1f, 0);
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 0) == 0.0f);

	// Value above deadzone => raw value
	pad.setAxis(GamepadAxis::LeftStickX, 0.8f, 0);
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, 0) == 0.8f);

	// Negative value above deadzone magnitude
	pad.setAxis(GamepadAxis::LeftStickY, -0.5f, 0);
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickY, 0) == -0.5f);
}

TEST_CASE("MockGamepadProvider - stick vectors", "[input][gamepad]")
{
	MockGamepadProvider pad;
	pad.setConnected(0, true);
	pad.setAxis(GamepadAxis::LeftStickX, 0.5f, 0);
	pad.setAxis(GamepadAxis::LeftStickY, -0.3f, 0);
	pad.setAxis(GamepadAxis::RightStickX, 1.0f, 0);
	pad.setAxis(GamepadAxis::RightStickY, 0.0f, 0);

	auto left = pad.getLeftStick(0);
	REQUIRE(left.x == 0.5f);
	REQUIRE(left.y == -0.3f);

	auto right = pad.getRightStick(0);
	REQUIRE(right.x == 1.0f);
	REQUIRE(right.y == 0.0f);
}

TEST_CASE("MockGamepadProvider - vibration", "[input][gamepad]")
{
	MockGamepadProvider pad;
	pad.setConnected(0, true);
	pad.setVibration(0.5f, 0.8f, 0);

	auto vib = pad.getVibration(0);
	REQUIRE(vib.x == 0.5f);
	REQUIRE(vib.y == 0.8f);

	// Clamping test
	pad.setVibration(1.5f, -0.5f, 0);
	vib = pad.getVibration(0);
	REQUIRE(vib.x == 1.0f);
	REQUIRE(vib.y == 0.0f);
}

TEST_CASE("MockGamepadProvider - invalid index returns safe defaults", "[input][gamepad]")
{
	MockGamepadProvider pad;
	REQUIRE_FALSE(pad.isConnected(-1));
	REQUIRE_FALSE(pad.isConnected(4));
	REQUIRE_FALSE(pad.isButtonDown(GamepadButton::A, 5));
	REQUIRE(pad.getAxis(GamepadAxis::LeftStickX, -1) == 0.0f);

	auto stick = pad.getLeftStick(10);
	REQUIRE(stick.x == 0.0f);
	REQUIRE(stick.y == 0.0f);
}
