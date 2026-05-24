#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Mat4.hpp"

TEST_CASE("Benchmark Vec3 operations", "[benchmark][math]")
{
	sgc::Vec3f a{1.0f, 2.0f, 3.0f};
	sgc::Vec3f b{4.0f, 5.0f, 6.0f};

	BENCHMARK("Vec3 dot product")
	{
		return a.dot(b);
	};

	BENCHMARK("Vec3 cross product")
	{
		return a.cross(b);
	};

	BENCHMARK("Vec3 normalize")
	{
		return a.normalized();
	};

	BENCHMARK("Vec3 addition")
	{
		return a + b;
	};

	BENCHMARK("Vec3 scalar multiply")
	{
		return a * 2.5f;
	};
}

TEST_CASE("Benchmark Mat4 operations", "[benchmark][math]")
{
	auto a = sgc::Mat4f::identity();
	auto b = sgc::Mat4f::identity();

	BENCHMARK("Mat4 multiply")
	{
		return a * b;
	};

	BENCHMARK("Mat4 identity creation")
	{
		return sgc::Mat4f::identity();
	};
}
