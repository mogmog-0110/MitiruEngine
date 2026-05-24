#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/math/SimdVec.hpp"

using Catch::Approx;

TEST_CASE("SimdVec4 default constructor is zero", "[math][simd]")
{
	sgc::simd::Vec4 v;
	REQUIRE(v[0] == Approx(0.0f));
	REQUIRE(v[1] == Approx(0.0f));
	REQUIRE(v[2] == Approx(0.0f));
	REQUIRE(v[3] == Approx(0.0f));
}

TEST_CASE("SimdVec4 component constructor", "[math][simd]")
{
	sgc::simd::Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
	REQUIRE(v.x() == Approx(1.0f));
	REQUIRE(v.y() == Approx(2.0f));
	REQUIRE(v.z() == Approx(3.0f));
	REQUIRE(v.w() == Approx(4.0f));
}

TEST_CASE("SimdVec4 broadcast constructor", "[math][simd]")
{
	sgc::simd::Vec4 v{5.0f};
	REQUIRE(v[0] == Approx(5.0f));
	REQUIRE(v[1] == Approx(5.0f));
	REQUIRE(v[2] == Approx(5.0f));
	REQUIRE(v[3] == Approx(5.0f));
}

TEST_CASE("SimdVec4 addition", "[math][simd]")
{
	sgc::simd::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
	sgc::simd::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
	auto c = a + b;

	REQUIRE(c[0] == Approx(6.0f));
	REQUIRE(c[1] == Approx(8.0f));
	REQUIRE(c[2] == Approx(10.0f));
	REQUIRE(c[3] == Approx(12.0f));
}

TEST_CASE("SimdVec4 subtraction", "[math][simd]")
{
	sgc::simd::Vec4 a{5.0f, 6.0f, 7.0f, 8.0f};
	sgc::simd::Vec4 b{1.0f, 2.0f, 3.0f, 4.0f};
	auto c = a - b;

	REQUIRE(c[0] == Approx(4.0f));
	REQUIRE(c[1] == Approx(4.0f));
	REQUIRE(c[2] == Approx(4.0f));
	REQUIRE(c[3] == Approx(4.0f));
}

TEST_CASE("SimdVec4 element-wise multiplication", "[math][simd]")
{
	sgc::simd::Vec4 a{2.0f, 3.0f, 4.0f, 5.0f};
	sgc::simd::Vec4 b{3.0f, 4.0f, 5.0f, 6.0f};
	auto c = a * b;

	REQUIRE(c[0] == Approx(6.0f));
	REQUIRE(c[1] == Approx(12.0f));
	REQUIRE(c[2] == Approx(20.0f));
	REQUIRE(c[3] == Approx(30.0f));
}

TEST_CASE("SimdVec4 scalar multiplication", "[math][simd]")
{
	sgc::simd::Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
	auto c = v * 2.0f;

	REQUIRE(c[0] == Approx(2.0f));
	REQUIRE(c[1] == Approx(4.0f));
	REQUIRE(c[2] == Approx(6.0f));
	REQUIRE(c[3] == Approx(8.0f));
}

TEST_CASE("SimdVec4 scalar left multiply", "[math][simd]")
{
	sgc::simd::Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
	auto c = 3.0f * v;

	REQUIRE(c[0] == Approx(3.0f));
	REQUIRE(c[1] == Approx(6.0f));
	REQUIRE(c[2] == Approx(9.0f));
	REQUIRE(c[3] == Approx(12.0f));
}

TEST_CASE("SimdVec4 compound assignment", "[math][simd]")
{
	sgc::simd::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
	sgc::simd::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};

	a += b;
	REQUIRE(a[0] == Approx(6.0f));
	REQUIRE(a[1] == Approx(8.0f));

	a -= b;
	REQUIRE(a[0] == Approx(1.0f));
	REQUIRE(a[1] == Approx(2.0f));
}

TEST_CASE("SimdVec4 dot product", "[math][simd]")
{
	sgc::simd::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
	sgc::simd::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};

	// 1*5 + 2*6 + 3*7 + 4*8 = 5 + 12 + 21 + 32 = 70
	REQUIRE(sgc::simd::dot(a, b) == Approx(70.0f));
}

TEST_CASE("SimdVec4 length", "[math][simd]")
{
	sgc::simd::Vec4 v{3.0f, 4.0f, 0.0f, 0.0f};
	REQUIRE(sgc::simd::length(v) == Approx(5.0f));
}

TEST_CASE("SimdVec4 lengthSquared", "[math][simd]")
{
	sgc::simd::Vec4 v{3.0f, 4.0f, 0.0f, 0.0f};
	REQUIRE(sgc::simd::lengthSquared(v) == Approx(25.0f));
}

TEST_CASE("SimdVec4 normalize", "[math][simd]")
{
	sgc::simd::Vec4 v{3.0f, 4.0f, 0.0f, 0.0f};
	auto n = sgc::simd::normalize(v);

	REQUIRE(n[0] == Approx(0.6f));
	REQUIRE(n[1] == Approx(0.8f));
	REQUIRE(n[2] == Approx(0.0f));
	REQUIRE(n[3] == Approx(0.0f));
	REQUIRE(sgc::simd::length(n) == Approx(1.0f));
}

TEST_CASE("SimdVec4 normalize zero vector", "[math][simd]")
{
	sgc::simd::Vec4 v;
	auto n = sgc::simd::normalize(v);

	REQUIRE(n[0] == Approx(0.0f));
	REQUIRE(n[1] == Approx(0.0f));
	REQUIRE(n[2] == Approx(0.0f));
	REQUIRE(n[3] == Approx(0.0f));
}
