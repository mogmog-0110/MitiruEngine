#include <catch2/catch_test_macros.hpp>

#include <mitiru/platform/glfw/GlfwInput.hpp>
#include <mitiru/platform/glfw/GlfwVulkanSurface.hpp>

// ============================================================
// GlfwInputConfig
// ============================================================

TEST_CASE("GlfwInputConfig - default values", "[glfw]")
{
	const mitiru::GlfwInputConfig config;

	REQUIRE(config.enableKeyboard == true);
	REQUIRE(config.enableMouse == true);
	REQUIRE(config.enableJoystick == true);
	REQUIRE(config.rawMouseMotion == false);
	REQUIRE(config.stickyKeys == false);
	REQUIRE(config.mouseSensitivity == 1.0f);
}

TEST_CASE("GlfwInputConfig - defaults factory", "[glfw]")
{
	const auto config = mitiru::GlfwInputConfig::defaults();

	REQUIRE(config.enableKeyboard == true);
	REQUIRE(config.rawMouseMotion == false);
}

TEST_CASE("GlfwInputConfig - fps factory", "[glfw]")
{
	const auto config = mitiru::GlfwInputConfig::fps();

	REQUIRE(config.rawMouseMotion == true);
	REQUIRE(config.mouseSensitivity == 0.5f);
}

// ============================================================
// GlfwJoystickState
// ============================================================

TEST_CASE("GlfwJoystickState - default state", "[glfw]")
{
	const mitiru::GlfwJoystickState state;

	REQUIRE(state.connected == false);
	REQUIRE(state.joystickId == -1);
	REQUIRE(state.axes[0] == 0.0f);
	REQUIRE(state.buttons[0] == false);
}

TEST_CASE("GlfwJoystickState - isButtonJustPressed edge detection", "[glfw]")
{
	mitiru::GlfwJoystickState state;

	state.buttons[3] = true;
	REQUIRE(state.isButtonJustPressed(3) == true);

	state.beginFrame();
	REQUIRE(state.isButtonJustPressed(3) == false);
}

TEST_CASE("GlfwJoystickState - isButtonJustReleased edge detection", "[glfw]")
{
	mitiru::GlfwJoystickState state;

	state.buttons[5] = true;
	state.beginFrame();

	state.buttons[5] = false;
	REQUIRE(state.isButtonJustReleased(5) == true);
}

TEST_CASE("GlfwJoystickState - out of range button returns false", "[glfw]")
{
	const mitiru::GlfwJoystickState state;

	REQUIRE(state.isButtonJustPressed(-1) == false);
	REQUIRE(state.isButtonJustPressed(100) == false);
	REQUIRE(state.isButtonJustReleased(-1) == false);
	REQUIRE(state.isButtonJustReleased(100) == false);
}

// ============================================================
// GlfwScrollState
// ============================================================

TEST_CASE("GlfwScrollState - default values", "[glfw]")
{
	const mitiru::GlfwScrollState state;

	REQUIRE(state.xOffset == 0.0);
	REQUIRE(state.yOffset == 0.0);
}

TEST_CASE("GlfwScrollState - reset", "[glfw]")
{
	mitiru::GlfwScrollState state;
	state.xOffset = 3.0;
	state.yOffset = -1.5;

	state.reset();

	REQUIRE(state.xOffset == 0.0);
	REQUIRE(state.yOffset == 0.0);
}

// ============================================================
// GlfwVulkanSurfaceDesc
// ============================================================

TEST_CASE("GlfwVulkanSurfaceDesc - default values", "[glfw]")
{
	const mitiru::GlfwVulkanSurfaceDesc desc;

	REQUIRE(desc.requirePresentation == true);
}

TEST_CASE("GlfwVulkanSurfaceDesc - defaultRequiredExtensions includes surface", "[glfw]")
{
	const auto extensions = mitiru::GlfwVulkanSurfaceDesc::defaultRequiredExtensions();

	REQUIRE(extensions.size() >= 1);
	REQUIRE(extensions[0] == "VK_KHR_surface");
}

TEST_CASE("GlfwVulkanSurfaceDesc - platform-specific extension", "[glfw]")
{
	const auto extensions = mitiru::GlfwVulkanSurfaceDesc::defaultRequiredExtensions();

	/// Windows環境ではwin32_surface、Linux環境ではxcb_surface等が含まれる
#ifdef _WIN32
	REQUIRE(extensions.size() >= 2);
	REQUIRE(extensions[1] == "VK_KHR_win32_surface");
#endif
}

// ============================================================
// SurfaceCreateResult
// ============================================================

TEST_CASE("SurfaceCreateResult - ok result", "[glfw]")
{
	const auto result = mitiru::SurfaceCreateResult::ok();

	REQUIRE(result.success == true);
	REQUIRE(result.errorMessage.empty());
}

TEST_CASE("SurfaceCreateResult - fail result", "[glfw]")
{
	const auto result = mitiru::SurfaceCreateResult::fail("test error");

	REQUIRE(result.success == false);
	REQUIRE(result.errorMessage == "test error");
}
