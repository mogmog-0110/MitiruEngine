#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sgc/ui/Modal.hpp>

using namespace sgc;
using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

TEST_CASE("evaluateModal - dialog centered on screen", "[ui][modal]")
{
	ModalConfig config;
	config.screenSize = {800.0f, 600.0f};
	config.dialogWidth = 400.0f;
	config.dialogHeight = 200.0f;

	auto result = evaluateModal(config, {0.0f, 0.0f}, false, false);
	REQUIRE_THAT(result.dialogBounds.x(), WithinAbs(200.0f, 0.1f));
	REQUIRE_THAT(result.dialogBounds.y(), WithinAbs(200.0f, 0.1f));
	REQUIRE_THAT(result.dialogBounds.width(), WithinAbs(400.0f, 0.1f));
}

TEST_CASE("evaluateModal - OK button click", "[ui][modal]")
{
	ModalConfig config;
	config.screenSize = {800.0f, 600.0f};
	config.dialogWidth = 400.0f;
	config.dialogHeight = 200.0f;

	// まず位置を取得
	auto preview = evaluateModal(config, {0.0f, 0.0f}, false, false);
	Vec2f okCenter{
		preview.okButtonBounds.x() + preview.okButtonBounds.width() * 0.5f,
		preview.okButtonBounds.y() + preview.okButtonBounds.height() * 0.5f
	};

	auto result = evaluateModal(config, okCenter, true, true);
	REQUIRE(result.okClicked);
	REQUIRE_FALSE(result.cancelClicked);
	REQUIRE_FALSE(result.overlayClicked);
}

TEST_CASE("evaluateModal - cancel button click", "[ui][modal]")
{
	ModalConfig config;
	config.screenSize = {800.0f, 600.0f};
	config.hasCancel = true;

	auto preview = evaluateModal(config, {0.0f, 0.0f}, false, false);
	Vec2f cancelCenter{
		preview.cancelButtonBounds.x() + preview.cancelButtonBounds.width() * 0.5f,
		preview.cancelButtonBounds.y() + preview.cancelButtonBounds.height() * 0.5f
	};

	auto result = evaluateModal(config, cancelCenter, true, true);
	REQUIRE(result.cancelClicked);
	REQUIRE_FALSE(result.okClicked);
}

TEST_CASE("evaluateModal - overlay click outside dialog", "[ui][modal]")
{
	ModalConfig config;
	config.screenSize = {800.0f, 600.0f};
	config.closeOnOverlay = true;

	auto result = evaluateModal(config, {10.0f, 10.0f}, true, true);
	REQUIRE(result.overlayClicked);
	REQUIRE_FALSE(result.okClicked);
}

TEST_CASE("evaluateModal - overlay click disabled", "[ui][modal]")
{
	ModalConfig config;
	config.screenSize = {800.0f, 600.0f};
	config.closeOnOverlay = false;

	auto result = evaluateModal(config, {10.0f, 10.0f}, true, true);
	REQUIRE_FALSE(result.overlayClicked);
}

TEST_CASE("evaluateModal - no cancel mode has no cancel bounds", "[ui][modal]")
{
	ModalConfig config;
	config.hasCancel = false;

	auto result = evaluateModal(config, {0.0f, 0.0f}, false, false);
	REQUIRE(result.cancelButtonBounds.width() == 0.0f);
}

TEST_CASE("evaluateModal - title bounds position", "[ui][modal]")
{
	ModalConfig config;
	config.screenSize = {800.0f, 600.0f};
	config.dialogWidth = 400.0f;
	config.dialogHeight = 200.0f;
	config.titleHeight = 40.0f;

	auto result = evaluateModal(config, {0.0f, 0.0f}, false, false);
	REQUIRE_THAT(result.titleBounds.x(), WithinAbs(result.dialogBounds.x(), 0.01f));
	REQUIRE_THAT(result.titleBounds.y(), WithinAbs(result.dialogBounds.y(), 0.01f));
	REQUIRE_THAT(result.titleBounds.height(), WithinAbs(40.0f, 0.01f));
}
