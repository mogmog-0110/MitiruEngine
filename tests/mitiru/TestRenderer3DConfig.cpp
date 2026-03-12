#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Renderer3D.hpp>
#include <mitiru/render/RenderState3D.hpp>
#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Mesh.hpp>

using Catch::Approx;

// ============================================================
// CbTransform struct layout
// ============================================================

TEST_CASE("CbTransform - size is 192 bytes (3x float4x4)", "[renderer3d_config]")
{
	REQUIRE(sizeof(mitiru::render::CbTransform) == 192);
}

TEST_CASE("CbTransform - 16-byte aligned", "[renderer3d_config]")
{
	REQUIRE(alignof(mitiru::render::CbTransform) == 16);
}

// ============================================================
// CbLighting struct layout
// ============================================================

TEST_CASE("CbLighting - 16-byte aligned", "[renderer3d_config]")
{
	REQUIRE(alignof(mitiru::render::CbLighting) == 16);
}

TEST_CASE("CbLighting - default shininess is 32", "[renderer3d_config]")
{
	const mitiru::render::CbLighting cb;
	REQUIRE(cb.materialShininess == Approx(32.0f));
}

// ============================================================
// Renderer3DConfig defaults
// ============================================================

TEST_CASE("Renderer3DConfig - default viewport size", "[renderer3d_config]")
{
	const mitiru::render::Renderer3DConfig cfg;

	REQUIRE(cfg.viewportWidth == Approx(1280.0f));
	REQUIRE(cfg.viewportHeight == Approx(720.0f));
	REQUIRE(cfg.enableDepthBuffer == true);
}

TEST_CASE("Renderer3DConfig - default ambient color", "[renderer3d_config]")
{
	const mitiru::render::Renderer3DConfig cfg;

	REQUIRE(cfg.defaultAmbient.r == Approx(0.15f));
	REQUIRE(cfg.defaultAmbient.g == Approx(0.15f));
	REQUIRE(cfg.defaultAmbient.b == Approx(0.15f));
}

TEST_CASE("Renderer3DConfig - custom values", "[renderer3d_config]")
{
	mitiru::render::Renderer3DConfig cfg;
	cfg.viewportWidth = 1920.0f;
	cfg.viewportHeight = 1080.0f;
	cfg.enableDepthBuffer = false;
	cfg.defaultAmbient = {0.3f, 0.3f, 0.3f, 1.0f};

	REQUIRE(cfg.viewportWidth == Approx(1920.0f));
	REQUIRE(cfg.viewportHeight == Approx(1080.0f));
	REQUIRE(cfg.enableDepthBuffer == false);
	REQUIRE(cfg.defaultAmbient.r == Approx(0.3f));
}

// ============================================================
// Renderer3D - pre-initialization state
// ============================================================

TEST_CASE("Renderer3D - default construction is not initialized", "[renderer3d_config]")
{
	const mitiru::render::Renderer3D renderer;

	REQUIRE(renderer.isInitialized() == false);
	REQUIRE(renderer.drawCallCount() == 0);
}

TEST_CASE("Renderer3D - default render state is opaque", "[renderer3d_config]")
{
	const mitiru::render::Renderer3D renderer;
	const auto state = renderer.renderState();

	REQUIRE(state == mitiru::render::RenderState3D::opaque());
}

// ============================================================
// Supporting structs usable without DX11
// ============================================================

TEST_CASE("Camera3D and Light work together without GPU", "[renderer3d_config]")
{
	mitiru::render::Camera3D camera;
	camera.setPosition({0, 10, -20});
	camera.setTarget({0, 0, 0});
	camera.setAspectRatio(16.0f / 9.0f);

	const auto light = mitiru::render::Light::directional({0, -1, 0.5f});
	const auto material = mitiru::render::Material::defaultMaterial();

	/// これらの値は全てCPU側で使えることを確認する
	REQUIRE(camera.viewMatrix().m[3][3] == Approx(1.0f));
	REQUIRE(light.direction.y == Approx(-1.0f));
	REQUIRE(material.shininess == Approx(32.0f));
}

TEST_CASE("Mesh and Material are ready for renderer submission", "[renderer3d_config]")
{
	auto cube = mitiru::render::Mesh::createCube(2.0f);
	const auto mat = mitiru::render::Material::defaultMaterial();

	REQUIRE(cube.vertexCount() == 24);
	REQUIRE(cube.indexCount() == 36);
	REQUIRE(mat.diffuse.r == Approx(0.8f));
}
