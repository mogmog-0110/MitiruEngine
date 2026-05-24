#include <catch2/catch_test_macros.hpp>

#include "sgc/effects/ParticlePool.hpp"

TEST_CASE("ParticlePool initial state", "[effects]")
{
	sgc::ParticlePool pool(5, 100);

	REQUIRE(pool.totalCount() == 5);
	REQUIRE(pool.availableCount() == 5);
	REQUIRE(pool.activeCount() == 0);
	REQUIRE(pool.particlesPerSystem() == 100);
}

TEST_CASE("ParticlePool acquire returns valid system", "[effects]")
{
	sgc::ParticlePool pool(3, 50);

	auto* ps = pool.acquire();
	REQUIRE(ps != nullptr);
	REQUIRE(pool.availableCount() == 2);
	REQUIRE(pool.activeCount() == 1);

	REQUIRE(ps->maxParticles() == 50);
}

TEST_CASE("ParticlePool acquire returns nullptr when exhausted", "[effects]")
{
	sgc::ParticlePool pool(2, 50);

	auto* ps1 = pool.acquire();
	auto* ps2 = pool.acquire();
	auto* ps3 = pool.acquire();

	REQUIRE(ps1 != nullptr);
	REQUIRE(ps2 != nullptr);
	REQUIRE(ps3 == nullptr);
	REQUIRE(pool.availableCount() == 0);
}

TEST_CASE("ParticlePool release resets system", "[effects]")
{
	sgc::ParticlePool pool(2, 100);

	auto* ps = pool.acquire();
	REQUIRE(ps != nullptr);

	// パーティクルを放出
	sgc::EmitterConfig config;
	config.rate = 0.0f;
	ps->setConfig(config);
	ps->emit(10);
	REQUIRE(ps->activeCount() == 10);

	// 返却後はクリアされている
	pool.release(ps);
	REQUIRE(pool.availableCount() == 2);
	REQUIRE(pool.activeCount() == 0);
}

TEST_CASE("ParticlePool release ignores null pointer", "[effects]")
{
	sgc::ParticlePool pool(2, 50);

	pool.release(nullptr);
	REQUIRE(pool.availableCount() == 2);
}

TEST_CASE("ParticlePool release ignores foreign pointer", "[effects]")
{
	sgc::ParticlePool pool(2, 50);
	sgc::ParticleSystem foreign(100);

	pool.release(&foreign);
	REQUIRE(pool.availableCount() == 2);
	REQUIRE(pool.activeCount() == 0);
}

TEST_CASE("ParticlePool double release is safe", "[effects]")
{
	sgc::ParticlePool pool(2, 50);

	auto* ps = pool.acquire();
	pool.release(ps);
	pool.release(ps);  // 二重解放

	REQUIRE(pool.availableCount() == 2);
	REQUIRE(pool.activeCount() == 0);
}

TEST_CASE("ParticlePool acquire after release reuses system", "[effects]")
{
	sgc::ParticlePool pool(1, 50);

	auto* ps1 = pool.acquire();
	REQUIRE(ps1 != nullptr);
	REQUIRE(pool.acquire() == nullptr);

	pool.release(ps1);
	auto* ps2 = pool.acquire();

	REQUIRE(ps2 != nullptr);
	REQUIRE(ps2 == ps1);
	REQUIRE(ps2->activeCount() == 0);
}
