/// @file TestSpatialAudio.cpp
/// @brief SpatialAudio.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/audio/SpatialAudio.hpp"

using Catch::Approx;

TEST_CASE("calculateAttenuation returns 1.0 within minDistance", "[audio][spatial]")
{
	const sgc::Vec3f src{0.5f, 0, 0};
	const sgc::Vec3f listener{0, 0, 0};

	const float atten = sgc::audio::calculateAttenuation(src, listener, 1.0f, 100.0f, 1.0f);
	REQUIRE(atten == Approx(1.0f));
}

TEST_CASE("calculateAttenuation returns 0.0 beyond maxDistance", "[audio][spatial]")
{
	const sgc::Vec3f src{200, 0, 0};
	const sgc::Vec3f listener{0, 0, 0};

	const float atten = sgc::audio::calculateAttenuation(src, listener, 1.0f, 100.0f, 1.0f);
	REQUIRE(atten == Approx(0.0f));
}

TEST_CASE("calculateAttenuation decreases with distance", "[audio][spatial]")
{
	const sgc::Vec3f listener{0, 0, 0};
	const sgc::Vec3f near{5, 0, 0};
	const sgc::Vec3f far{50, 0, 0};

	const float nearAtten = sgc::audio::calculateAttenuation(near, listener, 1.0f, 100.0f, 1.0f);
	const float farAtten = sgc::audio::calculateAttenuation(far, listener, 1.0f, 100.0f, 1.0f);

	REQUIRE(nearAtten > farAtten);
	REQUIRE(nearAtten > 0.0f);
	REQUIRE(farAtten > 0.0f);
}

TEST_CASE("calculatePan returns 0.0 for centered source", "[audio][spatial]")
{
	sgc::audio::AudioListener listener;
	listener.position = {0, 0, 0};
	listener.forward = {0, 0, -1};
	listener.up = {0, 1, 0};

	/// 正面の音源
	const sgc::Vec3f src{0, 0, -10};
	const float pan = sgc::audio::calculatePan(src, listener);
	REQUIRE(pan == Approx(0.0f).margin(0.01f));
}

TEST_CASE("calculatePan returns positive for right source", "[audio][spatial]")
{
	sgc::audio::AudioListener listener;
	listener.position = {0, 0, 0};
	listener.forward = {0, 0, -1};
	listener.up = {0, 1, 0};

	/// 右方向 = forward × up = (0,0,-1) × (0,1,0) = (1, 0, 0)
	const sgc::Vec3f srcRight{10, 0, 0};
	const float pan = sgc::audio::calculatePan(srcRight, listener);
	REQUIRE(pan > 0.0f);
}

TEST_CASE("calculateDopplerShift returns 1.0 when stationary", "[audio][spatial]")
{
	const sgc::Vec3f srcPos{10, 0, 0};
	const sgc::Vec3f srcVel{0, 0, 0};
	const sgc::Vec3f listenerPos{0, 0, 0};
	const sgc::Vec3f listenerVel{0, 0, 0};

	const float shift = sgc::audio::calculateDopplerShift(
		srcPos, srcVel, listenerPos, listenerVel);
	REQUIRE(shift == Approx(1.0f));
}

TEST_CASE("calculateDopplerShift increases when approaching", "[audio][spatial]")
{
	const sgc::Vec3f srcPos{10, 0, 0};
	const sgc::Vec3f srcVel{0, 0, 0};
	const sgc::Vec3f listenerPos{0, 0, 0};
	/// リスナーが音源に向かって移動
	const sgc::Vec3f listenerVel{50, 0, 0};

	const float shift = sgc::audio::calculateDopplerShift(
		srcPos, srcVel, listenerPos, listenerVel);
	REQUIRE(shift > 1.0f);
}
