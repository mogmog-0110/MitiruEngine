/// @file TestSiv3DInputProvider.cpp
/// @brief Siv3DInputProvider adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/siv3d/Siv3DInputProvider.hpp"

using namespace sgc;
using namespace sgc::siv3d;
using Catch::Approx;

// ── テスト前にスタブをリセット ─────────────────────────

namespace
{

struct ResetFixture
{
	ResetFixture() { siv3d_stub::reset(); }
};

} // anonymous namespace

// ── pollPressedKeys ──────────────────────────────────────

TEST_CASE("Siv3DInputProvider pollPressedKeys returns empty when no keys pressed", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;
	provider.addKey(s3d::KeySpace, 32);
	provider.addKey(s3d::KeyA, 'A');

	std::vector<int> keys;
	provider.pollPressedKeys(keys);

	REQUIRE(keys.empty());
}

TEST_CASE("Siv3DInputProvider pollPressedKeys collects pressed keys", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;
	provider.addKey(s3d::KeySpace, 32);
	provider.addKey(s3d::KeyA, 'A');

	// KeySpaceのIDは3（スタブ定義参照）
	siv3d_stub::keyPressed()[s3d::KeySpace.id] = true;

	std::vector<int> keys;
	provider.pollPressedKeys(keys);

	REQUIRE(keys.size() == 1);
	REQUIRE(keys[0] == 32);
}

TEST_CASE("Siv3DInputProvider pollPressedKeys collects multiple keys", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;
	provider.addKey(s3d::KeyW, 'W');
	provider.addKey(s3d::KeyA, 'A');
	provider.addKey(s3d::KeyS, 'S');
	provider.addKey(s3d::KeyD, 'D');

	siv3d_stub::keyPressed()[s3d::KeyW.id] = true;
	siv3d_stub::keyPressed()[s3d::KeyD.id] = true;

	std::vector<int> keys;
	provider.pollPressedKeys(keys);

	REQUIRE(keys.size() == 2);
}

// ── mousePosition ────────────────────────────────────────

TEST_CASE("Siv3DInputProvider mousePosition returns cursor position", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	siv3d_stub::mousePosX() = 400.0;
	siv3d_stub::mousePosY() = 300.0;

	const Vec2f pos = provider.mousePosition();

	REQUIRE(pos.x == Approx(400.0f));
	REQUIRE(pos.y == Approx(300.0f));
}

// ── mouseDelta ───────────────────────────────────────────

TEST_CASE("Siv3DInputProvider mouseDelta returns cursor delta", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	siv3d_stub::mouseDeltaX() = 5.0;
	siv3d_stub::mouseDeltaY() = -3.0;

	const Vec2f delta = provider.mouseDelta();

	REQUIRE(delta.x == Approx(5.0f));
	REQUIRE(delta.y == Approx(-3.0f));
}

// ── isMouseButtonDown ────────────────────────────────────

TEST_CASE("Siv3DInputProvider isMouseButtonDown left button", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	REQUIRE_FALSE(provider.isMouseButtonDown(IInputProvider::MOUSE_LEFT));

	siv3d_stub::keyPressed()[s3d::MouseL.id] = true;

	REQUIRE(provider.isMouseButtonDown(IInputProvider::MOUSE_LEFT));
}

TEST_CASE("Siv3DInputProvider isMouseButtonDown right button", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	siv3d_stub::keyPressed()[s3d::MouseR.id] = true;

	REQUIRE(provider.isMouseButtonDown(IInputProvider::MOUSE_RIGHT));
}

TEST_CASE("Siv3DInputProvider isMouseButtonDown middle button", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	siv3d_stub::keyPressed()[s3d::MouseM.id] = true;

	REQUIRE(provider.isMouseButtonDown(IInputProvider::MOUSE_MIDDLE));
}

// ── isMouseButtonPressed ─────────────────────────────────

TEST_CASE("Siv3DInputProvider isMouseButtonPressed detects frame press", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	siv3d_stub::keyDown()[s3d::MouseL.id] = true;

	REQUIRE(provider.isMouseButtonPressed(IInputProvider::MOUSE_LEFT));
}

// ── isMouseButtonReleased ────────────────────────────────

TEST_CASE("Siv3DInputProvider isMouseButtonReleased detects frame release", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	siv3d_stub::keyUp()[s3d::MouseL.id] = true;

	REQUIRE(provider.isMouseButtonReleased(IInputProvider::MOUSE_LEFT));
}

// ── isKeyDown ────────────────────────────────────────────

TEST_CASE("Siv3DInputProvider isKeyDown returns true for pressed key", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;
	provider.addKey(s3d::KeySpace, 32);

	siv3d_stub::keyPressed()[s3d::KeySpace.id] = true;

	REQUIRE(provider.isKeyDown(32));
}

TEST_CASE("Siv3DInputProvider isKeyDown returns false for unregistered key", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;

	// キー未登録のため常にfalse
	REQUIRE_FALSE(provider.isKeyDown(999));
}

// ── isKeyJustPressed ─────────────────────────────────────

TEST_CASE("Siv3DInputProvider isKeyJustPressed detects frame press", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;
	provider.addKey(s3d::KeyEnter, 13);

	siv3d_stub::keyDown()[s3d::KeyEnter.id] = true;

	REQUIRE(provider.isKeyJustPressed(13));
}

TEST_CASE("Siv3DInputProvider isKeyJustPressed returns false when not just pressed", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;
	provider.addKey(s3d::KeyEnter, 13);

	// pressed but not "just pressed" this frame
	siv3d_stub::keyPressed()[s3d::KeyEnter.id] = true;
	siv3d_stub::keyDown()[s3d::KeyEnter.id] = false;

	REQUIRE_FALSE(provider.isKeyJustPressed(13));
}

// ── clear ────────────────────────────────────────────────

TEST_CASE("Siv3DInputProvider clear removes all key bindings", "[siv3d][input]")
{
	ResetFixture fix;
	Siv3DInputProvider provider;
	provider.addKey(s3d::KeySpace, 32);
	provider.addKey(s3d::KeyA, 'A');

	siv3d_stub::keyPressed()[s3d::KeySpace.id] = true;
	siv3d_stub::keyPressed()[s3d::KeyA.id] = true;

	provider.clear();

	std::vector<int> keys;
	provider.pollPressedKeys(keys);

	REQUIRE(keys.empty());
}
