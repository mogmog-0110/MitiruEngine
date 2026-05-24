/// @file TestAudioBusGraph.cpp
/// @brief AudioBusGraph.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/audio/AudioBusGraph.hpp"

using Catch::Approx;
using namespace sgc::audio;

// ── バス作成テスト ──────────────────────────────────────

TEST_CASE("AudioBusGraph createBus returns valid handle", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	REQUIRE(master != INVALID_BUS);
	REQUIRE(graph.busCount() == 1);
}

TEST_CASE("AudioBusGraph createBus rejects duplicate name", "[audio][busgraph]")
{
	AudioBusGraph graph;
	graph.createBus("Master");
	const auto dup = graph.createBus("Master");
	REQUIRE(dup == INVALID_BUS);
	REQUIRE(graph.busCount() == 1);
}

TEST_CASE("AudioBusGraph createBus with parent builds hierarchy", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	const auto bgm = graph.createBus("BGM", master);
	const auto se = graph.createBus("SE", master);

	REQUIRE(bgm != INVALID_BUS);
	REQUIRE(se != INVALID_BUS);
	REQUIRE(graph.busCount() == 3);

	const auto children = graph.getChildren(master);
	REQUIRE(children.size() == 2);
}

TEST_CASE("AudioBusGraph createBus rejects invalid parent", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("Orphan", 999);
	REQUIRE(h == INVALID_BUS);
}

// ── findBusテスト ───────────────────────────────────────

TEST_CASE("AudioBusGraph findBus returns handle by name", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	REQUIRE(graph.findBus("Master") == master);
	REQUIRE(graph.findBus("NonExistent") == INVALID_BUS);
}

// ── 音量テスト ──────────────────────────────────────────

TEST_CASE("AudioBusGraph default volume is 1.0", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("Master");
	REQUIRE(graph.getVolume(h) == Approx(1.0f));
}

TEST_CASE("AudioBusGraph setVolume clamps to [0, 1]", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("Test");

	graph.setVolume(h, 2.0f);
	REQUIRE(graph.getVolume(h) == Approx(1.0f));

	graph.setVolume(h, -1.0f);
	REQUIRE(graph.getVolume(h) == Approx(0.0f));
}

TEST_CASE("AudioBusGraph getBusVolume propagates through parent chain", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	const auto bgm = graph.createBus("BGM", master);

	graph.setVolume(master, 0.5f);
	graph.setVolume(bgm, 0.8f);

	REQUIRE(graph.getBusVolume(bgm) == Approx(0.4f));
}

TEST_CASE("AudioBusGraph getBusVolume three level hierarchy", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	const auto music = graph.createBus("Music", master);
	const auto bgm = graph.createBus("BGM", music);

	graph.setVolume(master, 0.5f);
	graph.setVolume(music, 0.8f);
	graph.setVolume(bgm, 0.5f);

	/// BGM実効 = 0.5 * 0.8 * 0.5 = 0.2
	REQUIRE(graph.getBusVolume(bgm) == Approx(0.2f));
}

TEST_CASE("AudioBusGraph root bus volume equals own volume", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	graph.setVolume(master, 0.7f);
	REQUIRE(graph.getBusVolume(master) == Approx(0.7f));
}

// ── ミュートテスト ──────────────────────────────────────

TEST_CASE("AudioBusGraph mute makes getBusVolume return 0", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	const auto bgm = graph.createBus("BGM", master);

	graph.setMuted(bgm, true);
	REQUIRE(graph.isMuted(bgm));
	REQUIRE(graph.getBusVolume(bgm) == Approx(0.0f));
}

TEST_CASE("AudioBusGraph parent mute propagates to children", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto master = graph.createBus("Master");
	const auto bgm = graph.createBus("BGM", master);

	graph.setMuted(master, true);
	REQUIRE_FALSE(graph.isMuted(bgm)); /// 子自身はミュートされていない
	REQUIRE(graph.getBusVolume(bgm) == Approx(0.0f)); /// 親がミュートなので0
}

// ── エフェクトチェーンテスト ─────────────────────────────

TEST_CASE("AudioBusGraph processEffects with no effects returns input", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("Test");
	REQUIRE(graph.processEffects(h, 0.5f) == Approx(0.5f));
}

TEST_CASE("AudioBusGraph single effect processes sample", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("Test");
	graph.addEffect(h, [](float s) { return s * 0.5f; });

	REQUIRE(graph.processEffects(h, 1.0f) == Approx(0.5f));
}

TEST_CASE("AudioBusGraph effect chain applies in order", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("Test");

	/// エフェクト1: 2倍
	graph.addEffect(h, [](float s) { return s * 2.0f; });
	/// エフェクト2: -0.3
	graph.addEffect(h, [](float s) { return s - 0.3f; });

	/// 1.0 * 2.0 - 0.3 = 1.7
	REQUIRE(graph.processEffects(h, 1.0f) == Approx(1.7f));
}

TEST_CASE("AudioBusGraph clearEffects removes all effects", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("Test");
	graph.addEffect(h, [](float s) { return s * 0.1f; });
	graph.addEffect(h, [](float s) { return s + 5.0f; });
	REQUIRE(graph.effectCount(h) == 2);

	graph.clearEffects(h);
	REQUIRE(graph.effectCount(h) == 0);
	REQUIRE(graph.processEffects(h, 1.0f) == Approx(1.0f));
}

TEST_CASE("AudioBusGraph addEffect to invalid bus returns false", "[audio][busgraph]")
{
	AudioBusGraph graph;
	REQUIRE_FALSE(graph.addEffect(999, [](float s) { return s; }));
}

// ── getBusNameテスト ─────────────────────────────────────

TEST_CASE("AudioBusGraph getBusName returns correct name", "[audio][busgraph]")
{
	AudioBusGraph graph;
	const auto h = graph.createBus("MyBus");
	REQUIRE(graph.getBusName(h) == "MyBus");
}

TEST_CASE("AudioBusGraph getBusName returns empty for invalid handle", "[audio][busgraph]")
{
	AudioBusGraph graph;
	REQUIRE(graph.getBusName(999).empty());
}

// ── clearテスト ─────────────────────────────────────────

TEST_CASE("AudioBusGraph clear removes all buses", "[audio][busgraph]")
{
	AudioBusGraph graph;
	graph.createBus("A");
	graph.createBus("B");
	REQUIRE(graph.busCount() == 2);

	graph.clear();
	REQUIRE(graph.busCount() == 0);
	REQUIRE(graph.findBus("A") == INVALID_BUS);
}

// ── getVolume/setVolume for invalid handles ─────────────

TEST_CASE("AudioBusGraph getVolume returns 0 for invalid handle", "[audio][busgraph]")
{
	AudioBusGraph graph;
	REQUIRE(graph.getVolume(INVALID_BUS) == Approx(0.0f));
}

TEST_CASE("AudioBusGraph setVolume returns false for invalid handle", "[audio][busgraph]")
{
	AudioBusGraph graph;
	REQUIRE_FALSE(graph.setVolume(999, 0.5f));
}
