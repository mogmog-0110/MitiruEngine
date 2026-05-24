#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "sgc/debug/DebugDraw.hpp"

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

TEST_CASE("DebugDraw - pending count starts at zero", "[debug][DebugDraw]")
{
	sgc::debug::DebugDraw dd;
	REQUIRE(dd.pendingCount() == 0);
	REQUIRE(dd.isEnabled());
}

TEST_CASE("DebugDraw - drawRect adds pending command", "[debug][DebugDraw]")
{
	sgc::debug::DebugDraw dd;
	dd.drawRect({{0, 0}, {100, 50}}, sgc::Colorf::red());
	REQUIRE(dd.pendingCount() == 1);

	dd.drawRect({{10, 10}, {50, 30}}, sgc::Colorf::green());
	REQUIRE(dd.pendingCount() == 2);
}

TEST_CASE("DebugDraw - flush calls IRenderer and clears", "[debug][DebugDraw]")
{
	sgc::debug::DebugDraw dd;
	MockRenderer renderer;

	dd.drawRect({{0, 0}, {100, 50}}, sgc::Colorf::red());
	dd.drawCircle({50, 50}, 25.0f, sgc::Colorf::blue());
	dd.drawLine({0, 0}, {100, 100}, sgc::Colorf::green());

	REQUIRE(dd.pendingCount() == 3);

	dd.flush(renderer);

	// キューはクリアされている
	REQUIRE(dd.pendingCount() == 0);

	// レンダラーに描画コールが記録されている
	REQUIRE(renderer.calls.size() == 3);
	REQUIRE(renderer.calls[0].type == "rectFrame");
	REQUIRE(renderer.calls[1].type == "circleFrame");
	REQUIRE(renderer.calls[2].type == "line");
}

TEST_CASE("DebugDraw - setEnabled false suppresses flush output", "[debug][DebugDraw]")
{
	sgc::debug::DebugDraw dd;
	MockRenderer renderer;

	dd.setEnabled(false);
	REQUIRE_FALSE(dd.isEnabled());

	dd.drawRect({{0, 0}, {50, 50}}, sgc::Colorf::red());
	dd.drawLine({0, 0}, {10, 10}, sgc::Colorf::blue());
	REQUIRE(dd.pendingCount() == 2);

	dd.flush(renderer);

	// 描画はされないがキューはクリアされる
	REQUIRE(renderer.calls.empty());
	REQUIRE(dd.pendingCount() == 0);
}

TEST_CASE("DebugDraw - drawArrow produces line segments", "[debug][DebugDraw]")
{
	sgc::debug::DebugDraw dd;
	MockRenderer renderer;

	dd.drawArrow({0, 0}, {100, 0}, sgc::Colorf::yellow());
	REQUIRE(dd.pendingCount() == 1);

	dd.flush(renderer);

	// 矢印は本体1本 + 矢頭2本 = 3本の線分
	REQUIRE(renderer.calls.size() == 3);
	for (const auto& call : renderer.calls)
	{
		REQUIRE(call.type == "line");
	}
}

TEST_CASE("DebugDraw - drawPath with multiple points", "[debug][DebugDraw]")
{
	sgc::debug::DebugDraw dd;
	MockRenderer renderer;

	const sgc::Vec2f points[] = {{0, 0}, {10, 10}, {20, 0}, {30, 10}};
	dd.drawPath(points, sgc::Colorf::white());
	REQUIRE(dd.pendingCount() == 1);

	dd.flush(renderer);

	// 4点のパス → 3本の線分
	REQUIRE(renderer.calls.size() == 3);
	for (const auto& call : renderer.calls)
	{
		REQUIRE(call.type == "line");
	}
}

TEST_CASE("DebugDraw - clear removes pending without rendering", "[debug][DebugDraw]")
{
	sgc::debug::DebugDraw dd;
	MockRenderer renderer;

	dd.drawRect({{0, 0}, {50, 50}}, sgc::Colorf::red());
	dd.drawCircle({10, 10}, 5.0f, sgc::Colorf::blue());
	REQUIRE(dd.pendingCount() == 2);

	dd.clear();
	REQUIRE(dd.pendingCount() == 0);

	// flushしてもレンダラーには何も来ない
	dd.flush(renderer);
	REQUIRE(renderer.calls.empty());
}
