/// @file TestDebugOverlay.cpp
/// @brief DebugOverlay.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <vector>

#include "sgc/debug/DebugOverlay.hpp"

using Catch::Approx;

// ── Mock implementation ─────────────────────────────────

namespace
{

/// @brief 描画呼び出しの記録
struct TextDrawCall
{
	std::string text;
	float fontSize;
	sgc::Vec2f pos;
	sgc::Colorf color;
	bool centered;
};

/// @brief テスト用のモックテキストレンダラー
class MockTextRenderer : public sgc::ITextRenderer
{
public:
	std::vector<TextDrawCall> calls;

	void drawText(
		std::string_view text, float fontSize,
		const sgc::Vec2f& pos, const sgc::Colorf& color) override
	{
		calls.push_back({std::string(text), fontSize, pos, color, false});
	}

	void drawTextCentered(
		std::string_view text, float fontSize,
		const sgc::Vec2f& center, const sgc::Colorf& color) override
	{
		calls.push_back({std::string(text), fontSize, center, color, true});
	}
};

} // anonymous namespace

// ── Tests ───────────────────────────────────────────────

TEST_CASE("DebugOverlay setEntry and removeEntry", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;

	overlay.setEntry("Entities", "100");
	REQUIRE(overlay.entryCount() == 1);

	overlay.setEntry("FPS", "60");
	REQUIRE(overlay.entryCount() == 2);

	// 同じラベルは上書き
	overlay.setEntry("Entities", "200");
	REQUIRE(overlay.entryCount() == 2);

	overlay.removeEntry("Entities");
	REQUIRE(overlay.entryCount() == 1);
}

TEST_CASE("DebugOverlay clearEntries", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;
	overlay.setEntry("A", "1");
	overlay.setEntry("B", "2");

	overlay.clearEntries();
	REQUIRE(overlay.entryCount() == 0);
}

TEST_CASE("DebugOverlay visibility", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;
	REQUIRE(overlay.isVisible());

	overlay.setVisible(false);
	REQUIRE_FALSE(overlay.isVisible());

	overlay.toggleVisible();
	REQUIRE(overlay.isVisible());

	overlay.toggleVisible();
	REQUIRE_FALSE(overlay.isVisible());
}

TEST_CASE("DebugOverlay draw renders FPS and entries", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;

	// FPSを計算させる
	for (int i = 0; i < 30; ++i)
	{
		overlay.update(1.0f / 60.0f);
	}

	overlay.setEntry("Score", "42");

	MockTextRenderer mock;
	overlay.draw(mock);

	// FPS行 + カスタムエントリ1行 = 2呼び出し
	REQUIRE(mock.calls.size() == 2);

	// 最初はFPS表示
	REQUIRE(mock.calls[0].text.find("FPS:") != std::string::npos);

	// 2番目はカスタムエントリ
	REQUIRE(mock.calls[1].text.find("Score") != std::string::npos);
	REQUIRE(mock.calls[1].text.find("42") != std::string::npos);
}

TEST_CASE("DebugOverlay draw skips when invisible", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;
	overlay.setVisible(false);
	overlay.update(0.016f);

	MockTextRenderer mock;
	overlay.draw(mock);

	REQUIRE(mock.calls.empty());
}

TEST_CASE("DebugOverlay FPS counter integration", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;
	REQUIRE(overlay.fpsCounter().fps() == 0.0f);

	// 十分な時間を更新してFPSが計算されることを確認
	for (int i = 0; i < 60; ++i)
	{
		overlay.update(1.0f / 60.0f);
	}

	REQUIRE(overlay.fpsCounter().fps() > 0.0f);
}

TEST_CASE("DebugOverlay draw uses custom position and fontSize", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;
	overlay.update(1.0f);  // FPSを更新

	MockTextRenderer mock;
	overlay.draw(mock, {20.0f, 30.0f}, 24.0f, 28.0f);

	REQUIRE_FALSE(mock.calls.empty());
	REQUIRE(mock.calls[0].pos.x == Approx(20.0f));
	REQUIRE(mock.calls[0].pos.y == Approx(30.0f));
	REQUIRE(mock.calls[0].fontSize == Approx(24.0f));
}

TEST_CASE("DebugOverlay draw with multiple entries uses lineSpacing", "[debug][overlay]")
{
	sgc::debug::DebugOverlay overlay;
	overlay.update(1.0f);
	overlay.setEntry("A", "1");
	overlay.setEntry("B", "2");

	MockTextRenderer mock;
	const float lineSpacing = 25.0f;
	overlay.draw(mock, {0.0f, 0.0f}, 16.0f, lineSpacing);

	// FPS + 2エントリ = 3呼び出し
	REQUIRE(mock.calls.size() == 3);

	// Y座標が行間隔ごとに増加する
	REQUIRE(mock.calls[0].pos.y == Approx(0.0f));
	REQUIRE(mock.calls[1].pos.y == Approx(lineSpacing));
	REQUIRE(mock.calls[2].pos.y == Approx(lineSpacing * 2.0f));
}
