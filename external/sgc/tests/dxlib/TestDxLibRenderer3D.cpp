/// @file TestDxLibRenderer3D.cpp
/// @brief DxLibRenderer3D adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/dxlib/DxLibRenderer3D.hpp"
#include "sgc/graphics3d/IMesh.hpp"

using namespace sgc;
using namespace sgc::graphics3d;
using namespace sgc::dxlib;
using namespace dxlib_stub;
using Catch::Approx;

namespace
{

/// @brief テスト前にDxLibスタブをリセットするフィクスチャ
struct DxLibFixture
{
	DxLibFixture() { dxlib_stub::reset(); }
};

/// @brief テスト用のダミーメッシュ
class DummyMesh : public IMesh
{
public:
	void setVertices(std::span<const Vertex3D>) override {}
	void setIndices(std::span<const uint32_t>) override {}
	[[nodiscard]] std::size_t vertexCount() const override { return 0; }
	[[nodiscard]] std::size_t indexCount() const override { return 0; }
	void draw() const override {}
};

} // anonymous namespace

// ── ビュー/プロジェクション行列 ────────────────────────

TEST_CASE("DxLibRenderer3D - default matrices are identity", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	REQUIRE(renderer.viewMatrix() == Mat4f::identity());
	REQUIRE(renderer.projectionMatrix() == Mat4f::identity());
}

TEST_CASE("DxLibRenderer3D - setViewMatrix stores matrix", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	const auto view = Mat4f::lookAt(
		Vec3f{0, 5, -10}, Vec3f{0, 0, 0}, Vec3f{0, 1, 0});
	renderer.setViewMatrix(view);

	REQUIRE(renderer.viewMatrix() == view);
}

TEST_CASE("DxLibRenderer3D - setProjectionMatrix stores matrix", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	const auto proj = Mat4f::perspective(1.0f, 1.33f, 0.1f, 100.0f);
	renderer.setProjectionMatrix(proj);

	REQUIRE(renderer.projectionMatrix() == proj);
}

// ── メッシュ描画 ────────────────────────────────────────

TEST_CASE("DxLibRenderer3D - drawMesh increments draw call count", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;
	DummyMesh mesh;

	renderer.beginFrame();
	renderer.drawMesh(mesh, Mat4f::identity());
	renderer.drawMesh(mesh, Mat4f::translation(Vec3f{1, 2, 3}));

	REQUIRE(renderer.drawCallCount() == 2);
}

TEST_CASE("DxLibRenderer3D - beginFrame clears draw calls", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;
	DummyMesh mesh;

	renderer.beginFrame();
	renderer.drawMesh(mesh, Mat4f::identity());
	REQUIRE(renderer.drawCallCount() == 1);

	renderer.beginFrame();
	REQUIRE(renderer.drawCallCount() == 0);
}

// ── ライティング ────────────────────────────────────────

TEST_CASE("DxLibRenderer3D - default ambient light", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	const auto& amb = renderer.ambientLight();
	REQUIRE(amb.r == Approx(0.2f));
	REQUIRE(amb.g == Approx(0.2f));
	REQUIRE(amb.b == Approx(0.2f));
}

TEST_CASE("DxLibRenderer3D - setAmbientLight records DxLib call", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	renderer.setAmbientLight(Colorf{0.5f, 0.6f, 0.7f, 1.0f});
	REQUIRE(renderer.ambientLight().r == Approx(0.5f));

	// DxLibスタブに記録されることを確認
	bool found = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::LightAmbColor)
		{
			found = true;
			break;
		}
	}
	REQUIRE(found);
}

TEST_CASE("DxLibRenderer3D - addLight and clearLights", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	LightData light;
	light.type = LightType::Directional;
	light.direction = Vec3f{0, -1, 0};

	renderer.addLight(light);
	renderer.addLight(light);
	REQUIRE(renderer.lights().size() == 2);

	renderer.clearLights();
	REQUIRE(renderer.lights().empty());
}

// ── デバッグ描画 ────────────────────────────────────────

TEST_CASE("DxLibRenderer3D - drawDebugLine records DxLib call", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	renderer.beginFrame();
	renderer.drawDebugLine(Vec3f{0, 0, 0}, Vec3f{10, 10, 10}, Colorf::red());

	REQUIRE(renderer.drawCallCount() == 1);

	// DxLibスタブにLine3D記録を確認
	bool found = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::Line3D)
		{
			found = true;
			REQUIRE(call.params[0] == Approx(0.0f));
			REQUIRE(call.params[3] == Approx(10.0f));
			break;
		}
	}
	REQUIRE(found);
}

TEST_CASE("DxLibRenderer3D - drawDebugSphere records DxLib call", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	renderer.beginFrame();
	renderer.drawDebugSphere(Vec3f{5, 5, 5}, 3.0f, Colorf::green());

	REQUIRE(renderer.drawCallCount() == 1);

	bool found = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::Sphere3D)
		{
			found = true;
			REQUIRE(call.params[0] == Approx(5.0f));
			REQUIRE(call.params[3] == Approx(3.0f));
			break;
		}
	}
	REQUIRE(found);
}

TEST_CASE("DxLibRenderer3D - drawDebugBox records DxLib call", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	renderer.beginFrame();
	renderer.drawDebugBox(Vec3f{0, 0, 0}, Vec3f{1, 2, 3}, Colorf::blue());

	REQUIRE(renderer.drawCallCount() == 1);

	// ボックスは線分で描画されるため、Line3Dが記録される
	bool foundLine = false;
	for (const auto& call : drawCalls())
	{
		if (call.type == DrawType::Line3D)
		{
			foundLine = true;
			break;
		}
	}
	REQUIRE(foundLine);
}

// ── シェーダー ──────────────────────────────────────────

TEST_CASE("DxLibRenderer3D - setShader accepts nullptr", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;

	renderer.setShader(nullptr);
	REQUIRE(true);
}

// ── 複合テスト ──────────────────────────────────────────

TEST_CASE("DxLibRenderer3D - full frame lifecycle", "[dxlib][renderer3d]")
{
	DxLibFixture fix;
	DxLibRenderer3D renderer;
	DummyMesh mesh;

	renderer.setViewMatrix(Mat4f::lookAt(
		Vec3f{0, 5, -10}, Vec3f{0, 0, 0}, Vec3f{0, 1, 0}));
	renderer.setProjectionMatrix(Mat4f::perspective(1.0f, 1.33f, 0.1f, 100.0f));
	renderer.setAmbientLight(Colorf{0.3f, 0.3f, 0.3f, 1.0f});

	LightData sun;
	sun.type = LightType::Directional;
	sun.direction = Vec3f{0, -1, 0.5f};
	renderer.addLight(sun);

	renderer.beginFrame();
	renderer.drawMesh(mesh, Mat4f::identity());
	renderer.drawDebugLine(Vec3f{0, 0, 0}, Vec3f{0, 10, 0});
	renderer.endFrame();

	REQUIRE(renderer.drawCallCount() == 2);
	REQUIRE(renderer.lights().size() == 1);
}
