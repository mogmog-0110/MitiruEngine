/// @file TestMat3.cpp
/// @brief Mat3.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/math/Mat3.hpp"
#include "sgc/math/MathConstants.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("Mat3 identity", "[math][mat3]")
{
	const auto id = sgc::Mat3f::identity();
	REQUIRE(id.m[0][0] == 1.0f);
	REQUIRE(id.m[1][1] == 1.0f);
	REQUIRE(id.m[2][2] == 1.0f);
	REQUIRE(id.m[0][1] == 0.0f);
	REQUIRE(id.m[1][0] == 0.0f);
}

TEST_CASE("Mat3 identity * identity = identity", "[math][mat3]")
{
	const auto id = sgc::Mat3f::identity();
	const auto result = id * id;
	REQUIRE(result == id);
}

TEST_CASE("Mat3 identity * vector = vector", "[math][mat3]")
{
	const auto id = sgc::Mat3f::identity();
	const sgc::Vec3f v{1.0f, 2.0f, 3.0f};
	const auto result = id * v;
	REQUIRE(result == v);
}

TEST_CASE("Mat3 translation", "[math][mat3]")
{
	const auto t = sgc::Mat3f::translation({5.0f, 10.0f});
	const sgc::Vec2f p{1.0f, 2.0f};

	const auto result = t.transformPoint(p);
	REQUIRE(result.x == 6.0f);
	REQUIRE(result.y == 12.0f);
}

TEST_CASE("Mat3 transformVector ignores translation", "[math][mat3]")
{
	const auto t = sgc::Mat3f::translation({100.0f, 200.0f});
	const sgc::Vec2f dir{1.0f, 0.0f};

	const auto result = t.transformVector(dir);
	REQUIRE(result.x == 1.0f);
	REQUIRE(result.y == 0.0f);
}

TEST_CASE("Mat3 scaling", "[math][mat3]")
{
	const auto s = sgc::Mat3f::scaling({2.0f, 3.0f});
	const sgc::Vec2f p{4.0f, 5.0f};

	const auto result = s.transformPoint(p);
	REQUIRE(result.x == 8.0f);
	REQUIRE(result.y == 15.0f);
}

TEST_CASE("Mat3 uniform scaling", "[math][mat3]")
{
	const auto s = sgc::Mat3f::scaling(2.0f);
	const sgc::Vec2f p{3.0f, 4.0f};

	const auto result = s.transformPoint(p);
	REQUIRE(result.x == 6.0f);
	REQUIRE(result.y == 8.0f);
}

TEST_CASE("Mat3 rotation 90 degrees", "[math][mat3]")
{
	const auto r = sgc::Mat3f::rotation(sgc::toRadians(90.0f));
	const sgc::Vec2f p{1.0f, 0.0f};

	const auto result = r.transformPoint(p);
	REQUIRE_THAT(result.x, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("Mat3 determinant", "[math][mat3]")
{
	const auto id = sgc::Mat3f::identity();
	REQUIRE(id.determinant() == 1.0f);

	const auto s = sgc::Mat3f::scaling({2.0f, 3.0f});
	REQUIRE(s.determinant() == 6.0f);
}

TEST_CASE("Mat3 transpose", "[math][mat3]")
{
	const sgc::Mat3f m{
		1.0f, 2.0f, 3.0f,
		4.0f, 5.0f, 6.0f,
		7.0f, 8.0f, 9.0f
	};
	const auto t = m.transposed();

	REQUIRE(t.m[0][1] == 4.0f);
	REQUIRE(t.m[1][0] == 2.0f);
	REQUIRE(t.m[2][0] == 3.0f);
	REQUIRE(t.m[0][2] == 7.0f);
}

TEST_CASE("Mat3 inverse", "[math][mat3]")
{
	const auto t = sgc::Mat3f::translation({5.0f, 10.0f});
	const auto inv = t.inversed();
	const auto result = t * inv;

	const auto id = sgc::Mat3f::identity();
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			REQUIRE_THAT(result.m[r][c], WithinAbs(id.m[r][c], 1e-5f));
}

TEST_CASE("Mat3 combined transform", "[math][mat3]")
{
	const auto t = sgc::Mat3f::translation({10.0f, 0.0f});
	const auto s = sgc::Mat3f::scaling(2.0f);
	const auto combined = t * s;

	const sgc::Vec2f p{1.0f, 1.0f};
	const auto result = combined.transformPoint(p);
	REQUIRE_THAT(result.x, WithinAbs(12.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(2.0f, 1e-5f));
}

TEST_CASE("Mat3 scalar multiplication", "[math][mat3]")
{
	const auto id = sgc::Mat3f::identity();
	const auto scaled = id * 3.0f;
	REQUIRE(scaled.m[0][0] == 3.0f);
	REQUIRE(scaled.m[1][1] == 3.0f);

	const auto commutative = 3.0f * id;
	REQUIRE(commutative == scaled);
}

TEST_CASE("Mat3 addition and subtraction", "[math][mat3]")
{
	const auto a = sgc::Mat3f::identity();
	const auto b = sgc::Mat3f::identity();
	const auto sum = a + b;
	REQUIRE(sum.m[0][0] == 2.0f);

	const auto diff = sum - a;
	REQUIRE(diff == a);
}
