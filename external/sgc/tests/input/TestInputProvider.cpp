/// @file TestInputProvider.cpp
/// @brief IInputProvider 抽象インターフェースのモックテスト + ActionMap統合

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "sgc/core/Hash.hpp"
#include "sgc/input/ActionMap.hpp"
#include "sgc/input/IInputProvider.hpp"

using namespace sgc::literals;

namespace
{

/// @brief プログラムで制御可能なモック入力プロバイダー
class MockInputProvider : public sgc::IInputProvider
{
public:
	std::vector<int> pressedKeys;  ///< 現在押されているキー
	sgc::Vec2f mousePos{0.0f, 0.0f};    ///< マウス座標
	sgc::Vec2f mouseDlt{0.0f, 0.0f};    ///< マウス移動量

	void pollPressedKeys(std::vector<int>& outPressedKeys) const override
	{
		outPressedKeys = pressedKeys;
	}

	[[nodiscard]] sgc::Vec2f mousePosition() const override
	{
		return mousePos;
	}

	[[nodiscard]] sgc::Vec2f mouseDelta() const override
	{
		return mouseDlt;
	}
};

/// @brief マウスボタン対応のモック入力プロバイダー
class MockMouseInputProvider : public sgc::IInputProvider
{
public:
	std::vector<int> pressedKeys;
	sgc::Vec2f mousePos{};
	sgc::Vec2f mouseDlt{};
	bool mouseButtonsDown[3]{false, false, false};
	bool mouseButtonsPressed[3]{false, false, false};
	bool mouseButtonsReleased[3]{false, false, false};

	void pollPressedKeys(std::vector<int>& out) const override { out = pressedKeys; }
	[[nodiscard]] sgc::Vec2f mousePosition() const override { return mousePos; }
	[[nodiscard]] sgc::Vec2f mouseDelta() const override { return mouseDlt; }

	[[nodiscard]] bool isMouseButtonDown(int button) const override
	{
		if (button >= 0 && button < 3) return mouseButtonsDown[button];
		return false;
	}

	[[nodiscard]] bool isMouseButtonPressed(int button) const override
	{
		if (button >= 0 && button < 3) return mouseButtonsPressed[button];
		return false;
	}

	[[nodiscard]] bool isMouseButtonReleased(int button) const override
	{
		if (button >= 0 && button < 3) return mouseButtonsReleased[button];
		return false;
	}
};

} // anonymous namespace

// ── IInputProvider contract tests ────────────────────────

TEST_CASE("MockInputProvider pollPressedKeys returns set keys", "[input][provider]")
{
	MockInputProvider provider;
	provider.pressedKeys = {32, 65, 68};

	std::vector<int> out;
	provider.pollPressedKeys(out);

	REQUIRE(out.size() == 3);
	REQUIRE(out[0] == 32);
	REQUIRE(out[1] == 65);
	REQUIRE(out[2] == 68);
}

TEST_CASE("MockInputProvider pollPressedKeys returns empty when no keys", "[input][provider]")
{
	MockInputProvider provider;

	std::vector<int> out;
	provider.pollPressedKeys(out);

	REQUIRE(out.empty());
}

TEST_CASE("MockInputProvider mousePosition returns set value", "[input][provider]")
{
	MockInputProvider provider;
	provider.mousePos = {400.0f, 300.0f};

	REQUIRE(provider.mousePosition().x == Catch::Approx(400.0f));
	REQUIRE(provider.mousePosition().y == Catch::Approx(300.0f));
}

TEST_CASE("MockInputProvider mouseDelta returns set value", "[input][provider]")
{
	MockInputProvider provider;
	provider.mouseDlt = {5.0f, -3.0f};

	REQUIRE(provider.mouseDelta().x == Catch::Approx(5.0f));
	REQUIRE(provider.mouseDelta().y == Catch::Approx(-3.0f));
}

TEST_CASE("IInputProvider polymorphic dispatch works", "[input][provider]")
{
	MockInputProvider mock;
	mock.pressedKeys = {42};
	mock.mousePos = {100.0f, 200.0f};

	sgc::IInputProvider& provider = mock;

	std::vector<int> keys;
	provider.pollPressedKeys(keys);
	REQUIRE(keys.size() == 1);
	REQUIRE(keys[0] == 42);

	REQUIRE(provider.mousePosition().x == Catch::Approx(100.0f));
}

// ── ActionMap integration ────────────────────────────────

TEST_CASE("MockInputProvider drives ActionMap correctly", "[input][provider][integration]")
{
	MockInputProvider provider;
	sgc::ActionMap actionMap;

	// バインディング設定
	actionMap.bind("jump"_hash, 32);
	actionMap.bind("left"_hash, 65);

	// フレーム1: スペースを押す
	provider.pressedKeys = {32};
	std::vector<int> keys;
	provider.pollPressedKeys(keys);
	actionMap.update(keys);

	REQUIRE(actionMap.isPressed("jump"_hash));
	REQUIRE_FALSE(actionMap.isHeld("left"_hash));

	// フレーム2: スペースを押し続ける
	provider.pollPressedKeys(keys);
	actionMap.update(keys);

	REQUIRE(actionMap.isHeld("jump"_hash));
	REQUIRE_FALSE(actionMap.isPressed("jump"_hash));

	// フレーム3: スペースを離す
	provider.pressedKeys.clear();
	provider.pollPressedKeys(keys);
	actionMap.update(keys);

	REQUIRE(actionMap.isReleased("jump"_hash));
}

TEST_CASE("MockInputProvider with multiple keys drives ActionMap", "[input][provider][integration]")
{
	MockInputProvider provider;
	sgc::ActionMap actionMap;

	actionMap.bind("fire"_hash, 90);
	actionMap.bind("moveUp"_hash, 87);

	provider.pressedKeys = {90, 87};
	std::vector<int> keys;
	provider.pollPressedKeys(keys);
	actionMap.update(keys);

	REQUIRE(actionMap.isPressed("fire"_hash));
	REQUIRE(actionMap.isPressed("moveUp"_hash));
}

// ── Mouse button tests ──────────────────────────────────

TEST_CASE("IInputProvider default mouse button methods return false", "[input][provider][mouse]")
{
	MockInputProvider provider;
	sgc::IInputProvider& base = provider;

	REQUIRE_FALSE(base.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT));
	REQUIRE_FALSE(base.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT));
	REQUIRE_FALSE(base.isMouseButtonReleased(sgc::IInputProvider::MOUSE_LEFT));
}

TEST_CASE("Mouse button constants have expected values", "[input][provider][mouse]")
{
	REQUIRE(sgc::IInputProvider::MOUSE_LEFT == 0);
	REQUIRE(sgc::IInputProvider::MOUSE_RIGHT == 1);
	REQUIRE(sgc::IInputProvider::MOUSE_MIDDLE == 2);
}

TEST_CASE("MockMouseInputProvider isMouseButtonDown works", "[input][provider][mouse]")
{
	MockMouseInputProvider provider;
	provider.mouseButtonsDown[sgc::IInputProvider::MOUSE_LEFT] = true;

	REQUIRE(provider.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT));
	REQUIRE_FALSE(provider.isMouseButtonDown(sgc::IInputProvider::MOUSE_RIGHT));
	REQUIRE_FALSE(provider.isMouseButtonDown(sgc::IInputProvider::MOUSE_MIDDLE));
}

TEST_CASE("MockMouseInputProvider isMouseButtonPressed works", "[input][provider][mouse]")
{
	MockMouseInputProvider provider;
	provider.mouseButtonsPressed[sgc::IInputProvider::MOUSE_RIGHT] = true;

	REQUIRE(provider.isMouseButtonPressed(sgc::IInputProvider::MOUSE_RIGHT));
	REQUIRE_FALSE(provider.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT));
}

TEST_CASE("MockMouseInputProvider isMouseButtonReleased works", "[input][provider][mouse]")
{
	MockMouseInputProvider provider;
	provider.mouseButtonsReleased[sgc::IInputProvider::MOUSE_MIDDLE] = true;

	REQUIRE(provider.isMouseButtonReleased(sgc::IInputProvider::MOUSE_MIDDLE));
	REQUIRE_FALSE(provider.isMouseButtonReleased(sgc::IInputProvider::MOUSE_LEFT));
}

TEST_CASE("MockMouseInputProvider polymorphic dispatch for mouse buttons", "[input][provider][mouse]")
{
	MockMouseInputProvider mock;
	mock.mouseButtonsDown[sgc::IInputProvider::MOUSE_LEFT] = true;
	mock.mouseButtonsPressed[sgc::IInputProvider::MOUSE_LEFT] = true;

	sgc::IInputProvider& provider = mock;

	REQUIRE(provider.isMouseButtonDown(sgc::IInputProvider::MOUSE_LEFT));
	REQUIRE(provider.isMouseButtonPressed(sgc::IInputProvider::MOUSE_LEFT));
	REQUIRE_FALSE(provider.isMouseButtonReleased(sgc::IInputProvider::MOUSE_LEFT));
}

// ── Key query tests ──────────────────────────────────────

TEST_CASE("IInputProvider default isKeyDown returns false", "[input][provider][key]")
{
	MockInputProvider provider;
	sgc::IInputProvider& base = provider;

	REQUIRE_FALSE(base.isKeyDown(42));
}

TEST_CASE("IInputProvider default isKeyJustPressed returns false", "[input][provider][key]")
{
	MockInputProvider provider;
	sgc::IInputProvider& base = provider;

	REQUIRE_FALSE(base.isKeyJustPressed(42));
}

namespace
{

/// @brief キー問い合わせ対応のモック入力プロバイダー
class MockKeyQueryProvider : public sgc::IInputProvider
{
public:
	std::vector<int> pressedKeys;
	std::vector<int> downKeys;         ///< 押下中キー
	std::vector<int> justPressedKeys;  ///< 今フレームで押されたキー
	sgc::Vec2f mousePos{};
	sgc::Vec2f mouseDlt{};

	void pollPressedKeys(std::vector<int>& out) const override { out = pressedKeys; }
	[[nodiscard]] sgc::Vec2f mousePosition() const override { return mousePos; }
	[[nodiscard]] sgc::Vec2f mouseDelta() const override { return mouseDlt; }

	[[nodiscard]] bool isKeyDown(int keyCode) const override
	{
		for (const auto& k : downKeys)
		{
			if (k == keyCode) return true;
		}
		return false;
	}

	[[nodiscard]] bool isKeyJustPressed(int keyCode) const override
	{
		for (const auto& k : justPressedKeys)
		{
			if (k == keyCode) return true;
		}
		return false;
	}
};

} // anonymous namespace

TEST_CASE("MockKeyQueryProvider isKeyDown works", "[input][provider][key]")
{
	MockKeyQueryProvider provider;
	provider.downKeys = {32, 65};

	REQUIRE(provider.isKeyDown(32));
	REQUIRE(provider.isKeyDown(65));
	REQUIRE_FALSE(provider.isKeyDown(90));
}

TEST_CASE("MockKeyQueryProvider isKeyJustPressed works", "[input][provider][key]")
{
	MockKeyQueryProvider provider;
	provider.justPressedKeys = {32};

	REQUIRE(provider.isKeyJustPressed(32));
	REQUIRE_FALSE(provider.isKeyJustPressed(65));
}

TEST_CASE("MockKeyQueryProvider polymorphic dispatch for key queries", "[input][provider][key]")
{
	MockKeyQueryProvider mock;
	mock.downKeys = {42};
	mock.justPressedKeys = {42};

	sgc::IInputProvider& provider = mock;

	REQUIRE(provider.isKeyDown(42));
	REQUIRE(provider.isKeyJustPressed(42));
	REQUIRE_FALSE(provider.isKeyDown(99));
	REQUIRE_FALSE(provider.isKeyJustPressed(99));
}
