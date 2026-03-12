/// @file TestWin32InputConfig.cpp
/// @brief Win32Input関連のユニットテスト（プラットフォーム非依存部分）
/// @details KeyCode enum、MouseButton enum、InputState状態管理ロジックのテスト。
///          実Win32 APIは使用しない。

#include <catch2/catch_test_macros.hpp>

#include "mitiru/input/KeyCode.hpp"
#include "mitiru/input/InputState.hpp"

// ============================================================
// KeyCode enum value tests
// ============================================================

TEST_CASE("Win32InputConfig - KeyCode values match Win32 VK codes", "[mitiru][input][win32input]")
{
	/// KeyCodeはWin32仮想キーコードに準拠
	CHECK(static_cast<int>(mitiru::KeyCode::Space) == 32);
	CHECK(static_cast<int>(mitiru::KeyCode::Enter) == 13);
	CHECK(static_cast<int>(mitiru::KeyCode::Escape) == 27);
	CHECK(static_cast<int>(mitiru::KeyCode::A) == 65);
	CHECK(static_cast<int>(mitiru::KeyCode::Z) == 90);
	CHECK(static_cast<int>(mitiru::KeyCode::Num0) == 48);
	CHECK(static_cast<int>(mitiru::KeyCode::Num9) == 57);
	CHECK(static_cast<int>(mitiru::KeyCode::F1) == 112);
	CHECK(static_cast<int>(mitiru::KeyCode::F12) == 123);
	CHECK(static_cast<int>(mitiru::KeyCode::Shift) == 16);
	CHECK(static_cast<int>(mitiru::KeyCode::Ctrl) == 17);
	CHECK(static_cast<int>(mitiru::KeyCode::Alt) == 18);
	CHECK(static_cast<int>(mitiru::KeyCode::Tab) == 9);
}

TEST_CASE("Win32InputConfig - KeyCode arrow keys match VK codes", "[mitiru][input][win32input]")
{
	CHECK(static_cast<int>(mitiru::KeyCode::Left) == 37);
	CHECK(static_cast<int>(mitiru::KeyCode::Up) == 38);
	CHECK(static_cast<int>(mitiru::KeyCode::Right) == 39);
	CHECK(static_cast<int>(mitiru::KeyCode::Down) == 40);
}

// ============================================================
// MouseButton enum tests
// ============================================================

TEST_CASE("Win32InputConfig - MouseButton enum values", "[mitiru][input][win32input]")
{
	CHECK(static_cast<int>(mitiru::MouseButton::Left) == 0);
	CHECK(static_cast<int>(mitiru::MouseButton::Right) == 1);
	CHECK(static_cast<int>(mitiru::MouseButton::Middle) == 2);
}

// ============================================================
// InputState key tracking tests
// ============================================================

TEST_CASE("Win32InputConfig - InputState default has no keys down", "[mitiru][input][win32input]")
{
	mitiru::InputState state;

	CHECK_FALSE(state.isKeyDown(static_cast<int>(mitiru::KeyCode::A)));
	CHECK_FALSE(state.isKeyDown(static_cast<int>(mitiru::KeyCode::Space)));
	CHECK_FALSE(state.isMouseButtonDown(mitiru::MouseButton::Left));
}

TEST_CASE("Win32InputConfig - InputState key down and up", "[mitiru][input][win32input]")
{
	mitiru::InputState state;
	const int spaceCode = static_cast<int>(mitiru::KeyCode::Space);

	state.setKeyDown(spaceCode, true);
	CHECK(state.isKeyDown(spaceCode));

	state.setKeyDown(spaceCode, false);
	CHECK_FALSE(state.isKeyDown(spaceCode));
}

TEST_CASE("Win32InputConfig - InputState edge detection for keys", "[mitiru][input][win32input]")
{
	mitiru::InputState state;
	const int aCode = static_cast<int>(mitiru::KeyCode::A);

	/// フレーム1: キーを押す
	state.beginFrame();
	state.setKeyDown(aCode, true);
	CHECK(state.isKeyJustPressed(aCode));
	CHECK_FALSE(state.isKeyJustReleased(aCode));

	/// フレーム2: キーを押し続ける
	state.beginFrame();
	state.setKeyDown(aCode, true);
	CHECK_FALSE(state.isKeyJustPressed(aCode));
	CHECK_FALSE(state.isKeyJustReleased(aCode));

	/// フレーム3: キーを離す
	state.beginFrame();
	state.setKeyDown(aCode, false);
	CHECK_FALSE(state.isKeyJustPressed(aCode));
	CHECK(state.isKeyJustReleased(aCode));
}

TEST_CASE("Win32InputConfig - InputState mouse button edge detection", "[mitiru][input][win32input]")
{
	mitiru::InputState state;

	/// フレーム1: 左ボタンを押す
	state.beginFrame();
	state.setMouseButtonDown(mitiru::MouseButton::Left, true);
	CHECK(state.isMouseButtonJustPressed(mitiru::MouseButton::Left));
	CHECK_FALSE(state.isMouseButtonJustReleased(mitiru::MouseButton::Left));

	/// フレーム2: 押し続け
	state.beginFrame();
	state.setMouseButtonDown(mitiru::MouseButton::Left, true);
	CHECK_FALSE(state.isMouseButtonJustPressed(mitiru::MouseButton::Left));

	/// フレーム3: 離す
	state.beginFrame();
	state.setMouseButtonDown(mitiru::MouseButton::Left, false);
	CHECK(state.isMouseButtonJustReleased(mitiru::MouseButton::Left));
}

TEST_CASE("Win32InputConfig - InputState mouse position", "[mitiru][input][win32input]")
{
	mitiru::InputState state;
	state.setMousePosition(100.0f, 200.0f);

	const auto [x, y] = state.mousePosition();
	CHECK(x == 100.0f);
	CHECK(y == 200.0f);
}

TEST_CASE("Win32InputConfig - InputState out of range key code returns false", "[mitiru][input][win32input]")
{
	mitiru::InputState state;

	CHECK_FALSE(state.isKeyDown(-1));
	CHECK_FALSE(state.isKeyDown(256));
	CHECK_FALSE(state.isKeyDown(1000));
	CHECK_FALSE(state.isKeyJustPressed(-1));
	CHECK_FALSE(state.isKeyJustReleased(999));
}
