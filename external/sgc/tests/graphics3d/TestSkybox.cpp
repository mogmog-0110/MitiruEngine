#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/graphics3d/Skybox.hpp"

using namespace sgc;
using namespace sgc::graphics3d;

// ── CubeFace ────────────────────────────────────────────────

TEST_CASE("CubeFace - enumeration values", "[graphics3d][skybox]")
{
	REQUIRE(static_cast<int>(CubeFace::PositiveX) == 0);
	REQUIRE(static_cast<int>(CubeFace::NegativeX) == 1);
	REQUIRE(static_cast<int>(CubeFace::PositiveY) == 2);
	REQUIRE(static_cast<int>(CubeFace::NegativeY) == 3);
	REQUIRE(static_cast<int>(CubeFace::PositiveZ) == 4);
	REQUIRE(static_cast<int>(CubeFace::NegativeZ) == 5);
	REQUIRE(static_cast<int>(CubeFace::Count) == 6);
}

// ── generateSkyboxVertices ──────────────────────────────────

TEST_CASE("generateSkyboxVertices - 36 vertices", "[graphics3d][skybox]")
{
	const auto verts = generateSkyboxVertices();
	REQUIRE(verts.size() == 36);
}

TEST_CASE("generateSkyboxVertices - all on unit cube", "[graphics3d][skybox]")
{
	const auto verts = generateSkyboxVertices();
	for (const auto& v : verts)
	{
		// 各成分は-1または+1
		REQUIRE((v.position.x == Catch::Approx(-1.0f) || v.position.x == Catch::Approx(1.0f)));
		REQUIRE((v.position.y == Catch::Approx(-1.0f) || v.position.y == Catch::Approx(1.0f)));
		REQUIRE((v.position.z == Catch::Approx(-1.0f) || v.position.z == Catch::Approx(1.0f)));
	}
}

// ── createSkyboxData ────────────────────────────────────────

TEST_CASE("createSkyboxData - face textures assigned", "[graphics3d][skybox]")
{
	const auto skybox = createSkyboxData(
		"right", "left", "top", "bottom", "front", "back");

	REQUIRE(skybox.faceTextures[0] == "right");   // PositiveX
	REQUIRE(skybox.faceTextures[1] == "left");    // NegativeX
	REQUIRE(skybox.faceTextures[2] == "top");     // PositiveY
	REQUIRE(skybox.faceTextures[3] == "bottom");  // NegativeY
	REQUIRE(skybox.faceTextures[4] == "front");   // PositiveZ
	REQUIRE(skybox.faceTextures[5] == "back");    // NegativeZ
}

TEST_CASE("createSkyboxData - has 36 vertices", "[graphics3d][skybox]")
{
	const auto skybox = createSkyboxData(
		"px", "nx", "py", "ny", "pz", "nz");
	REQUIRE(skybox.vertices.size() == 36);
}

// ── SkyParams ───────────────────────────────────────────────

TEST_CASE("SkyParams - default values", "[graphics3d][skybox]")
{
	SkyParams params;
	REQUIRE(params.zenithColor.r == Catch::Approx(0.1f));
	REQUIRE(params.horizonColor.r == Catch::Approx(0.6f));
	REQUIRE(params.sunSize == Catch::Approx(0.05f));
	REQUIRE(params.sunIntensity == Catch::Approx(2.0f));
}

// ── calculateSkyColor ───────────────────────────────────────

TEST_CASE("calculateSkyColor - zenith is bluer than horizon", "[graphics3d][skybox]")
{
	SkyParams params;
	const Vec3f sunDir = Vec3f{0.0f, 1.0f, 0.0f};  // 太陽は真上

	// 天頂を見上げた場合
	const auto zenithColor = calculateSkyColor(sunDir, Vec3f::up(), params);
	// 地平線を見た場合
	const auto horizonColor = calculateSkyColor(sunDir, Vec3f{1.0f, 0.0f, 0.0f}, params);

	// 天頂はより青い（b成分が高い）
	REQUIRE(zenithColor.b > horizonColor.b - 0.01f);
}

TEST_CASE("calculateSkyColor - looking at sun is brightest", "[graphics3d][skybox]")
{
	SkyParams params;
	const Vec3f sunDir = Vec3f{0.0f, 0.5f, -0.866f}.normalized();

	// 太陽を直接見た場合
	const auto sunColor = calculateSkyColor(sunDir, sunDir, params);
	// 太陽と反対方向を見た場合
	const auto awayColor = calculateSkyColor(sunDir, -sunDir, params);

	// 太陽方向の方が明るい
	const float sunBrightness = sunColor.r + sunColor.g + sunColor.b;
	const float awayBrightness = awayColor.r + awayColor.g + awayColor.b;
	REQUIRE(sunBrightness > awayBrightness);
}

TEST_CASE("calculateSkyColor - output clamped to [0,1]", "[graphics3d][skybox]")
{
	SkyParams params;
	params.sunIntensity = 100.0f;  // 非常に高い太陽強度
	const Vec3f sunDir = Vec3f::up();

	const auto color = calculateSkyColor(sunDir, sunDir, params);
	REQUIRE(color.r >= 0.0f);
	REQUIRE(color.r <= 1.0f);
	REQUIRE(color.g >= 0.0f);
	REQUIRE(color.g <= 1.0f);
	REQUIRE(color.b >= 0.0f);
	REQUIRE(color.b <= 1.0f);
}

TEST_CASE("calculateSkyColor - alpha is always 1", "[graphics3d][skybox]")
{
	SkyParams params;
	const auto color = calculateSkyColor(Vec3f::up(), Vec3f::right(), params);
	REQUIRE(color.a == Catch::Approx(1.0f));
}

// ── getCubeFaceDirection ────────────────────────────────────

TEST_CASE("getCubeFaceDirection - correct axes", "[graphics3d][skybox]")
{
	const auto posX = getCubeFaceDirection(CubeFace::PositiveX);
	REQUIRE(posX.x == Catch::Approx(1.0f));
	REQUIRE(posX.y == Catch::Approx(0.0f));
	REQUIRE(posX.z == Catch::Approx(0.0f));

	const auto negY = getCubeFaceDirection(CubeFace::NegativeY);
	REQUIRE(negY.x == Catch::Approx(0.0f));
	REQUIRE(negY.y == Catch::Approx(-1.0f));
	REQUIRE(negY.z == Catch::Approx(0.0f));
}

TEST_CASE("getCubeFaceDirection - all 6 faces are unit vectors", "[graphics3d][skybox]")
{
	for (int i = 0; i < static_cast<int>(CubeFace::Count); ++i)
	{
		const auto dir = getCubeFaceDirection(static_cast<CubeFace>(i));
		REQUIRE(dir.lengthSquared() == Catch::Approx(1.0f));
	}
}

// ── getCubeFaceUp ───────────────────────────────────────────

TEST_CASE("getCubeFaceUp - up vectors are unit length", "[graphics3d][skybox]")
{
	for (int i = 0; i < static_cast<int>(CubeFace::Count); ++i)
	{
		const auto up = getCubeFaceUp(static_cast<CubeFace>(i));
		REQUIRE(up.lengthSquared() == Catch::Approx(1.0f));
	}
}

TEST_CASE("getCubeFaceUp - up perpendicular to direction", "[graphics3d][skybox]")
{
	for (int i = 0; i < static_cast<int>(CubeFace::Count); ++i)
	{
		const auto face = static_cast<CubeFace>(i);
		const auto dir = getCubeFaceDirection(face);
		const auto up = getCubeFaceUp(face);
		REQUIRE(dir.dot(up) == Catch::Approx(0.0f).margin(0.001f));
	}
}
