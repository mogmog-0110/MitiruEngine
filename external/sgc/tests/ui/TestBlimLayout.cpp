#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/ui/BlimLayout.hpp"

using namespace sgc;

static constexpr auto approx = [](float expected) {
	return Catch::Matchers::WithinAbs(expected, 1.0f);
};

// ── computeBlimLayout ──────────────────────────────────

TEST_CASE("BlimLayout - default config produces valid panels", "[ui][blim]")
{
	auto layout = computeBlimLayout(1280.0f, 720.0f, {});

	// メインパネルとサイドパネルが非ゼロ
	REQUIRE(layout.mainPanel.width() > 0.0f);
	REQUIRE(layout.mainPanel.height() > 0.0f);
	REQUIRE(layout.sidePanel.width() > 0.0f);
	REQUIRE(layout.sidePanel.height() > 0.0f);
	REQUIRE(layout.bottomBar.height() > 0.0f);
}

TEST_CASE("BlimLayout - main and side panels dont overlap", "[ui][blim]")
{
	auto layout = computeBlimLayout(1280.0f, 720.0f, {});

	// サイドが右にある場合、メインの右端 < サイドの左端
	REQUIRE(layout.mainPanel.right() <= layout.sidePanel.left() + 1.0f);
}

TEST_CASE("BlimLayout - side on left", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.sideOnRight = false;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);

	// サイドが左にある場合、サイドの右端 <= メインの左端
	REQUIRE(layout.sidePanel.right() <= layout.mainPanel.left() + 1.0f);
}

TEST_CASE("BlimLayout - fixed sidebar width", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.sidebarWidth = 300.0f;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);

	REQUIRE_THAT(layout.sidePanel.width(), approx(300.0f));
}

TEST_CASE("BlimLayout - ratio based sidebar", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.mainRatio = 0.7f;
	config.sidebarWidth = 0.0f;  // 比率ベース

	auto layout = computeBlimLayout(1000.0f, 600.0f, config);

	// サイドバー = 画面幅 * (1 - 0.7) = 300
	REQUIRE_THAT(layout.sidePanel.width(), approx(300.0f));
}

TEST_CASE("BlimLayout - bottom bar full width", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.bottomHeight = 50.0f;
	config.bottomMainOnly = false;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);

	// ボトムバーが全幅
	REQUIRE_THAT(layout.bottomBar.width(), approx(1280.0f));
}

TEST_CASE("BlimLayout - bottom bar main only", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.bottomHeight = 50.0f;
	config.bottomMainOnly = true;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);

	// ボトムバーがメインパネル幅
	REQUIRE_THAT(layout.bottomBar.width(), approx(layout.mainPanel.width()));
}

TEST_CASE("BlimLayout - no bottom bar when height is zero", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.bottomHeight = 0.0f;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);

	// メインパネルが画面高さ分ある
	REQUIRE_THAT(layout.mainPanel.height(), approx(720.0f));
}

TEST_CASE("BlimLayout - character slot position", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.characterSize = 80.0f;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);

	REQUIRE_THAT(layout.characterSlot.width(), approx(80.0f));
	REQUIRE_THAT(layout.characterSlot.height(), approx(80.0f));
	// キャラスロットはメインパネル左下付近
	REQUIRE(layout.characterSlot.x() >= layout.mainPanel.x());
}

TEST_CASE("BlimLayout - no character when size is zero", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.characterSize = 0.0f;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);
	REQUIRE_THAT(layout.characterSlot.width(), approx(0.0f));
}

TEST_CASE("BlimLayout - margin shrinks available area", "[ui][blim]")
{
	BlimLayoutConfig config;
	config.margin = 20.0f;

	auto layout = computeBlimLayout(1280.0f, 720.0f, config);

	REQUIRE_THAT(layout.fullArea.width(), approx(1240.0f));  // 1280 - 40
	REQUIRE_THAT(layout.fullArea.height(), approx(680.0f));  // 720 - 40
}

// ── splitSidePanelVertical ─────────────────────────────

TEST_CASE("BlimLayout - split side panel vertical", "[ui][blim]")
{
	Rectf side{900, 0, 300, 600};
	auto [top, bottom] = splitSidePanelVertical(side, 0.5f, 4.0f);

	REQUIRE_THAT(top.height(), approx(298.0f));  // (600-4)*0.5
	REQUIRE_THAT(bottom.height(), approx(298.0f));
	REQUIRE(top.bottom() < bottom.top() + 0.1f);
}

// ── panelContent ───────────────────────────────────────

TEST_CASE("BlimLayout - panelContent applies padding", "[ui][blim]")
{
	Rectf panel{100, 50, 400, 300};
	auto content = panelContent(panel, 10.0f);

	REQUIRE_THAT(content.x(), approx(110.0f));
	REQUIRE_THAT(content.y(), approx(60.0f));
	REQUIRE_THAT(content.width(), approx(380.0f));
	REQUIRE_THAT(content.height(), approx(280.0f));
}

// ── panelWithHeader ────────────────────────────────────

TEST_CASE("BlimLayout - panelWithHeader splits correctly", "[ui][blim]")
{
	Rectf panel{0, 0, 300, 500};
	auto [header, content] = panelWithHeader(panel, 30.0f, 2.0f);

	REQUIRE_THAT(header.height(), approx(30.0f));
	REQUIRE_THAT(content.y(), approx(32.0f));
	REQUIRE_THAT(content.height(), approx(468.0f));
}
