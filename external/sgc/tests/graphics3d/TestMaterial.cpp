#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/graphics3d/IMaterial.hpp"

using namespace sgc;
using namespace sgc::graphics3d;

TEST_CASE("MaterialData - default values", "[graphics3d][material]")
{
	MaterialData mat;
	REQUIRE_THAT(mat.albedo.r, Catch::Matchers::WithinAbs(1.0f, 0.001f));
	REQUIRE_THAT(mat.metallic, Catch::Matchers::WithinAbs(0.0f, 0.001f));
	REQUIRE_THAT(mat.roughness, Catch::Matchers::WithinAbs(0.5f, 0.001f));
	REQUIRE_THAT(mat.opacity, Catch::Matchers::WithinAbs(1.0f, 0.001f));
	REQUIRE(mat.renderMode == RenderMode::Opaque);
}

TEST_CASE("MaterialData - equality comparison", "[graphics3d][material]")
{
	MaterialData a;
	MaterialData b;
	REQUIRE(a == b);

	b.metallic = 1.0f;
	REQUIRE_FALSE(a == b);
}

TEST_CASE("BasicMaterial - default construction", "[graphics3d][material]")
{
	BasicMaterial mat;
	REQUIRE(mat.name() == "default");
	auto data = mat.getData();
	REQUIRE_THAT(data.roughness, Catch::Matchers::WithinAbs(0.5f, 0.001f));
}

TEST_CASE("BasicMaterial - named construction", "[graphics3d][material]")
{
	MaterialData data;
	data.albedo = Colorf{0.8f, 0.2f, 0.1f, 1.0f};
	data.metallic = 0.9f;
	data.roughness = 0.1f;

	BasicMaterial mat{"metalRed", data};
	REQUIRE(mat.name() == "metalRed");

	auto got = mat.getData();
	REQUIRE_THAT(got.albedo.r, Catch::Matchers::WithinAbs(0.8f, 0.001f));
	REQUIRE_THAT(got.metallic, Catch::Matchers::WithinAbs(0.9f, 0.001f));
}

TEST_CASE("BasicMaterial - setData updates values", "[graphics3d][material]")
{
	BasicMaterial mat;

	MaterialData newData;
	newData.roughness = 0.9f;
	newData.renderMode = RenderMode::Transparent;
	mat.setData(newData);

	auto got = mat.getData();
	REQUIRE_THAT(got.roughness, Catch::Matchers::WithinAbs(0.9f, 0.001f));
	REQUIRE(got.renderMode == RenderMode::Transparent);
}

TEST_CASE("BasicMaterial - IMaterial interface", "[graphics3d][material]")
{
	BasicMaterial concrete{"test"};
	IMaterial& iface = concrete;

	REQUIRE(iface.name() == "test");
	iface.apply();  // Should not crash

	MaterialData data;
	data.metallic = 0.5f;
	iface.setData(data);
	REQUIRE_THAT(iface.getData().metallic, Catch::Matchers::WithinAbs(0.5f, 0.001f));
}

TEST_CASE("RenderMode - all values accessible", "[graphics3d][material]")
{
	REQUIRE(static_cast<int>(RenderMode::Opaque) == 0);
	REQUIRE(static_cast<int>(RenderMode::Transparent) == 1);
	REQUIRE(static_cast<int>(RenderMode::Additive) == 2);
	REQUIRE(static_cast<int>(RenderMode::Cutout) == 3);
}
