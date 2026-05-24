/// @file TestRenderer.cpp
/// @brief IRenderer 抽象インターフェースのモックテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <vector>

#include "sgc/graphics/IRenderer.hpp"

namespace
{

/// @brief 描画コール記録エントリ
struct DrawCall
{
	std::string type;   ///< 描画種別
	sgc::Colorf color;  ///< 使用色
};

/// @brief 描画コールを記録するモックレンダラー
class MockRenderer : public sgc::IRenderer
{
public:
	std::vector<DrawCall> calls;  ///< 記録された描画コール

	void drawRect(const sgc::AABB2f&, const sgc::Colorf& color) override
	{
		calls.push_back({"rect", color});
	}

	void drawRectFrame(const sgc::AABB2f&, float, const sgc::Colorf& color) override
	{
		calls.push_back({"rectFrame", color});
	}

	void drawCircle(const sgc::Vec2f&, float, const sgc::Colorf& color) override
	{
		calls.push_back({"circle", color});
	}

	void drawCircleFrame(const sgc::Vec2f&, float, float, const sgc::Colorf& color) override
	{
		calls.push_back({"circleFrame", color});
	}

	void drawLine(const sgc::Vec2f&, const sgc::Vec2f&, float, const sgc::Colorf& color) override
	{
		calls.push_back({"line", color});
	}

	void drawTriangle(const sgc::Vec2f&, const sgc::Vec2f&, const sgc::Vec2f&, const sgc::Colorf& color) override
	{
		calls.push_back({"triangle", color});
	}

	void drawFadeOverlay(float, const sgc::Colorf& color) override
	{
		calls.push_back({"fadeOverlay", color});
	}

	void clearBackground(const sgc::Colorf& color) override
	{
		calls.push_back({"clearBg", color});
	}
};

} // anonymous namespace

// ── IRenderer contract tests ─────────────────────────────

TEST_CASE("MockRenderer records drawRect call", "[graphics][renderer]")
{
	MockRenderer renderer;
	sgc::AABB2f rect{{10.0f, 20.0f}, {100.0f, 80.0f}};
	renderer.drawRect(rect, sgc::Colorf::red());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "rect");
	REQUIRE(renderer.calls[0].color.r == Catch::Approx(1.0f));
	REQUIRE(renderer.calls[0].color.g == Catch::Approx(0.0f));
}

TEST_CASE("MockRenderer records drawRectFrame call", "[graphics][renderer]")
{
	MockRenderer renderer;
	sgc::AABB2f rect{{0.0f, 0.0f}, {50.0f, 50.0f}};
	renderer.drawRectFrame(rect, 2.0f, sgc::Colorf::blue());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "rectFrame");
}

TEST_CASE("MockRenderer records drawCircle call", "[graphics][renderer]")
{
	MockRenderer renderer;
	renderer.drawCircle({100.0f, 200.0f}, 25.0f, sgc::Colorf::green());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "circle");
}

TEST_CASE("MockRenderer records drawCircleFrame call", "[graphics][renderer]")
{
	MockRenderer renderer;
	renderer.drawCircleFrame({50.0f, 50.0f}, 30.0f, 1.5f, sgc::Colorf::white());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "circleFrame");
}

TEST_CASE("MockRenderer records drawLine call", "[graphics][renderer]")
{
	MockRenderer renderer;
	renderer.drawLine({0.0f, 0.0f}, {100.0f, 100.0f}, 2.0f, sgc::Colorf::yellow());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "line");
}

TEST_CASE("MockRenderer records drawTriangle call", "[graphics][renderer]")
{
	MockRenderer renderer;
	renderer.drawTriangle({0.0f, 0.0f}, {50.0f, 100.0f}, {100.0f, 0.0f}, sgc::Colorf::cyan());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "triangle");
}

TEST_CASE("MockRenderer records drawFadeOverlay call", "[graphics][renderer]")
{
	MockRenderer renderer;
	renderer.drawFadeOverlay(0.5f, sgc::Colorf::black());

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "fadeOverlay");
}

TEST_CASE("MockRenderer records clearBackground call", "[graphics][renderer]")
{
	MockRenderer renderer;
	renderer.clearBackground(sgc::Colorf{0.1f, 0.2f, 0.3f, 1.0f});

	REQUIRE(renderer.calls.size() == 1);
	REQUIRE(renderer.calls[0].type == "clearBg");
}

TEST_CASE("IRenderer polymorphic dispatch works", "[graphics][renderer]")
{
	MockRenderer mock;
	sgc::IRenderer& renderer = mock;

	renderer.drawRect(sgc::AABB2f{{0, 0}, {10, 10}}, sgc::Colorf::red());
	renderer.drawCircle({5, 5}, 3.0f, sgc::Colorf::blue());
	renderer.clearBackground(sgc::Colorf::white());

	REQUIRE(mock.calls.size() == 3);
	REQUIRE(mock.calls[0].type == "rect");
	REQUIRE(mock.calls[1].type == "circle");
	REQUIRE(mock.calls[2].type == "clearBg");
}
