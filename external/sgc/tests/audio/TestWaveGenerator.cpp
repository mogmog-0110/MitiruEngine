#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <algorithm>

#include "sgc/audio/WaveGenerator.hpp"

using Catch::Approx;

TEST_CASE("WaveGenerator sine produces correct sample count", "[audio][WaveGenerator]")
{
	sgc::audio::WaveGenerator gen;
	const auto samples = gen.generateSine(440.0f, 1.0f, 0.5f, 44100);
	REQUIRE(samples.size() == 22050);
}

TEST_CASE("WaveGenerator sine amplitude range", "[audio][WaveGenerator]")
{
	sgc::audio::WaveGenerator gen;
	const float amp = 0.8f;
	const auto samples = gen.generateSine(440.0f, amp, 1.0f, 44100);

	for (const auto s : samples)
	{
		REQUIRE(s >= -amp - 0.001f);
		REQUIRE(s <= amp + 0.001f);
	}
}

TEST_CASE("WaveGenerator square produces only +amp/-amp values", "[audio][WaveGenerator]")
{
	sgc::audio::WaveGenerator gen;
	const float amp = 0.5f;
	const auto samples = gen.generateSquare(440.0f, amp, 0.1f, 44100);

	for (const auto s : samples)
	{
		REQUIRE((s == Approx(amp) || s == Approx(-amp)));
	}
}

TEST_CASE("WaveGenerator triangle range check", "[audio][WaveGenerator]")
{
	sgc::audio::WaveGenerator gen;
	const float amp = 0.6f;
	const auto samples = gen.generateTriangle(440.0f, amp, 0.5f, 44100);

	for (const auto s : samples)
	{
		REQUIRE(s >= -amp - 0.001f);
		REQUIRE(s <= amp + 0.001f);
	}
}

TEST_CASE("WaveGenerator sawtooth range check", "[audio][WaveGenerator]")
{
	sgc::audio::WaveGenerator gen;
	const float amp = 0.7f;
	const auto samples = gen.generateSawtooth(440.0f, amp, 0.5f, 44100);

	for (const auto s : samples)
	{
		REQUIRE(s >= -amp - 0.001f);
		REQUIRE(s <= amp + 0.001f);
	}
}

TEST_CASE("WaveGenerator mix combines channels", "[audio][WaveGenerator]")
{
	// Two constant channels: all 0.5 and all -0.5 => average = 0
	std::vector<float> ch1(100, 0.5f);
	std::vector<float> ch2(100, -0.5f);

	const auto mixed = sgc::audio::WaveGenerator::mix({ch1, ch2});
	REQUIRE(mixed.size() == 100);

	for (const auto s : mixed)
	{
		REQUIRE(s == Approx(0.0f).margin(0.001f));
	}
}

TEST_CASE("WaveGenerator applyEnvelope starts at zero (attack)", "[audio][WaveGenerator]")
{
	// Constant signal of 1.0
	std::vector<float> samples(44100, 1.0f);

	const auto env = sgc::audio::WaveGenerator::applyEnvelope(
		samples, 0.1f, 0.1f, 0.5f, 0.1f, 44100);

	REQUIRE(env.size() == samples.size());

	// First sample should be 0 or very close to 0 (attack start)
	REQUIRE(env[0] == Approx(0.0f).margin(0.01f));

	// Middle (after attack+decay) should be near sustain
	const auto midIdx = static_cast<std::size_t>(44100 * 0.5f);
	REQUIRE(env[midIdx] == Approx(0.5f).margin(0.05f));
}

TEST_CASE("WaveGenerator applyLFO modulates amplitude", "[audio][WaveGenerator]")
{
	// Constant signal of 1.0
	std::vector<float> samples(44100, 1.0f);

	const auto modulated = sgc::audio::WaveGenerator::applyLFO(
		samples, 5.0f, 0.5f, 44100);

	REQUIRE(modulated.size() == samples.size());

	// Find min and max to verify modulation
	float minVal = *std::min_element(modulated.begin(), modulated.end());
	float maxVal = *std::max_element(modulated.begin(), modulated.end());

	// With depth 0.5, range should be approximately [0.5, 1.0]
	REQUIRE(minVal >= 0.4f);
	REQUIRE(minVal <= 0.6f);
	REQUIRE(maxVal >= 0.9f);
	REQUIRE(maxVal <= 1.05f);
}

TEST_CASE("WaveGenerator generate with WaveParams struct", "[audio][WaveGenerator]")
{
	sgc::audio::WaveGenerator gen;
	sgc::audio::WaveParams params{
		.type = sgc::audio::WaveType::Sine,
		.frequency = 440.0f,
		.amplitude = 0.5f,
		.phase = 0.0f,
		.duration = 0.1f
	};

	const auto samples = gen.generate(params, 44100);
	REQUIRE(samples.size() == 4410);

	for (const auto s : samples)
	{
		REQUIRE(s >= -0.501f);
		REQUIRE(s <= 0.501f);
	}
}

TEST_CASE("WaveGenerator noise is deterministic with seed", "[audio][WaveGenerator]")
{
	sgc::audio::WaveGenerator gen;
	const auto s1 = gen.generateNoise(1.0f, 0.01f, 44100, 42);
	const auto s2 = gen.generateNoise(1.0f, 0.01f, 44100, 42);

	REQUIRE(s1.size() == s2.size());
	for (std::size_t i = 0; i < s1.size(); ++i)
	{
		REQUIRE(s1[i] == s2[i]);
	}
}

TEST_CASE("WaveGenerator mix handles different length channels", "[audio][WaveGenerator]")
{
	std::vector<float> ch1(50, 1.0f);
	std::vector<float> ch2(100, 1.0f);

	const auto mixed = sgc::audio::WaveGenerator::mix({ch1, ch2});
	REQUIRE(mixed.size() == 100);

	// First 50 samples: average of 1.0 and 1.0 = 1.0
	REQUIRE(mixed[0] == Approx(1.0f));
	// Last 50 samples: only ch2 contributes, average = 1.0/2 = 0.5
	REQUIRE(mixed[75] == Approx(0.5f));
}
