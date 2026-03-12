#include <catch2/catch_test_macros.hpp>

#include <mitiru/imgui/ImGuiBackend.hpp>

// --- NullImGuiBackend tests ---

TEST_CASE("NullImGuiBackend init returns true", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	REQUIRE(backend.init());
}

TEST_CASE("NullImGuiBackend frame cycle does not crash", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	backend.init();
	backend.newFrame();
	backend.text("Hello");
	backend.render();
	backend.shutdown();
}

TEST_CASE("NullImGuiBackend frameCount increments on newFrame", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	REQUIRE(backend.frameCount() == 0);
	backend.newFrame();
	REQUIRE(backend.frameCount() == 1);
	backend.newFrame();
	REQUIRE(backend.frameCount() == 2);
}

TEST_CASE("NullImGuiBackend button returns false", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	REQUIRE_FALSE(backend.button("Click me"));
}

TEST_CASE("NullImGuiBackend slider returns false and does not change value", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	float val = 0.5f;
	REQUIRE_FALSE(backend.slider("Speed", val, 0.0f, 1.0f));
	REQUIRE(val == 0.5f);
}

TEST_CASE("NullImGuiBackend checkbox returns false", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	bool checked = true;
	REQUIRE_FALSE(backend.checkbox("Enable", checked));
	REQUIRE(checked == true);
}

TEST_CASE("NullImGuiBackend inputText returns false", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	std::string buf = "test";
	REQUIRE_FALSE(backend.inputText("Name", buf));
	REQUIRE(buf == "test");
}

TEST_CASE("NullImGuiBackend colorEdit returns false", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	float r = 1.0f, g = 0.0f, b = 0.0f, a = 1.0f;
	REQUIRE_FALSE(backend.colorEdit("Color", r, g, b, a));
}

TEST_CASE("NullImGuiBackend treeNode returns false", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	REQUIRE_FALSE(backend.treeNode("Node"));
	backend.treePop();
}

TEST_CASE("NullImGuiBackend beginTable returns false", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	REQUIRE_FALSE(backend.beginTable("Table", 3));
	backend.tableNextColumn();
	backend.endTable();
}

// --- Helper free function tests ---

TEST_CASE("labeledText with null backend does not crash", "[mitiru][imgui]")
{
	mitiru::imgui::labeledText(nullptr, "FPS", "60");
}

TEST_CASE("labeledText with valid backend does not crash", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	mitiru::imgui::labeledText(&backend, "FPS", "60");
}

TEST_CASE("evaluateButton returns not clicked for NullBackend", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	auto result = mitiru::imgui::evaluateButton(&backend, "OK");
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateButton with null backend returns default", "[mitiru][imgui]")
{
	auto result = mitiru::imgui::evaluateButton(nullptr, "OK");
	REQUIRE_FALSE(result.clicked);
}

TEST_CASE("evaluateSlider returns unchanged for NullBackend", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	auto result = mitiru::imgui::evaluateSlider(&backend, "Volume", 0.5f, 0.0f, 1.0f);
	REQUIRE_FALSE(result.changed);
	REQUIRE(result.valueFloat == 0.5f);
}

TEST_CASE("evaluateCheckbox returns unchanged for NullBackend", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	auto result = mitiru::imgui::evaluateCheckbox(&backend, "Mute", true);
	REQUIRE_FALSE(result.changed);
	REQUIRE(result.valueBool == true);
}

TEST_CASE("evaluateInputText returns unchanged for NullBackend", "[mitiru][imgui]")
{
	mitiru::imgui::NullImGuiBackend backend;
	auto result = mitiru::imgui::evaluateInputText(&backend, "Name", "Hello");
	REQUIRE_FALSE(result.changed);
	REQUIRE(result.valueStr == "Hello");
}

TEST_CASE("ImGuiWidgetResult default values", "[mitiru][imgui]")
{
	mitiru::imgui::ImGuiWidgetResult result;
	REQUIRE_FALSE(result.clicked);
	REQUIRE_FALSE(result.hovered);
	REQUIRE_FALSE(result.changed);
	REQUIRE(result.valueFloat == 0.0f);
	REQUIRE(result.valueInt == 0);
	REQUIRE_FALSE(result.valueBool);
	REQUIRE(result.valueStr.empty());
}
