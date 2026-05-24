#include <catch2/catch_test_macros.hpp>

#include "sgc/vn/VNSkip.hpp"

using namespace sgc::vn;

TEST_CASE("VNFlowController - Default skip mode is Off", "[vn][skip]")
{
	VNFlowController flow;
	REQUIRE(flow.skipMode() == SkipMode::Off);
}

TEST_CASE("VNFlowController - Set and get skip mode", "[vn][skip]")
{
	VNFlowController flow;
	flow.setSkipMode(SkipMode::All);
	REQUIRE(flow.skipMode() == SkipMode::All);

	flow.setSkipMode(SkipMode::ReadOnly);
	REQUIRE(flow.skipMode() == SkipMode::ReadOnly);
}

TEST_CASE("VNFlowController - Mark text as read", "[vn][skip]")
{
	VNFlowController flow;
	const std::size_t hash = 12345;

	REQUIRE_FALSE(flow.isTextRead(hash));
	flow.markRead(hash);
	REQUIRE(flow.isTextRead(hash));
	REQUIRE(flow.readCount() == 1);
}

TEST_CASE("VNFlowController - Clear read history", "[vn][skip]")
{
	VNFlowController flow;
	flow.markRead(100);
	flow.markRead(200);
	REQUIRE(flow.readCount() == 2);

	flow.clearReadHistory();
	REQUIRE(flow.readCount() == 0);
	REQUIRE_FALSE(flow.isTextRead(100));
}

TEST_CASE("VNFlowController - ShouldAdvance with SkipMode::All", "[vn][skip]")
{
	VNFlowController flow;
	flow.setSkipMode(SkipMode::All);
	REQUIRE(flow.shouldAdvance(999, 10));
}

TEST_CASE("VNFlowController - ShouldAdvance with SkipMode::ReadOnly", "[vn][skip]")
{
	VNFlowController flow;
	flow.setSkipMode(SkipMode::ReadOnly);

	// Unread text should not advance
	REQUIRE_FALSE(flow.shouldAdvance(100, 10));

	// Read text should advance
	flow.markRead(100);
	REQUIRE(flow.shouldAdvance(100, 10));
}

TEST_CASE("VNFlowController - ShouldAdvance with auto mode", "[vn][skip]")
{
	VNFlowController flow;
	AutoModeConfig config;
	config.enabled = true;
	flow.setAutoModeConfig(config);

	REQUIRE(flow.shouldAdvance(999, 10));
}

TEST_CASE("VNFlowController - ShouldAdvance returns false when off", "[vn][skip]")
{
	VNFlowController flow;
	REQUIRE_FALSE(flow.shouldAdvance(999, 10));
}

TEST_CASE("VNFlowController - CalculateAutoDelay clamps to min", "[vn][skip]")
{
	VNFlowController flow;
	AutoModeConfig config;
	config.delayMsPerChar = 50;
	config.minDelayMs = 500;
	config.maxDelayMs = 5000;
	flow.setAutoModeConfig(config);

	// 5 chars * 50ms = 250ms < 500ms min
	REQUIRE(flow.calculateAutoDelay(5) == 500);
}

TEST_CASE("VNFlowController - CalculateAutoDelay clamps to max", "[vn][skip]")
{
	VNFlowController flow;
	AutoModeConfig config;
	config.delayMsPerChar = 50;
	config.minDelayMs = 500;
	config.maxDelayMs = 5000;
	flow.setAutoModeConfig(config);

	// 200 chars * 50ms = 10000ms > 5000ms max
	REQUIRE(flow.calculateAutoDelay(200) == 5000);
}

TEST_CASE("VNFlowController - CalculateAutoDelay normal range", "[vn][skip]")
{
	VNFlowController flow;
	AutoModeConfig config;
	config.delayMsPerChar = 50;
	config.minDelayMs = 500;
	config.maxDelayMs = 5000;
	flow.setAutoModeConfig(config);

	// 20 chars * 50ms = 1000ms (within range)
	REQUIRE(flow.calculateAutoDelay(20) == 1000);
}

TEST_CASE("VNFlowController - Auto mode config round trip", "[vn][skip]")
{
	VNFlowController flow;
	AutoModeConfig config;
	config.enabled = true;
	config.delayMsPerChar = 75;
	config.minDelayMs = 300;
	config.maxDelayMs = 8000;
	flow.setAutoModeConfig(config);

	const auto& got = flow.autoModeConfig();
	REQUIRE(got.enabled == true);
	REQUIRE(got.delayMsPerChar == 75);
	REQUIRE(got.minDelayMs == 300);
	REQUIRE(got.maxDelayMs == 8000);
}
