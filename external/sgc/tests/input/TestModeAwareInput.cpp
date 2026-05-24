#include <catch2/catch_test_macros.hpp>
#include <sgc/input/ModeAwareInput.hpp>

namespace
{

/// @brief テスト用の入力プロバイダー
class MockInput : public sgc::IInputProvider
{
public:
	void pollPressedKeys(std::vector<int>&) const override {}
	sgc::Vec2f mousePosition() const noexcept override { return {100.0f, 200.0f}; }
	sgc::Vec2f mouseDelta() const noexcept override { return {0.0f, 0.0f}; }
	bool isMouseButtonDown(int) const noexcept override { return false; }
	bool isMouseButtonPressed(int) const noexcept override { return false; }
	bool isMouseButtonReleased(int) const noexcept override { return false; }
	bool isKeyDown(int keyCode) const noexcept override { return keyCode == 42; }
	bool isKeyJustPressed(int keyCode) const noexcept override { return keyCode == 42; }
};

} // anonymous namespace

TEST_CASE("ModeAwareInput - gameplay mode passes keys", "[input][mode-aware]")
{
	MockInput mock;
	sgc::ModeAwareInput input(&mock);

	REQUIRE(input.currentMode() == sgc::InputMode::Gameplay);
	REQUIRE(input.isGameplayKeyDown(42));
	REQUIRE(input.isGameplayKeyJustPressed(42));
	REQUIRE_FALSE(input.isGameplayKeyDown(99));
}

TEST_CASE("ModeAwareInput - text input blocks gameplay keys", "[input][mode-aware]")
{
	MockInput mock;
	sgc::ModeAwareInput input(&mock);

	input.pushMode(sgc::InputMode::TextInput);
	REQUIRE(input.currentMode() == sgc::InputMode::TextInput);
	REQUIRE_FALSE(input.isGameplayKeyDown(42));
	REQUIRE_FALSE(input.isGameplayKeyJustPressed(42));
	// Raw key still works
	REQUIRE(input.isKeyDown(42));
}

TEST_CASE("ModeAwareInput - pop restores gameplay", "[input][mode-aware]")
{
	MockInput mock;
	sgc::ModeAwareInput input(&mock);

	input.pushMode(sgc::InputMode::Menu);
	REQUIRE_FALSE(input.isGameplayKeyDown(42));

	input.popMode();
	REQUIRE(input.isGameplayKeyDown(42));
}

TEST_CASE("ModeAwareInput - menu key filtering", "[input][mode-aware]")
{
	MockInput mock;
	sgc::ModeAwareInput input(&mock);

	REQUIRE_FALSE(input.isMenuKeyDown(42));

	input.pushMode(sgc::InputMode::Menu);
	REQUIRE(input.isMenuKeyDown(42));
}

TEST_CASE("ModeAwareInput - mouse position always available", "[input][mode-aware]")
{
	MockInput mock;
	sgc::ModeAwareInput input(&mock);

	input.pushMode(sgc::InputMode::Disabled);
	auto pos = input.mousePosition();
	REQUIRE(pos.x == 100.0f);
	REQUIRE(pos.y == 200.0f);
}

TEST_CASE("ModeAwareInput - null provider is safe", "[input][mode-aware]")
{
	sgc::ModeAwareInput input(nullptr);

	REQUIRE_FALSE(input.isGameplayKeyDown(42));
	REQUIRE_FALSE(input.isKeyDown(42));
	auto pos = input.mousePosition();
	REQUIRE(pos.x == 0.0f);
}

TEST_CASE("ModeAwareInput - reset clears to gameplay", "[input][mode-aware]")
{
	MockInput mock;
	sgc::ModeAwareInput input(&mock);

	input.pushMode(sgc::InputMode::TextInput);
	input.pushMode(sgc::InputMode::Menu);
	input.reset();

	REQUIRE(input.currentMode() == sgc::InputMode::Gameplay);
	REQUIRE(input.isGameplayKeyDown(42));
}
