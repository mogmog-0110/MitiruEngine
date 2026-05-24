/// @file TestSiv3DRenderer3D.cpp
/// @brief Siv3DRenderer3D adapter tests with stub

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/siv3d/Siv3DRenderer3D.hpp"
#include "sgc/graphics3d/IMesh.hpp"

using namespace sgc;
using namespace sgc::graphics3d;
using namespace sgc::siv3d;
using Catch::Approx;

namespace
{

/// @brief テスト前にスタブをリセットするフィクスチャ
struct Siv3DFixture
{
	Siv3DFixture() { siv3d_stub::reset(); }
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

TEST_CASE("Siv3DRenderer3D - default matrices are identity", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	REQUIRE(renderer.viewMatrix() == Mat4f::identity());
	REQUIRE(renderer.projectionMatrix() == Mat4f::identity());
}

TEST_CASE("Siv3DRenderer3D - setViewMatrix stores matrix", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	const auto view = Mat4f::lookAt(
		Vec3f{0, 5, -10}, Vec3f{0, 0, 0}, Vec3f{0, 1, 0});
	renderer.setViewMatrix(view);

	REQUIRE(renderer.viewMatrix() == view);
}

TEST_CASE("Siv3DRenderer3D - setProjectionMatrix stores matrix", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	const auto proj = Mat4f::perspective(1.0f, 1.33f, 0.1f, 100.0f);
	renderer.setProjectionMatrix(proj);

	REQUIRE(renderer.projectionMatrix() == proj);
}

// ── メッシュ描画 ────────────────────────────────────────

TEST_CASE("Siv3DRenderer3D - drawMesh increments draw call count", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;
	DummyMesh mesh;

	renderer.beginFrame();
	renderer.drawMesh(mesh, Mat4f::identity());
	renderer.drawMesh(mesh, Mat4f::translation(Vec3f{1, 2, 3}));

	REQUIRE(renderer.drawCallCount() == 2);
}

TEST_CASE("Siv3DRenderer3D - beginFrame clears draw calls", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;
	DummyMesh mesh;

	renderer.beginFrame();
	renderer.drawMesh(mesh, Mat4f::identity());
	REQUIRE(renderer.drawCallCount() == 1);

	renderer.beginFrame();
	REQUIRE(renderer.drawCallCount() == 0);
}

// ── ライティング ────────────────────────────────────────

TEST_CASE("Siv3DRenderer3D - default ambient light", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	const auto& amb = renderer.ambientLight();
	REQUIRE(amb.r == Approx(0.2f));
	REQUIRE(amb.g == Approx(0.2f));
	REQUIRE(amb.b == Approx(0.2f));
}

TEST_CASE("Siv3DRenderer3D - setAmbientLight changes color", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	renderer.setAmbientLight(Colorf{0.5f, 0.6f, 0.7f, 1.0f});
	REQUIRE(renderer.ambientLight().r == Approx(0.5f));
	REQUIRE(renderer.ambientLight().g == Approx(0.6f));
	REQUIRE(renderer.ambientLight().b == Approx(0.7f));
}

TEST_CASE("Siv3DRenderer3D - addLight and clearLights", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	LightData light;
	light.type = LightType::Point;
	light.position = Vec3f{1, 2, 3};
	light.intensity = 2.0f;

	renderer.addLight(light);
	renderer.addLight(light);
	REQUIRE(renderer.lights().size() == 2);

	renderer.clearLights();
	REQUIRE(renderer.lights().empty());
}

// ── シェーダー ──────────────────────────────────────────

TEST_CASE("Siv3DRenderer3D - setShader accepts nullptr", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	// シェーダーをnullptrに設定しても安全に動作する
	renderer.setShader(nullptr);
	// クラッシュしなければOK
	REQUIRE(true);
}

// ── デバッグ描画 ────────────────────────────────────────

TEST_CASE("Siv3DRenderer3D - drawDebugLine adds draw call", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	renderer.beginFrame();
	renderer.drawDebugLine(Vec3f{0, 0, 0}, Vec3f{1, 1, 1}, Colorf::red());

	REQUIRE(renderer.drawCallCount() == 1);
}

TEST_CASE("Siv3DRenderer3D - drawDebugSphere adds draw call", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	renderer.beginFrame();
	renderer.drawDebugSphere(Vec3f{0, 0, 0}, 5.0f, Colorf::green());

	REQUIRE(renderer.drawCallCount() == 1);
}

TEST_CASE("Siv3DRenderer3D - drawDebugBox adds draw call", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	renderer.beginFrame();
	renderer.drawDebugBox(Vec3f{0, 0, 0}, Vec3f{1, 2, 3}, Colorf::blue());

	REQUIRE(renderer.drawCallCount() == 1);
}

TEST_CASE("Siv3DRenderer3D - multiple debug draws accumulate", "[siv3d][renderer3d]")
{
	Siv3DFixture fix;
	Siv3DRenderer3D renderer;

	renderer.beginFrame();
	renderer.drawDebugLine(Vec3f{0, 0, 0}, Vec3f{1, 0, 0});
	renderer.drawDebugSphere(Vec3f{0, 0, 0}, 1.0f);
	renderer.drawDebugBox(Vec3f{0, 0, 0}, Vec3f{1, 1, 1});

	REQUIRE(renderer.drawCallCount() == 3);
}
