/// @file TestQuaternion.cpp
/// @brief Quaternion.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/math/Quaternion.hpp"
#include "sgc/math/MathConstants.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("Quaternion identity", "[math][quaternion]")
{
	const auto id = sgc::Quaternionf::identity();
	REQUIRE(id.x == 0.0f);
	REQUIRE(id.y == 0.0f);
	REQUIRE(id.z == 0.0f);
	REQUIRE(id.w == 1.0f);
}

TEST_CASE("Quaternion default is identity", "[math][quaternion]")
{
	const sgc::Quaternionf q;
	REQUIRE(q == sgc::Quaternionf::identity());
}

TEST_CASE("Quaternion norm", "[math][quaternion]")
{
	const auto id = sgc::Quaternionf::identity();
	REQUIRE_THAT(id.norm(), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("Quaternion normalization", "[math][quaternion]")
{
	const sgc::Quaternionf q{1.0f, 2.0f, 3.0f, 4.0f};
	const auto n = q.normalized();
	REQUIRE_THAT(n.norm(), WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("Quaternion conjugate", "[math][quaternion]")
{
	const sgc::Quaternionf q{1.0f, 2.0f, 3.0f, 4.0f};
	const auto conj = q.conjugate();
	REQUIRE(conj.x == -1.0f);
	REQUIRE(conj.y == -2.0f);
	REQUIRE(conj.z == -3.0f);
	REQUIRE(conj.w == 4.0f);
}

TEST_CASE("Quaternion inverse", "[math][quaternion]")
{
	const auto q = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitY(), sgc::toRadians(45.0f));
	const auto inv = q.inversed();
	const auto result = q * inv;

	REQUIRE_THAT(result.x, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.w, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("Quaternion fromAxisAngle Y 90 degrees", "[math][quaternion]")
{
	const auto q = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitY(), sgc::toRadians(90.0f));
	const sgc::Vec3f p{0.0f, 0.0f, 1.0f};

	const auto result = q.rotate(p);
	REQUIRE_THAT(result.x, WithinAbs(1.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("Quaternion fromAxisAngle X 90 degrees", "[math][quaternion]")
{
	const auto q = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitX(), sgc::toRadians(90.0f));
	const sgc::Vec3f p{0.0f, 1.0f, 0.0f};

	const auto result = q.rotate(p);
	REQUIRE_THAT(result.x, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("Quaternion rotation composition", "[math][quaternion]")
{
	const auto q1 = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitY(), sgc::toRadians(90.0f));
	const auto q2 = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitY(), sgc::toRadians(90.0f));
	const auto combined = q1 * q2;

	const sgc::Vec3f p{1.0f, 0.0f, 0.0f};
	const auto result = combined.rotate(p);

	// 180 degrees around Y: (1,0,0) -> (-1,0,0)
	REQUIRE_THAT(result.x, WithinAbs(-1.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("Quaternion toMat4", "[math][quaternion]")
{
	const auto q = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitZ(), sgc::toRadians(90.0f));
	const auto mat = q.toMat4();

	const sgc::Vec3f p{1.0f, 0.0f, 0.0f};
	const auto result = mat.transformPoint(p);

	REQUIRE_THAT(result.x, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(1.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("Quaternion toMat4 matches rotate", "[math][quaternion]")
{
	const auto q = sgc::Quaternionf::fromAxisAngle(
		sgc::Vec3f{1.0f, 1.0f, 0.0f}.normalized(), sgc::toRadians(60.0f));
	const sgc::Vec3f p{1.0f, 2.0f, 3.0f};

	const auto rotated = q.rotate(p);
	const auto matResult = q.toMat4().transformPoint(p);

	REQUIRE_THAT(rotated.x, WithinAbs(matResult.x, 1e-4f));
	REQUIRE_THAT(rotated.y, WithinAbs(matResult.y, 1e-4f));
	REQUIRE_THAT(rotated.z, WithinAbs(matResult.z, 1e-4f));
}

TEST_CASE("Quaternion slerp endpoints", "[math][quaternion]")
{
	const auto a = sgc::Quaternionf::identity();
	const auto b = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitY(), sgc::toRadians(90.0f));

	const auto at0 = sgc::Quaternionf::slerp(a, b, 0.0f);
	REQUIRE_THAT(at0.x, WithinAbs(a.x, 1e-5f));
	REQUIRE_THAT(at0.y, WithinAbs(a.y, 1e-5f));
	REQUIRE_THAT(at0.z, WithinAbs(a.z, 1e-5f));
	REQUIRE_THAT(at0.w, WithinAbs(a.w, 1e-5f));

	const auto at1 = sgc::Quaternionf::slerp(a, b, 1.0f);
	REQUIRE_THAT(at1.x, WithinAbs(b.x, 1e-5f));
	REQUIRE_THAT(at1.y, WithinAbs(b.y, 1e-5f));
	REQUIRE_THAT(at1.z, WithinAbs(b.z, 1e-5f));
	REQUIRE_THAT(at1.w, WithinAbs(b.w, 1e-5f));
}

TEST_CASE("Quaternion slerp midpoint", "[math][quaternion]")
{
	const auto a = sgc::Quaternionf::identity();
	const auto b = sgc::Quaternionf::fromAxisAngle(sgc::Vec3f::unitY(), sgc::toRadians(90.0f));

	const auto mid = sgc::Quaternionf::slerp(a, b, 0.5f);
	REQUIRE_THAT(mid.norm(), WithinAbs(1.0f, 1e-5f));

	// 45 degrees around Y
	const sgc::Vec3f p{1.0f, 0.0f, 0.0f};
	const auto result = mid.rotate(p);
	REQUIRE_THAT(result.x, WithinAbs(std::cos(sgc::toRadians(45.0f)), 1e-4f));
	REQUIRE_THAT(result.z, WithinAbs(-std::sin(sgc::toRadians(45.0f)), 1e-4f));
}

TEST_CASE("Quaternion fromEuler and toEuler roundtrip", "[math][quaternion]")
{
	const float pitch = sgc::toRadians(30.0f);
	const float yaw = sgc::toRadians(45.0f);
	const float roll = sgc::toRadians(60.0f);

	const auto q = sgc::Quaternionf::fromEuler(pitch, yaw, roll);
	const auto euler = q.toEuler();

	REQUIRE_THAT(euler.x, WithinAbs(pitch, 1e-4f));
	REQUIRE_THAT(euler.y, WithinAbs(yaw, 1e-4f));
	REQUIRE_THAT(euler.z, WithinAbs(roll, 1e-4f));
}

TEST_CASE("Quaternion dot product", "[math][quaternion]")
{
	const auto a = sgc::Quaternionf::identity();
	REQUIRE_THAT(a.dot(a), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("Quaternion identity rotation leaves vector unchanged", "[math][quaternion]")
{
	const auto id = sgc::Quaternionf::identity();
	const sgc::Vec3f v{3.0f, 4.0f, 5.0f};
	const auto result = id.rotate(v);

	REQUIRE_THAT(result.x, WithinAbs(v.x, 1e-6f));
	REQUIRE_THAT(result.y, WithinAbs(v.y, 1e-6f));
	REQUIRE_THAT(result.z, WithinAbs(v.z, 1e-6f));
}
