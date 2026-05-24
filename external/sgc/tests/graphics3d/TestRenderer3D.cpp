#include <catch2/catch_test_macros.hpp>

#include "sgc/graphics3d/IRenderer3D.hpp"
#include "sgc/graphics3d/IMesh.hpp"
#include "sgc/graphics3d/IMaterial.hpp"
#include "sgc/graphics3d/IShader.hpp"

using namespace sgc;
using namespace sgc::graphics3d;

/// @brief テスト用のモックレンダラー
class MockRenderer3D : public IRenderer3D
{
public:
	int drawMeshCount = 0;
	int addLightCount = 0;
	int debugLineCount = 0;
	int debugSphereCount = 0;
	int debugBoxCount = 0;
	int beginFrameCount = 0;
	int endFrameCount = 0;
	bool lightsCleared = false;
	Colorf lastAmbient;
	Mat4f lastView;
	Mat4f lastProjection;

	void setViewMatrix(const Mat4f& view) override { lastView = view; }
	void setProjectionMatrix(const Mat4f& proj) override { lastProjection = proj; }
	void drawMesh(const IMesh&, const Mat4f&, const IMaterial*) override { ++drawMeshCount; }
	void setAmbientLight(const Colorf& c) override { lastAmbient = c; }
	void addLight(const LightData&) override { ++addLightCount; }
	void clearLights() override { lightsCleared = true; addLightCount = 0; }
	void setShader(const IShader*) override {}
	void drawDebugLine(const Vec3f&, const Vec3f&, const Colorf&) override { ++debugLineCount; }
	void drawDebugSphere(const Vec3f&, float, const Colorf&) override { ++debugSphereCount; }
	void drawDebugBox(const Vec3f&, const Vec3f&, const Colorf&) override { ++debugBoxCount; }
	void beginFrame() override { ++beginFrameCount; }
	void endFrame() override { ++endFrameCount; }
};

/// @brief テスト用のモックメッシュ
class MockMesh : public IMesh
{
public:
	void setVertices(std::span<const Vertex3D>) override {}
	void setIndices(std::span<const uint32_t>) override {}
	[[nodiscard]] std::size_t vertexCount() const override { return 0; }
	[[nodiscard]] std::size_t indexCount() const override { return 0; }
	void draw() const override {}
};

TEST_CASE("IRenderer3D - drawMesh via interface", "[graphics3d][renderer3d]")
{
	MockRenderer3D renderer;
	MockMesh mesh;

	renderer.drawMesh(mesh, Mat4f::identity(), nullptr);
	renderer.drawMesh(mesh, Mat4f::identity(), nullptr);

	REQUIRE(renderer.drawMeshCount == 2);
}

TEST_CASE("IRenderer3D - lighting management", "[graphics3d][renderer3d]")
{
	MockRenderer3D renderer;

	renderer.setAmbientLight(Colorf{0.2f, 0.2f, 0.2f, 1.0f});

	LightData sun;
	sun.type = LightType::Directional;
	sun.direction = Vec3f{0, -1, 0};
	sun.intensity = 1.0f;
	renderer.addLight(sun);

	LightData point;
	point.type = LightType::Point;
	point.position = Vec3f{10, 5, 0};
	point.range = 50.0f;
	renderer.addLight(point);

	REQUIRE(renderer.addLightCount == 2);

	renderer.clearLights();
	REQUIRE(renderer.lightsCleared);
	REQUIRE(renderer.addLightCount == 0);
}

TEST_CASE("IRenderer3D - debug drawing", "[graphics3d][renderer3d]")
{
	MockRenderer3D renderer;

	renderer.drawDebugLine(Vec3f{0,0,0}, Vec3f{1,1,1}, Colorf{1,1,1,1});
	renderer.drawDebugSphere(Vec3f{0,0,0}, 5.0f, Colorf{1,0,0,1});
	renderer.drawDebugBox(Vec3f{0,0,0}, Vec3f{1,1,1}, Colorf{0,1,0,1});

	REQUIRE(renderer.debugLineCount == 1);
	REQUIRE(renderer.debugSphereCount == 1);
	REQUIRE(renderer.debugBoxCount == 1);
}

TEST_CASE("IRenderer3D - frame management", "[graphics3d][renderer3d]")
{
	MockRenderer3D renderer;

	renderer.beginFrame();
	renderer.endFrame();
	renderer.beginFrame();
	renderer.endFrame();

	REQUIRE(renderer.beginFrameCount == 2);
	REQUIRE(renderer.endFrameCount == 2);
}

TEST_CASE("IRenderer3D - view and projection matrices", "[graphics3d][renderer3d]")
{
	MockRenderer3D renderer;

	auto view = Mat4f::lookAt(Vec3f{0,10,10}, Vec3f{0,0,0}, Vec3f{0,1,0});
	auto proj = Mat4f::perspective(1.0f, 16.0f/9.0f, 0.1f, 1000.0f);

	renderer.setViewMatrix(view);
	renderer.setProjectionMatrix(proj);

	// Verify matrices were stored
	REQUIRE(renderer.lastView == view);
	REQUIRE(renderer.lastProjection == proj);
}

TEST_CASE("LightData - default values", "[graphics3d][renderer3d]")
{
	LightData light;
	REQUIRE(light.type == LightType::Directional);
	REQUIRE(light.intensity == 1.0f);
	REQUIRE(light.range == 100.0f);
	REQUIRE(light.spotAngle == 30.0f);
}
