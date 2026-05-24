/// @file TestSpatialAudio3D.cpp
/// @brief SpatialAudio3D.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/audio/SpatialAudio3D.hpp"

using Catch::Approx;
using namespace sgc::audio;

// ── 距離減衰テスト ──────────────────────────────────────

TEST_CASE("Linear attenuation returns 1.0 within minDist", "[audio][spatial3d]")
{
	const float vol = calculateVolume(0.5f, 1.0f, 100.0f, 1.0f, AttenuationModel::Linear);
	REQUIRE(vol == Approx(1.0f));
}

TEST_CASE("Linear attenuation returns 0.0 beyond maxDist", "[audio][spatial3d]")
{
	const float vol = calculateVolume(150.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::Linear);
	REQUIRE(vol == Approx(0.0f));
}

TEST_CASE("Linear attenuation returns 0.5 at midpoint", "[audio][spatial3d]")
{
	/// midpoint = (1+100)/2 = 50.5
	const float vol = calculateVolume(50.5f, 1.0f, 100.0f, 1.0f, AttenuationModel::Linear);
	REQUIRE(vol == Approx(0.5f));
}

TEST_CASE("InverseDistance attenuation decreases with distance", "[audio][spatial3d]")
{
	const float near = calculateVolume(5.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::InverseDistance);
	const float far = calculateVolume(50.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::InverseDistance);
	REQUIRE(near > far);
	REQUIRE(near > 0.0f);
	REQUIRE(far > 0.0f);
}

TEST_CASE("ExponentialDecay attenuation decreases with distance", "[audio][spatial3d]")
{
	const float near = calculateVolume(5.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::ExponentialDecay);
	const float far = calculateVolume(50.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::ExponentialDecay);
	REQUIRE(near > far);
	REQUIRE(near > 0.0f);
	REQUIRE(far > 0.0f);
}

TEST_CASE("All models return 1.0 at minDist boundary", "[audio][spatial3d]")
{
	REQUIRE(calculateVolume(1.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::Linear) == Approx(1.0f));
	REQUIRE(calculateVolume(1.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::InverseDistance) == Approx(1.0f));
	REQUIRE(calculateVolume(1.0f, 1.0f, 100.0f, 1.0f, AttenuationModel::ExponentialDecay) == Approx(1.0f));
}

// ── パンニングテスト ────────────────────────────────────

TEST_CASE("calculatePan returns 0 for front source", "[audio][spatial3d]")
{
	Listener listener;
	listener.position = {0, 0, 0};
	listener.forward = {0, 0, -1};
	listener.up = {0, 1, 0};

	const float pan = calculatePan(listener, {0, 0, -10});
	REQUIRE(pan == Approx(0.0f).margin(0.01f));
}

TEST_CASE("calculatePan returns positive for right source", "[audio][spatial3d]")
{
	Listener listener;
	listener.position = {0, 0, 0};
	listener.forward = {0, 0, -1};
	listener.up = {0, 1, 0};

	const float pan = calculatePan(listener, {10, 0, 0});
	REQUIRE(pan > 0.0f);
}

TEST_CASE("calculatePan returns negative for left source", "[audio][spatial3d]")
{
	Listener listener;
	listener.position = {0, 0, 0};
	listener.forward = {0, 0, -1};
	listener.up = {0, 1, 0};

	const float pan = calculatePan(listener, {-10, 0, 0});
	REQUIRE(pan < 0.0f);
}

TEST_CASE("calculatePan returns 0 for coincident positions", "[audio][spatial3d]")
{
	Listener listener;
	listener.position = {5, 5, 5};
	const float pan = calculatePan(listener, {5, 5, 5});
	REQUIRE(pan == Approx(0.0f));
}

// ── ドップラーテスト ────────────────────────────────────

TEST_CASE("Doppler returns 1.0 when both stationary", "[audio][spatial3d]")
{
	Listener listener;
	SpatialSource source;
	source.position = {10, 0, 0};

	const float shift = calculateDoppler(listener, source);
	REQUIRE(shift == Approx(1.0f));
}

TEST_CASE("Doppler increases when listener approaches", "[audio][spatial3d]")
{
	Listener listener;
	listener.velocity = {50, 0, 0};

	SpatialSource source;
	source.position = {100, 0, 0};

	const float shift = calculateDoppler(listener, source);
	REQUIRE(shift > 1.0f);
}

TEST_CASE("Doppler decreases when source moves away", "[audio][spatial3d]")
{
	Listener listener;

	SpatialSource source;
	source.position = {10, 0, 0};
	source.velocity = {50, 0, 0};

	const float shift = calculateDoppler(listener, source);
	REQUIRE(shift < 1.0f);
}

// ── calculateSpatial統合テスト ──────────────────────────

TEST_CASE("calculateSpatial produces combined result", "[audio][spatial3d]")
{
	Listener listener;
	listener.forward = {0, 0, -1};
	listener.up = {0, 1, 0};

	SpatialSource source;
	source.position = {10, 0, 0};
	source.baseVolume = 0.8f;

	const auto result = calculateSpatial(
		listener, source, AttenuationModel::InverseDistance, 1.0f, 100.0f);

	REQUIRE(result.volume > 0.0f);
	REQUIRE(result.volume <= 0.8f);
	REQUIRE(result.pan > 0.0f); /// 右方向
	REQUIRE(result.dopplerShift == Approx(1.0f)); /// 静止
}

// ── SpatialAudioMixerテスト ─────────────────────────────

TEST_CASE("SpatialAudioMixer add and remove sources", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;
	REQUIRE(mixer.sourceCount() == 0);

	SpatialSource src;
	src.position = {10, 0, 0};
	const uint32_t id = mixer.addSource(src);
	REQUIRE(mixer.sourceCount() == 1);

	REQUIRE(mixer.removeSource(id));
	REQUIRE(mixer.sourceCount() == 0);
}

TEST_CASE("SpatialAudioMixer update source position", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;
	SpatialSource src;
	src.position = {0, 0, 0};
	const uint32_t id = mixer.addSource(src);

	REQUIRE(mixer.updateSource(id, {20, 0, 0}));
	const auto found = mixer.getSource(id);
	REQUIRE(found.has_value());
	REQUIRE(found->position.x == Approx(20.0f));
}

TEST_CASE("SpatialAudioMixer calculate returns result for valid source", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;
	mixer.setListener(Listener{{0, 0, 0}, {0, 0, -1}, {0, 1, 0}});

	SpatialSource src;
	src.position = {5, 0, 0};
	src.baseVolume = 1.0f;
	const uint32_t id = mixer.addSource(src);

	const auto result = mixer.calculate(id, AttenuationModel::Linear, 1.0f, 100.0f);
	REQUIRE(result.has_value());
	REQUIRE(result->volume > 0.0f);
}

TEST_CASE("SpatialAudioMixer calculate returns nullopt for invalid id", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;
	const auto result = mixer.calculate(999);
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("SpatialAudioMixer calculateAll returns all active sources", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;

	SpatialSource s1;
	s1.position = {10, 0, 0};
	mixer.addSource(s1);

	SpatialSource s2;
	s2.position = {0, 0, 10};
	mixer.addSource(s2);

	const auto results = mixer.calculateAll(AttenuationModel::Linear, 1.0f, 100.0f);
	REQUIRE(results.size() == 2);
}

TEST_CASE("SpatialAudioMixer getSource returns nullopt for missing id", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;
	REQUIRE_FALSE(mixer.getSource(42).has_value());
}

TEST_CASE("SpatialAudioMixer clear removes all sources", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;
	SpatialSource s;
	mixer.addSource(s);
	mixer.addSource(s);
	REQUIRE(mixer.sourceCount() == 2);

	mixer.clear();
	REQUIRE(mixer.sourceCount() == 0);
}

TEST_CASE("SpatialAudioMixer removeSource returns false for invalid id", "[audio][spatial3d]")
{
	SpatialAudioMixer mixer;
	REQUIRE_FALSE(mixer.removeSource(999));
}
