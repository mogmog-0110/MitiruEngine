/// @file TestTypeTraits.cpp
/// @brief TypeTraits.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/types/TypeTraits.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"
#include "sgc/math/Vec4.hpp"
#include "sgc/math/Mat3.hpp"
#include "sgc/math/Mat4.hpp"
#include "sgc/math/Quaternion.hpp"

TEST_CASE("IsVec2 detects Vec2 types", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_VEC2<sgc::Vec2f>);
	STATIC_REQUIRE(sgc::IS_VEC2<sgc::Vec2d>);
	STATIC_REQUIRE(sgc::IS_VEC2<sgc::Vec2i>);
	STATIC_REQUIRE_FALSE(sgc::IS_VEC2<sgc::Vec3f>);
	STATIC_REQUIRE_FALSE(sgc::IS_VEC2<float>);
}

TEST_CASE("IsVec3 detects Vec3 types", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_VEC3<sgc::Vec3f>);
	STATIC_REQUIRE(sgc::IS_VEC3<sgc::Vec3d>);
	STATIC_REQUIRE_FALSE(sgc::IS_VEC3<sgc::Vec2f>);
	STATIC_REQUIRE_FALSE(sgc::IS_VEC3<int>);
}

TEST_CASE("IsVec4 detects Vec4 types", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_VEC4<sgc::Vec4f>);
	STATIC_REQUIRE_FALSE(sgc::IS_VEC4<sgc::Vec3f>);
}

TEST_CASE("IsVec detects any Vec type", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_VEC<sgc::Vec2f>);
	STATIC_REQUIRE(sgc::IS_VEC<sgc::Vec3f>);
	STATIC_REQUIRE(sgc::IS_VEC<sgc::Vec4f>);
	STATIC_REQUIRE_FALSE(sgc::IS_VEC<sgc::Mat3f>);
	STATIC_REQUIRE_FALSE(sgc::IS_VEC<float>);
}

TEST_CASE("IsMat3 detects Mat3 types", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_MAT3<sgc::Mat3f>);
	STATIC_REQUIRE(sgc::IS_MAT3<sgc::Mat3d>);
	STATIC_REQUIRE_FALSE(sgc::IS_MAT3<sgc::Mat4f>);
}

TEST_CASE("IsMat4 detects Mat4 types", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_MAT4<sgc::Mat4f>);
	STATIC_REQUIRE_FALSE(sgc::IS_MAT4<sgc::Mat3f>);
}

TEST_CASE("IsMat detects any Mat type", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_MAT<sgc::Mat3f>);
	STATIC_REQUIRE(sgc::IS_MAT<sgc::Mat4f>);
	STATIC_REQUIRE_FALSE(sgc::IS_MAT<sgc::Vec3f>);
}

TEST_CASE("IsQuaternion detects Quaternion types", "[types][traits]")
{
	STATIC_REQUIRE(sgc::IS_QUATERNION<sgc::Quaternionf>);
	STATIC_REQUIRE(sgc::IS_QUATERNION<sgc::Quaterniond>);
	STATIC_REQUIRE_FALSE(sgc::IS_QUATERNION<sgc::Vec4f>);
}

TEST_CASE("MathType concept accepts all math types", "[types][traits]")
{
	STATIC_REQUIRE(sgc::MathType<sgc::Vec2f>);
	STATIC_REQUIRE(sgc::MathType<sgc::Vec3f>);
	STATIC_REQUIRE(sgc::MathType<sgc::Vec4f>);
	STATIC_REQUIRE(sgc::MathType<sgc::Mat3f>);
	STATIC_REQUIRE(sgc::MathType<sgc::Mat4f>);
	STATIC_REQUIRE(sgc::MathType<sgc::Quaternionf>);
	STATIC_REQUIRE_FALSE(sgc::MathType<float>);
	STATIC_REQUIRE_FALSE(sgc::MathType<int>);
}

TEST_CASE("ElementTypeT extracts element type", "[types][traits]")
{
	STATIC_REQUIRE(std::is_same_v<sgc::ElementTypeT<sgc::Vec2f>, float>);
	STATIC_REQUIRE(std::is_same_v<sgc::ElementTypeT<sgc::Vec3d>, double>);
	STATIC_REQUIRE(std::is_same_v<sgc::ElementTypeT<sgc::Mat4f>, float>);
	STATIC_REQUIRE(std::is_same_v<sgc::ElementTypeT<sgc::Quaternionf>, float>);
}

TEST_CASE("VEC_DIMENSION returns correct dimensions", "[types][traits]")
{
	STATIC_REQUIRE(sgc::VEC_DIMENSION<sgc::Vec2f> == 2);
	STATIC_REQUIRE(sgc::VEC_DIMENSION<sgc::Vec3f> == 3);
	STATIC_REQUIRE(sgc::VEC_DIMENSION<sgc::Vec4f> == 4);
}
