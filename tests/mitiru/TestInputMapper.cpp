/// @file TestInputMapper.cpp
/// @brief InputMapperのユニットテスト
/// @details アクションバインディング、複数バインディング、軸マッピングのテスト。

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <set>
#include <string>

#include "mitiru/input/InputMapper.hpp"

// ============================================================
// Binding management tests
// ============================================================

TEST_CASE("InputMapper - bindKey adds binding", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);

	CHECK(mapper.bindingCount("Jump") == 1);
	CHECK(mapper.actionCount() == 1);
}

TEST_CASE("InputMapper - multiple bindings for same action", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);
	mapper.bindKey("Jump", mitiru::KeyCode::W);
	mapper.bindGamepadButton("Jump", mitiru::GamepadButtonId::A);

	CHECK(mapper.bindingCount("Jump") == 3);
	CHECK(mapper.actionCount() == 1);
}

TEST_CASE("InputMapper - different actions are independent", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);
	mapper.bindKey("Fire", mitiru::KeyCode::F);
	mapper.bindMouseButton("Fire", 0);

	CHECK(mapper.bindingCount("Jump") == 1);
	CHECK(mapper.bindingCount("Fire") == 2);
	CHECK(mapper.actionCount() == 2);
}

TEST_CASE("InputMapper - unbindAll removes all bindings for action", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);
	mapper.bindKey("Jump", mitiru::KeyCode::W);
	mapper.bindKey("Fire", mitiru::KeyCode::F);

	mapper.unbindAll("Jump");

	CHECK(mapper.bindingCount("Jump") == 0);
	CHECK(mapper.bindingCount("Fire") == 1);
	CHECK(mapper.actionCount() == 1);
}

TEST_CASE("InputMapper - clearAllBindings removes everything", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);
	mapper.bindKey("Fire", mitiru::KeyCode::F);
	mapper.bindMouseButton("Aim", 1);

	mapper.clearAllBindings();

	CHECK(mapper.actionCount() == 0);
	CHECK(mapper.bindingCount("Jump") == 0);
	CHECK(mapper.bindingCount("Fire") == 0);
	CHECK(mapper.bindingCount("Aim") == 0);
}

TEST_CASE("InputMapper - bindingCount for unregistered action is 0", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	CHECK(mapper.bindingCount("NonExistent") == 0);
}

// ============================================================
// getBindings tests
// ============================================================

TEST_CASE("InputMapper - getBindings returns correct binding types", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);
	mapper.bindMouseButton("Jump", 0);
	mapper.bindGamepadButton("Jump", mitiru::GamepadButtonId::A);
	mapper.bindGamepadAxis("MoveX", 0);

	const auto& jumpBindings = mapper.getBindings("Jump");
	REQUIRE(jumpBindings.size() == 3);
	CHECK(jumpBindings[0].type == mitiru::BindingType::Key);
	CHECK(jumpBindings[0].code == static_cast<int>(mitiru::KeyCode::Space));
	CHECK(jumpBindings[1].type == mitiru::BindingType::MouseButton);
	CHECK(jumpBindings[1].code == 0);
	CHECK(jumpBindings[2].type == mitiru::BindingType::GamepadButton);

	const auto& moveBindings = mapper.getBindings("MoveX");
	REQUIRE(moveBindings.size() == 1);
	CHECK(moveBindings[0].type == mitiru::BindingType::GamepadAxis);
}

TEST_CASE("InputMapper - getBindings for unregistered action returns empty", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	const auto& bindings = mapper.getBindings("Nothing");
	CHECK(bindings.empty());
}

// ============================================================
// isActionDown with mock provider tests
// ============================================================

TEST_CASE("InputMapper - isActionDown with key provider", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);

	std::set<int> pressedKeys;

	mitiru::InputStateProvider provider;
	provider.isKeyDown = [&](int code) -> bool
	{
		return pressedKeys.count(code) > 0;
	};
	mapper.setProvider(provider);

	/// キー未押下
	CHECK_FALSE(mapper.isActionDown("Jump"));

	/// キー押下
	pressedKeys.insert(static_cast<int>(mitiru::KeyCode::Space));
	CHECK(mapper.isActionDown("Jump"));

	/// キー解放
	pressedKeys.clear();
	CHECK_FALSE(mapper.isActionDown("Jump"));
}

TEST_CASE("InputMapper - isActionDown with multiple bindings OR logic", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);
	mapper.bindKey("Jump", mitiru::KeyCode::W);

	std::set<int> pressedKeys;

	mitiru::InputStateProvider provider;
	provider.isKeyDown = [&](int code) -> bool
	{
		return pressedKeys.count(code) > 0;
	};
	mapper.setProvider(provider);

	/// どちらも未押下
	CHECK_FALSE(mapper.isActionDown("Jump"));

	/// Spaceのみ
	pressedKeys.insert(static_cast<int>(mitiru::KeyCode::Space));
	CHECK(mapper.isActionDown("Jump"));

	/// Wのみ
	pressedKeys.clear();
	pressedKeys.insert(static_cast<int>(mitiru::KeyCode::W));
	CHECK(mapper.isActionDown("Jump"));

	/// 両方
	pressedKeys.insert(static_cast<int>(mitiru::KeyCode::Space));
	CHECK(mapper.isActionDown("Jump"));
}

TEST_CASE("InputMapper - isActionDown with mouse button", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindMouseButton("Fire", 0);

	bool mouseDown = false;

	mitiru::InputStateProvider provider;
	provider.isMouseButtonDown = [&](int button) -> bool
	{
		return button == 0 && mouseDown;
	};
	mapper.setProvider(provider);

	CHECK_FALSE(mapper.isActionDown("Fire"));

	mouseDown = true;
	CHECK(mapper.isActionDown("Fire"));
}

TEST_CASE("InputMapper - isActionDown with gamepad button", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindGamepadButton("Jump", mitiru::GamepadButtonId::A);

	bool gpDown = false;

	mitiru::InputStateProvider provider;
	provider.isGamepadButtonDown = [&](int code) -> bool
	{
		return code == static_cast<int>(mitiru::GamepadButtonId::A) && gpDown;
	};
	mapper.setProvider(provider);

	CHECK_FALSE(mapper.isActionDown("Jump"));

	gpDown = true;
	CHECK(mapper.isActionDown("Jump"));
}

// ============================================================
// isActionPressed tests
// ============================================================

TEST_CASE("InputMapper - isActionPressed with key provider", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);

	bool pressed = false;

	mitiru::InputStateProvider provider;
	provider.isKeyPressed = [&](int code) -> bool
	{
		return code == static_cast<int>(mitiru::KeyCode::Space) && pressed;
	};
	mapper.setProvider(provider);

	CHECK_FALSE(mapper.isActionPressed("Jump"));

	pressed = true;
	CHECK(mapper.isActionPressed("Jump"));
}

// ============================================================
// getActionAxis tests
// ============================================================

TEST_CASE("InputMapper - getActionAxis with gamepad axis", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindGamepadAxis("MoveX", 0);

	float axisValue = 0.0f;

	mitiru::InputStateProvider provider;
	provider.getGamepadAxis = [&](int axisId) -> float
	{
		return (axisId == 0) ? axisValue : 0.0f;
	};
	mapper.setProvider(provider);

	CHECK(mapper.getActionAxis("MoveX") == Catch::Approx(0.0f));

	axisValue = 0.75f;
	CHECK(mapper.getActionAxis("MoveX") == Catch::Approx(0.75f));

	axisValue = -0.5f;
	CHECK(mapper.getActionAxis("MoveX") == Catch::Approx(-0.5f));
}

TEST_CASE("InputMapper - getActionAxis for unbound action returns 0", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	CHECK(mapper.getActionAxis("Nothing") == 0.0f);
}

TEST_CASE("InputMapper - isActionDown for unregistered action returns false", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	CHECK_FALSE(mapper.isActionDown("Nothing"));
	CHECK_FALSE(mapper.isActionPressed("Nothing"));
}

// ============================================================
// Rebinding tests
// ============================================================

TEST_CASE("InputMapper - rebind action at runtime", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);

	std::set<int> pressedKeys;

	mitiru::InputStateProvider provider;
	provider.isKeyDown = [&](int code) -> bool
	{
		return pressedKeys.count(code) > 0;
	};
	mapper.setProvider(provider);

	/// Spaceで反応
	pressedKeys.insert(static_cast<int>(mitiru::KeyCode::Space));
	CHECK(mapper.isActionDown("Jump"));

	/// リバインド: Space → Enter
	mapper.unbindAll("Jump");
	mapper.bindKey("Jump", mitiru::KeyCode::Enter);

	/// もうSpaceでは反応しない
	CHECK_FALSE(mapper.isActionDown("Jump"));

	/// Enterで反応
	pressedKeys.clear();
	pressedKeys.insert(static_cast<int>(mitiru::KeyCode::Enter));
	CHECK(mapper.isActionDown("Jump"));
}

// ============================================================
// InputBinding factory tests
// ============================================================

TEST_CASE("InputMapper - InputBinding factory methods", "[mitiru][input][mapper]")
{
	const auto keyBinding = mitiru::InputBinding::fromKey(mitiru::KeyCode::A);
	CHECK(keyBinding.type == mitiru::BindingType::Key);
	CHECK(keyBinding.code == static_cast<int>(mitiru::KeyCode::A));

	const auto mouseBinding = mitiru::InputBinding::fromMouseButton(1);
	CHECK(mouseBinding.type == mitiru::BindingType::MouseButton);
	CHECK(mouseBinding.code == 1);

	const auto gpBinding = mitiru::InputBinding::fromGamepadButton(mitiru::GamepadButtonId::X);
	CHECK(gpBinding.type == mitiru::BindingType::GamepadButton);
	CHECK(gpBinding.code == static_cast<int>(mitiru::GamepadButtonId::X));

	const auto axisBinding = mitiru::InputBinding::fromGamepadAxis(3);
	CHECK(axisBinding.type == mitiru::BindingType::GamepadAxis);
	CHECK(axisBinding.code == 3);
}

// ============================================================
// Provider not set - graceful degradation
// ============================================================

TEST_CASE("InputMapper - no provider set returns false for all queries", "[mitiru][input][mapper]")
{
	mitiru::InputMapper mapper;
	mapper.bindKey("Jump", mitiru::KeyCode::Space);
	mapper.bindMouseButton("Fire", 0);
	mapper.bindGamepadButton("Dash", mitiru::GamepadButtonId::B);
	mapper.bindGamepadAxis("MoveX", 0);

	/// プロバイダ未設定でもクラッシュしない
	CHECK_FALSE(mapper.isActionDown("Jump"));
	CHECK_FALSE(mapper.isActionDown("Fire"));
	CHECK_FALSE(mapper.isActionDown("Dash"));
	CHECK_FALSE(mapper.isActionPressed("Jump"));
	CHECK(mapper.getActionAxis("MoveX") == 0.0f);
}
