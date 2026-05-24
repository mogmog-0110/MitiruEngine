#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/effects/ParticleSystem.hpp"

using Catch::Approx;

TEST_CASE("ParticleSystem starts empty", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	REQUIRE(ps.activeCount() == 0);
	REQUIRE(ps.maxParticles() == 100);
}

TEST_CASE("ParticleSystem emit creates particles", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.positionX = 50.0f;
	config.positionY = 50.0f;
	config.lifetime = 2.0f;
	ps.setConfig(config);

	ps.emit(10);
	REQUIRE(ps.activeCount() == 10);
}

TEST_CASE("ParticleSystem respects max particles", "[effects][particles]")
{
	sgc::ParticleSystem ps(5);
	ps.emit(10);
	REQUIRE(ps.activeCount() == 5);
}

TEST_CASE("ParticleSystem update moves particles", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.positionX = 0.0f;
	config.positionY = 0.0f;
	config.speed = 100.0f;
	config.lifetime = 5.0f;
	config.rate = 0.0f; // no auto emit
	ps.setConfig(config);

	ps.emit(1);
	ps.update(1.0f);

	auto particles = ps.activeParticles();
	REQUIRE(particles.size() == 1);
	// Particle should have moved from origin
	bool moved = (particles[0].x != 0.0f || particles[0].y != 0.0f);
	REQUIRE(moved);
}

TEST_CASE("ParticleSystem removes dead particles", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.lifetime = 0.5f;
	config.rate = 0.0f;
	ps.setConfig(config);

	ps.emit(5);
	REQUIRE(ps.activeCount() == 5);

	ps.update(1.0f); // All should die
	REQUIRE(ps.activeCount() == 0);
}

TEST_CASE("ParticleSystem auto emit based on rate", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.rate = 10.0f; // 10 per second
	config.lifetime = 5.0f;
	ps.setConfig(config);

	ps.update(1.0f); // Should emit ~10
	REQUIRE(ps.activeCount() >= 8);
	REQUIRE(ps.activeCount() <= 12);
}

TEST_CASE("ParticleSystem affector modifies particles", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.lifetime = 5.0f;
	config.speed = 0.0f;
	config.rate = 0.0f;
	ps.setConfig(config);

	// 重力アフェクター
	ps.addAffector([](sgc::Particle& p, float dt) {
		p.vy += 9.8f * dt;
	});

	ps.emit(1);
	ps.update(1.0f);

	auto particles = ps.activeParticles();
	REQUIRE(particles.size() == 1);
	REQUIRE(particles[0].vy > 0.0f);
}

TEST_CASE("ParticleSystem size interpolation", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.lifetime = 1.0f;
	config.startSize = 10.0f;
	config.endSize = 0.0f;
	config.speed = 0.0f;
	config.rate = 0.0f;
	ps.setConfig(config);

	ps.emit(1);
	ps.update(0.5f); // Half lifetime

	auto particles = ps.activeParticles();
	REQUIRE(particles.size() == 1);
	REQUIRE(particles[0].size == Approx(5.0f).margin(1.0f));
}

TEST_CASE("ParticleSystem alpha interpolation", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.lifetime = 1.0f;
	config.startA = 1.0f;
	config.endA = 0.0f;
	config.speed = 0.0f;
	config.rate = 0.0f;
	ps.setConfig(config);

	ps.emit(1);
	ps.update(0.5f);

	auto particles = ps.activeParticles();
	REQUIRE(particles.size() == 1);
	REQUIRE(particles[0].a == Approx(0.5f).margin(0.1f));
}

TEST_CASE("ParticleSystem clear removes all", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	ps.emit(10);
	ps.clear();
	REQUIRE(ps.activeCount() == 0);
}

TEST_CASE("ParticleSystem clearAffectors", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	ps.addAffector([](sgc::Particle&, float) {});
	ps.clearAffectors();
	// Just verify it doesn't crash
	ps.emit(1);
	sgc::EmitterConfig config;
	config.lifetime = 5.0f;
	config.rate = 0.0f;
	ps.setConfig(config);
	ps.update(0.1f);
	REQUIRE(ps.activeCount() >= 1);
}

TEST_CASE("ParticleSystem activeParticles span", "[effects][particles]")
{
	sgc::ParticleSystem ps(100);
	sgc::EmitterConfig config;
	config.positionX = 42.0f;
	config.lifetime = 5.0f;
	config.rate = 0.0f;
	ps.setConfig(config);

	ps.emit(3);
	auto span = ps.activeParticles();
	REQUIRE(span.size() == 3);
	for (const auto& p : span)
	{
		REQUIRE(p.x == 42.0f);
	}
}
