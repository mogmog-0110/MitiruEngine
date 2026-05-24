#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/graphics3d/ShadowMap.hpp"

using namespace sgc;
using namespace sgc::graphics3d;

// ── ShadowCascade ───────────────────────────────────────────

TEST_CASE("ShadowCascade - default values", "[graphics3d][shadow]")
{
	ShadowCascade cascade;
	REQUIRE(cascade.nearPlane == Catch::Approx(0.0f));
	REQUIRE(cascade.farPlane == Catch::Approx(0.0f));
}

TEST_CASE("ShadowCascade - equality", "[graphics3d][shadow]")
{
	ShadowCascade a;
	ShadowCascade b;
	REQUIRE(a == b);

	b.nearPlane = 1.0f;
	REQUIRE_FALSE(a == b);
}

// ── ShadowConfig ────────────────────────────────────────────

TEST_CASE("ShadowConfig - default values", "[graphics3d][shadow]")
{
	ShadowConfig config;
	REQUIRE(config.cascadeCount == 4);
	REQUIRE(config.shadowDistance == Catch::Approx(100.0f));
	REQUIRE(config.splitLambda == Catch::Approx(0.5f));
	REQUIRE(config.bias == Catch::Approx(0.005f));
}

// ── calculateLightViewMatrix ────────────────────────────────

TEST_CASE("calculateLightViewMatrix - downward light", "[graphics3d][shadow]")
{
	const auto view = calculateLightViewMatrix(
		Vec3f{0.0f, -1.0f, 0.0f},
		Vec3f{0.0f, 0.0f, 0.0f});

	// 下向きライトの場合、上方向がほぼY軸並行なのでforward代替が使われる
	// 結果は有効な行列であること
	REQUIRE_FALSE(view == Mat4f{});
}

TEST_CASE("calculateLightViewMatrix - diagonal light", "[graphics3d][shadow]")
{
	const Vec3f lightDir = Vec3f{-1.0f, -1.0f, -1.0f}.normalized();
	const auto view = calculateLightViewMatrix(lightDir, Vec3f::zero());

	REQUIRE_FALSE(view == Mat4f{});
}

// ── calculateLightSpaceMatrix ───────────────────────────────

TEST_CASE("calculateLightSpaceMatrix - produces valid matrix", "[graphics3d][shadow]")
{
	const Vec3f lightDir = Vec3f{0.0f, -1.0f, 0.5f}.normalized();
	const auto lsm = calculateLightSpaceMatrix(
		lightDir, Vec3f::zero(), 50.0f, 200.0f);

	REQUIRE_FALSE(lsm == Mat4f{});
}

// ── calculateSplitDistances ─────────────────────────────────

TEST_CASE("calculateSplitDistances - correct count", "[graphics3d][shadow]")
{
	const auto splits = calculateSplitDistances(4, 0.1f, 100.0f, 0.5f);
	REQUIRE(splits.size() == 5);  // cascadeCount + 1
}

TEST_CASE("calculateSplitDistances - first is near, last is far", "[graphics3d][shadow]")
{
	const auto splits = calculateSplitDistances(4, 0.1f, 100.0f, 0.5f);
	REQUIRE(splits.front() == Catch::Approx(0.1f));
	REQUIRE(splits.back() == Catch::Approx(100.0f));
}

TEST_CASE("calculateSplitDistances - monotonically increasing", "[graphics3d][shadow]")
{
	const auto splits = calculateSplitDistances(4, 0.1f, 100.0f, 0.5f);
	for (std::size_t i = 1; i < splits.size(); ++i)
	{
		REQUIRE(splits[i] > splits[i - 1]);
	}
}

TEST_CASE("calculateSplitDistances - linear (lambda=0)", "[graphics3d][shadow]")
{
	const auto splits = calculateSplitDistances(4, 1.0f, 101.0f, 0.0f);
	// 線形分割: near=1, far=101, range=100 → 1, 26, 51, 76, 101
	REQUIRE(splits[0] == Catch::Approx(1.0f));
	REQUIRE(splits[1] == Catch::Approx(26.0f));
	REQUIRE(splits[2] == Catch::Approx(51.0f));
	REQUIRE(splits[3] == Catch::Approx(76.0f));
	REQUIRE(splits[4] == Catch::Approx(101.0f));
}

TEST_CASE("calculateSplitDistances - single cascade", "[graphics3d][shadow]")
{
	const auto splits = calculateSplitDistances(1, 0.1f, 100.0f, 0.5f);
	REQUIRE(splits.size() == 2);
	REQUIRE(splits[0] == Catch::Approx(0.1f));
	REQUIRE(splits[1] == Catch::Approx(100.0f));
}

// ── calculateCascades ───────────────────────────────────────

TEST_CASE("calculateCascades - correct cascade count", "[graphics3d][shadow]")
{
	Camera3D camera;
	camera.position = {0, 5, 10};
	camera.target = {0, 0, 0};
	camera.nearClip = 0.1f;

	ShadowConfig config;
	config.cascadeCount = 4;
	config.shadowDistance = 100.0f;

	const Vec3f lightDir = Vec3f{-1, -1, -1}.normalized();
	const auto cascades = calculateCascades(camera, lightDir, config);

	REQUIRE(cascades.size() == 4);
}

TEST_CASE("calculateCascades - cascades cover full range", "[graphics3d][shadow]")
{
	Camera3D camera;
	camera.position = {0, 0, 0};
	camera.target = {0, 0, -1};
	camera.nearClip = 0.1f;

	ShadowConfig config;
	config.cascadeCount = 3;
	config.shadowDistance = 50.0f;

	const auto cascades = calculateCascades(
		camera, Vec3f{0, -1, 0}, config);

	REQUIRE(cascades[0].nearPlane == Catch::Approx(camera.nearClip));
	REQUIRE(cascades.back().farPlane == Catch::Approx(50.0f));

	// 各カスケードが連続していることを確認
	for (std::size_t i = 1; i < cascades.size(); ++i)
	{
		REQUIRE(cascades[i].nearPlane == Catch::Approx(cascades[i - 1].farPlane));
	}
}

TEST_CASE("calculateCascades - each has valid lightViewProj", "[graphics3d][shadow]")
{
	Camera3D camera;
	camera.position = {0, 5, 10};
	camera.target = {0, 0, 0};

	ShadowConfig config;
	config.cascadeCount = 2;
	config.shadowDistance = 50.0f;

	const auto cascades = calculateCascades(
		camera, Vec3f{0, -1, 0}, config);

	for (const auto& c : cascades)
	{
		REQUIRE_FALSE(c.lightViewProj == Mat4f{});
	}
}

// ── generatePCFKernel ───────────────────────────────────────

TEST_CASE("generatePCFKernel - 1x1 kernel", "[graphics3d][shadow]")
{
	const auto kernel = generatePCFKernel(1);
	REQUIRE(kernel.size() == 1);
	REQUIRE(kernel[0].offsetX == Catch::Approx(0.0f));
	REQUIRE(kernel[0].offsetY == Catch::Approx(0.0f));
	REQUIRE(kernel[0].weight == Catch::Approx(1.0f));
}

TEST_CASE("generatePCFKernel - 3x3 kernel", "[graphics3d][shadow]")
{
	const auto kernel = generatePCFKernel(3);
	REQUIRE(kernel.size() == 9);

	// ウェイトの合計は1.0
	float totalWeight = 0.0f;
	for (const auto& s : kernel)
	{
		totalWeight += s.weight;
	}
	REQUIRE(totalWeight == Catch::Approx(1.0f));
}

TEST_CASE("generatePCFKernel - 5x5 kernel", "[graphics3d][shadow]")
{
	const auto kernel = generatePCFKernel(5);
	REQUIRE(kernel.size() == 25);

	// 中心サンプルのオフセットは(0,0)
	bool hasCenterSample = false;
	for (const auto& s : kernel)
	{
		if (s.offsetX == Catch::Approx(0.0f) && s.offsetY == Catch::Approx(0.0f))
		{
			hasCenterSample = true;
			break;
		}
	}
	REQUIRE(hasCenterSample);
}

TEST_CASE("generatePCFKernel - even size rounded up to odd", "[graphics3d][shadow]")
{
	const auto kernel = generatePCFKernel(4);
	// 4→5に補正、5x5=25サンプル
	REQUIRE(kernel.size() == 25);
}

TEST_CASE("generatePCFKernel - minimum size is 1", "[graphics3d][shadow]")
{
	const auto kernel = generatePCFKernel(0);
	REQUIRE(kernel.size() == 1);
}

TEST_CASE("PCFSample - default values", "[graphics3d][shadow]")
{
	PCFSample sample;
	REQUIRE(sample.offsetX == Catch::Approx(0.0f));
	REQUIRE(sample.offsetY == Catch::Approx(0.0f));
	REQUIRE(sample.weight == Catch::Approx(1.0f));
}
