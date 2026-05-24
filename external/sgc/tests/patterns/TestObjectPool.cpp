/// @file TestObjectPool.cpp
/// @brief ObjectPool.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/patterns/ObjectPool.hpp"

namespace
{
struct Particle
{
	float x = 0.0f;
	float y = 0.0f;
	bool active = false;
};
} // namespace

TEST_CASE("ObjectPool acquire returns valid object", "[patterns][pool]")
{
	sgc::ObjectPool<Particle> pool(10);

	auto* p = pool.acquire();
	REQUIRE(p != nullptr);
	REQUIRE(pool.inUse() == 1);
	REQUIRE(pool.available() == 9);
}

TEST_CASE("ObjectPool release returns object to pool", "[patterns][pool]")
{
	sgc::ObjectPool<Particle> pool(5);

	auto* p = pool.acquire();
	pool.release(p);

	REQUIRE(pool.available() == 5);
	REQUIRE(pool.inUse() == 0);
}

TEST_CASE("ObjectPool acquire returns nullptr when empty", "[patterns][pool]")
{
	sgc::ObjectPool<Particle> pool(2);

	(void)pool.acquire();
	(void)pool.acquire();
	auto* p = pool.acquire();

	REQUIRE(p == nullptr);
}

TEST_CASE("ObjectPool capacity is correct", "[patterns][pool]")
{
	sgc::ObjectPool<Particle> pool(100);
	REQUIRE(pool.capacity() == 100);
	REQUIRE(pool.available() == 100);
}

TEST_CASE("ObjectPool with custom factory", "[patterns][pool]")
{
	sgc::ObjectPool<Particle> pool(5, []
	{
		return Particle{1.0f, 2.0f, true};
	});

	auto* p = pool.acquire();
	REQUIRE(p != nullptr);
	REQUIRE(p->x == 1.0f);
	REQUIRE(p->y == 2.0f);
	REQUIRE(p->active == true);
}
