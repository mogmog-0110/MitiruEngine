/// @file TestMat4.cpp
/// @brief Mat4.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/math/Mat4.hpp"
#include "sgc/math/MathConstants.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("Mat4 identity", "[math][mat4]")
{
	const auto id = sgc::Mat4f::identity();
	REQUIRE(id.m[0][0] == 1.0f);
	REQUIRE(id.m[1][1] == 1.0f);
	REQUIRE(id.m[2][2] == 1.0f);
	REQUIRE(id.m[3][3] == 1.0f);
	REQUIRE(id.m[0][3] == 0.0f);
}

TEST_CASE("Mat4 identity * identity = identity", "[math][mat4]")
{
	const auto id = sgc::Mat4f::identity();
	REQUIRE(id * id == id);
}

TEST_CASE("Mat4 identity * vec4 = vec4", "[math][mat4]")
{
	const auto id = sgc::Mat4f::identity();
	const sgc::Vec4f v{1.0f, 2.0f, 3.0f, 1.0f};
	REQUIRE(id * v == v);
}

TEST_CASE("Mat4 translation", "[math][mat4]")
{
	const auto t = sgc::Mat4f::translation({5.0f, 10.0f, 15.0f});
	const sgc::Vec3f p{1.0f, 2.0f, 3.0f};

	const auto result = t.transformPoint(p);
	REQUIRE(result.x == 6.0f);
	REQUIRE(result.y == 12.0f);
	REQUIRE(result.z == 18.0f);
}

TEST_CASE("Mat4 transformVector ignores translation", "[math][mat4]")
{
	const auto t = sgc::Mat4f::translation({100.0f, 200.0f, 300.0f});
	const sgc::Vec3f dir{1.0f, 0.0f, 0.0f};

	const auto result = t.transformVector(dir);
	REQUIRE(result.x == 1.0f);
	REQUIRE(result.y == 0.0f);
	REQUIRE(result.z == 0.0f);
}

TEST_CASE("Mat4 scaling", "[math][mat4]")
{
	const auto s = sgc::Mat4f::scaling({2.0f, 3.0f, 4.0f});
	const sgc::Vec3f p{1.0f, 1.0f, 1.0f};

	const auto result = s.transformPoint(p);
	REQUIRE(result.x == 2.0f);
	REQUIRE(result.y == 3.0f);
	REQUIRE(result.z == 4.0f);
}

TEST_CASE("Mat4 uniform scaling", "[math][mat4]")
{
	const auto s = sgc::Mat4f::scaling(3.0f);
	const sgc::Vec3f p{1.0f, 2.0f, 3.0f};

	const auto result = s.transformPoint(p);
	REQUIRE(result.x == 3.0f);
	REQUIRE(result.y == 6.0f);
	REQUIRE(result.z == 9.0f);
}

TEST_CASE("Mat4 rotationX 90 degrees", "[math][mat4]")
{
	const auto r = sgc::Mat4f::rotationX(sgc::toRadians(90.0f));
	const sgc::Vec3f p{0.0f, 1.0f, 0.0f};

	const auto result = r.transformPoint(p);
	REQUIRE_THAT(result.x, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("Mat4 rotationY 90 degrees", "[math][mat4]")
{
	const auto r = sgc::Mat4f::rotationY(sgc::toRadians(90.0f));
	const sgc::Vec3f p{0.0f, 0.0f, 1.0f};

	const auto result = r.transformPoint(p);
	REQUIRE_THAT(result.x, WithinAbs(1.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("Mat4 rotationZ 90 degrees", "[math][mat4]")
{
	const auto r = sgc::Mat4f::rotationZ(sgc::toRadians(90.0f));
	const sgc::Vec3f p{1.0f, 0.0f, 0.0f};

	const auto result = r.transformPoint(p);
	REQUIRE_THAT(result.x, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(1.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("Mat4 rotationAxis around Y", "[math][mat4]")
{
	const auto r = sgc::Mat4f::rotationAxis(sgc::Vec3f::unitY(), sgc::toRadians(90.0f));
	const sgc::Vec3f p{0.0f, 0.0f, 1.0f};

	const auto result = r.transformPoint(p);
	REQUIRE_THAT(result.x, WithinAbs(1.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("Mat4 determinant", "[math][mat4]")
{
	const auto id = sgc::Mat4f::identity();
	REQUIRE(id.determinant() == 1.0f);

	const auto s = sgc::Mat4f::scaling({2.0f, 3.0f, 4.0f});
	REQUIRE_THAT(s.determinant(), WithinAbs(24.0f, 1e-5f));
}

TEST_CASE("Mat4 transpose", "[math][mat4]")
{
	const auto t = sgc::Mat4f::translation({1.0f, 2.0f, 3.0f});
	const auto tr = t.transposed();

	REQUIRE(tr.m[0][3] == 0.0f);
	REQUIRE(tr.m[3][0] == 1.0f);
	REQUIRE(tr.m[3][1] == 2.0f);
	REQUIRE(tr.m[3][2] == 3.0f);
}

TEST_CASE("Mat4 inverse of translation", "[math][mat4]")
{
	const auto t = sgc::Mat4f::translation({5.0f, 10.0f, 15.0f});
	const auto inv = t.inversed();
	const auto result = t * inv;

	const auto id = sgc::Mat4f::identity();
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			REQUIRE_THAT(result.m[r][c], WithinAbs(id.m[r][c], 1e-4f));
}

TEST_CASE("Mat4 inverse of rotation", "[math][mat4]")
{
	const auto r = sgc::Mat4f::rotationZ(sgc::toRadians(45.0f));
	const auto inv = r.inversed();
	const auto result = r * inv;

	const auto id = sgc::Mat4f::identity();
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 4; ++col)
			REQUIRE_THAT(result.m[row][col], WithinAbs(id.m[row][col], 1e-4f));
}

TEST_CASE("Mat4 lookAt", "[math][mat4]")
{
	const sgc::Vec3f eye{0.0f, 0.0f, 5.0f};
	const sgc::Vec3f target{0.0f, 0.0f, 0.0f};
	const sgc::Vec3f up{0.0f, 1.0f, 0.0f};

	const auto view = sgc::Mat4f::lookAt(eye, target, up);

	// 原点はカメラの前方に見えるはず
	const auto origin = view.transformPoint(sgc::Vec3f::zero());
	REQUIRE_THAT(origin.x, WithinAbs(0.0f, 1e-5f));
	REQUIRE_THAT(origin.y, WithinAbs(0.0f, 1e-5f));
	REQUIRE(origin.z < 0.0f);
}

TEST_CASE("Mat4 orthographic", "[math][mat4]")
{
	const auto ortho = sgc::Mat4f::orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);

	// 中心点は原点にマッピングされる
	REQUIRE(ortho.m[0][0] != 0.0f);
	REQUIRE(ortho.m[1][1] != 0.0f);
	REQUIRE(ortho.m[2][2] != 0.0f);
	REQUIRE(ortho.m[3][3] == 1.0f);
}

TEST_CASE("Mat4 perspective", "[math][mat4]")
{
	const auto persp = sgc::Mat4f::perspective(sgc::toRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

	REQUIRE(persp.m[0][0] != 0.0f);
	REQUIRE(persp.m[1][1] != 0.0f);
	REQUIRE(persp.m[3][2] == -1.0f);
	REQUIRE(persp.m[3][3] == 0.0f);
}

TEST_CASE("Mat4 combined transform", "[math][mat4]")
{
	const auto t = sgc::Mat4f::translation({10.0f, 0.0f, 0.0f});
	const auto s = sgc::Mat4f::scaling(2.0f);
	const auto combined = t * s;

	const sgc::Vec3f p{1.0f, 1.0f, 1.0f};
	const auto result = combined.transformPoint(p);
	REQUIRE_THAT(result.x, WithinAbs(12.0f, 1e-5f));
	REQUIRE_THAT(result.y, WithinAbs(2.0f, 1e-5f));
	REQUIRE_THAT(result.z, WithinAbs(2.0f, 1e-5f));
}

TEST_CASE("Mat4 singular matrix determinant is zero", "[math][mat4]")
{
	// 全ての行が同じ → 特異行列
	sgc::Mat4f singular{};
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			singular.m[r][c] = 1.0f;
	REQUIRE_THAT(singular.determinant(), WithinAbs(0.0f, 1e-5f));
}

TEST_CASE("Mat4 transpose of transpose is original", "[math][mat4]")
{
	const auto t = sgc::Mat4f::translation({3.0f, 7.0f, 11.0f});
	const auto tt = t.transposed().transposed();

	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			REQUIRE_THAT(tt.m[r][c], WithinAbs(t.m[r][c], 1e-5f));
}
