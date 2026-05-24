/// @file TestDxLibInputProvider.cpp
/// @brief DxLibInputProvider adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "sgc/dxlib/DxLibInputProvider.hpp"

using namespace sgc;
using namespace sgc::dxlib;
using Catch::Approx;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { dxlib_stub::reset(); }
};

} // anonymous namespace

// ── pollPressedKeys ──────────────────────────────────────

TEST_CASE("DxLibInputProvider pollPressedKeys returns empty when no keys pressed", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_SPACE, 32);

	std::vector<int> keys;
	provider.pollPressedKeys(keys);

	REQUIRE(keys.empty());
}

TEST_CASE("DxLibInputProvider pollPressedKeys returns pressed key", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_SPACE, 32);
	provider.addKey(KEY_INPUT_A, 'A');

	dxlib_stub::keyState()[KEY_INPUT_SPACE] = 1;

	std::vector<int> keys;
	provider.pollPressedKeys(keys);

	REQUIRE(keys.size() == 1);
	REQUIRE(keys[0] == 32);
}

TEST_CASE("DxLibInputProvider pollPressedKeys returns multiple pressed keys", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_SPACE, 32);
	provider.addKey(KEY_INPUT_A, 'A');

	dxlib_stub::keyState()[KEY_INPUT_SPACE] = 1;
	dxlib_stub::keyState()[KEY_INPUT_A] = 1;

	std::vector<int> keys;
	provider.pollPressedKeys(keys);

	REQUIRE(keys.size() == 2);
}

// ── mousePosition ────────────────────────────────────────

TEST_CASE("DxLibInputProvider mousePosition returns mock position", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;

	dxlib_stub::mouseX() = 400;
	dxlib_stub::mouseY() = 300;

	auto pos = provider.mousePosition();

	REQUIRE(pos.x == Approx(400.0f));
	REQUIRE(pos.y == Approx(300.0f));
}

// ── mouseDelta ───────────────────────────────────────────

TEST_CASE("DxLibInputProvider mouseDelta returns movement since last call", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;

	dxlib_stub::mouseX() = 100;
	dxlib_stub::mouseY() = 100;
	(void)provider.mouseDelta();  // 初回: 前回座標を設定

	dxlib_stub::mouseX() = 120;
	dxlib_stub::mouseY() = 110;
	auto delta = provider.mouseDelta();

	REQUIRE(delta.x == Approx(20.0f));
	REQUIRE(delta.y == Approx(10.0f));
}

// ── isMouseButtonDown ────────────────────────────────────

TEST_CASE("DxLibInputProvider isMouseButtonDown detects left button", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;

	REQUIRE_FALSE(provider.isMouseButtonDown(IInputProvider::MOUSE_LEFT));

	dxlib_stub::mouseInput() = MOUSE_INPUT_LEFT;

	REQUIRE(provider.isMouseButtonDown(IInputProvider::MOUSE_LEFT));
}

// ── isMouseButtonPressed ─────────────────────────────────

TEST_CASE("DxLibInputProvider isMouseButtonPressed detects new press", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;

	// 前フレーム: ボタンなし
	provider.updateMouseState();

	// 今フレーム: 左ボタン押下
	dxlib_stub::mouseInput() = MOUSE_INPUT_LEFT;

	REQUIRE(provider.isMouseButtonPressed(IInputProvider::MOUSE_LEFT));
}

// ── isMouseButtonReleased ────────────────────────────────

TEST_CASE("DxLibInputProvider isMouseButtonReleased detects release", "[dxlib][input]")
{
	ResetFixture fix;
	DxLibInputProvider provider;

	// 前フレーム: 左ボタン押下中
	dxlib_stub::mouseInput() = MOUSE_INPUT_LEFT;
	provider.updateMouseState();

	// 今フレーム: ボタンなし
	dxlib_stub::mouseInput() = 0;

	REQUIRE(provider.isMouseButtonReleased(IInputProvider::MOUSE_LEFT));
}

// ── isKeyDown ────────────────────────────────────────────

TEST_CASE("DxLibInputProvider isKeyDown detects pressed key", "[dxlib][input][key]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_SPACE, 32);

	REQUIRE_FALSE(provider.isKeyDown(32));

	dxlib_stub::keyState()[KEY_INPUT_SPACE] = 1;

	REQUIRE(provider.isKeyDown(32));
}

TEST_CASE("DxLibInputProvider isKeyDown returns false for unregistered key", "[dxlib][input][key]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_SPACE, 32);

	dxlib_stub::keyState()[KEY_INPUT_A] = 1;

	REQUIRE_FALSE(provider.isKeyDown(99));
}

// ── isKeyJustPressed ────────────────────────────────────

TEST_CASE("DxLibInputProvider isKeyJustPressed detects new press", "[dxlib][input][key]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_SPACE, 32);

	// 前フレーム: キー押されていない
	provider.update();

	// 今フレーム: スペースキー押下
	dxlib_stub::keyState()[KEY_INPUT_SPACE] = 1;

	REQUIRE(provider.isKeyJustPressed(32));
}

TEST_CASE("DxLibInputProvider isKeyJustPressed returns false when held", "[dxlib][input][key]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_SPACE, 32);

	// 前フレーム: スペースキー押下中
	dxlib_stub::keyState()[KEY_INPUT_SPACE] = 1;
	provider.update();

	// 今フレーム: まだ押下中
	REQUIRE_FALSE(provider.isKeyJustPressed(32));
}

TEST_CASE("DxLibInputProvider update saves both key and mouse state", "[dxlib][input][key]")
{
	ResetFixture fix;
	DxLibInputProvider provider;
	provider.addKey(KEY_INPUT_A, 'A');

	// フレーム1: Aキー押下 + マウスなし
	dxlib_stub::keyState()[KEY_INPUT_A] = 1;
	dxlib_stub::mouseInput() = 0;
	provider.update();

	// フレーム2: Aキー離す + マウス押下
	dxlib_stub::keyState()[KEY_INPUT_A] = 0;
	dxlib_stub::mouseInput() = MOUSE_INPUT_LEFT;

	REQUIRE_FALSE(provider.isKeyJustPressed('A'));
	REQUIRE(provider.isMouseButtonPressed(IInputProvider::MOUSE_LEFT));
}
