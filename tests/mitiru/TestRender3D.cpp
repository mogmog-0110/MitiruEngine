#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <string>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex3D.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Material.hpp>
#include <mitiru/render/Light.hpp>
#include <mitiru/render/Scene3D.hpp>

// ============================================================
// Vertex3D
// ============================================================

TEST_CASE("Vertex3D - default constructor", "[render3d]")
{
	constexpr mitiru::render::Vertex3D v;

	REQUIRE(v.position.x == 0.0f);
	REQUIRE(v.position.y == 0.0f);
	REQUIRE(v.position.z == 0.0f);
	REQUIRE(v.normal.x == 0.0f);
	REQUIRE(v.normal.y == 0.0f);
	REQUIRE(v.normal.z == 0.0f);
	REQUIRE(v.texCoord.x == 0.0f);
	REQUIRE(v.texCoord.y == 0.0f);
	REQUIRE(v.color.r == 1.0f);
	REQUIRE(v.color.g == 1.0f);
	REQUIRE(v.color.b == 1.0f);
	REQUIRE(v.color.a == 1.0f);
}

TEST_CASE("Vertex3D - full constructor", "[render3d]")
{
	constexpr mitiru::render::Vertex3D v(
		sgc::Vec3f{1.0f, 2.0f, 3.0f},
		sgc::Vec3f{0.0f, 1.0f, 0.0f},
		sgc::Vec2f{0.5f, 0.5f},
		sgc::Colorf::red());

	REQUIRE(v.position.x == 1.0f);
	REQUIRE(v.position.y == 2.0f);
	REQUIRE(v.position.z == 3.0f);
	REQUIRE(v.normal.y == 1.0f);
	REQUIRE(v.texCoord.x == 0.5f);
	REQUIRE(v.color.r == 1.0f);
	REQUIRE(v.color.g == 0.0f);
}

TEST_CASE("Vertex3D - position and normal constructor", "[render3d]")
{
	constexpr mitiru::render::Vertex3D v(
		sgc::Vec3f{10.0f, 20.0f, 30.0f},
		sgc::Vec3f{0.0f, 0.0f, 1.0f});

	REQUIRE(v.position.x == 10.0f);
	REQUIRE(v.normal.z == 1.0f);
	REQUIRE(v.texCoord.x == 0.0f);
	REQUIRE(v.color.r == 1.0f);
}

// ============================================================
// Mesh - createCube
// ============================================================

TEST_CASE("Mesh - createCube has 24 vertices and 36 indices", "[render3d]")
{
	const auto cube = mitiru::render::Mesh::createCube(1.0f);

	REQUIRE(cube.vertexCount() == 24);
	REQUIRE(cube.indexCount() == 36);
}

TEST_CASE("Mesh - createCube with custom size", "[render3d]")
{
	const auto cube = mitiru::render::Mesh::createCube(2.0f);

	REQUIRE(cube.vertexCount() == 24);
	REQUIRE(cube.indexCount() == 36);

	/// 頂点が±1.0の範囲内にあることを確認
	for (const auto& v : cube.vertices())
	{
		REQUIRE(std::abs(v.position.x) <= Catch::Approx(1.0f));
		REQUIRE(std::abs(v.position.y) <= Catch::Approx(1.0f));
		REQUIRE(std::abs(v.position.z) <= Catch::Approx(1.0f));
	}
}

TEST_CASE("Mesh - createCube normals are unit length", "[render3d]")
{
	const auto cube = mitiru::render::Mesh::createCube(1.0f);

	for (const auto& v : cube.vertices())
	{
		const float len = std::sqrt(
			v.normal.x * v.normal.x +
			v.normal.y * v.normal.y +
			v.normal.z * v.normal.z);
		REQUIRE(len == Catch::Approx(1.0f).margin(0.001f));
	}
}

// ============================================================
// Mesh - createSphere
// ============================================================

TEST_CASE("Mesh - createSphere vertex count", "[render3d]")
{
	const int segments = 16;
	const auto sphere = mitiru::render::Mesh::createSphere(0.5f, segments);

	/// (stacks+1) * (slices+1) = 17 * 17 = 289
	const auto expectedVerts = static_cast<std::size_t>((segments + 1) * (segments + 1));
	REQUIRE(sphere.vertexCount() == expectedVerts);
}

TEST_CASE("Mesh - createSphere index count", "[render3d]")
{
	const int segments = 16;
	const auto sphere = mitiru::render::Mesh::createSphere(0.5f, segments);

	/// stacks * slices * 6 = 16 * 16 * 6 = 1536
	const auto expectedIdx = static_cast<std::size_t>(segments * segments * 6);
	REQUIRE(sphere.indexCount() == expectedIdx);
}

TEST_CASE("Mesh - createSphere vertices are on sphere surface", "[render3d]")
{
	const float radius = 2.0f;
	const auto sphere = mitiru::render::Mesh::createSphere(radius, 8);

	for (const auto& v : sphere.vertices())
	{
		const float dist = std::sqrt(
			v.position.x * v.position.x +
			v.position.y * v.position.y +
			v.position.z * v.position.z);
		REQUIRE(dist == Catch::Approx(radius).margin(0.001f));
	}
}

// ============================================================
// Mesh - createPlane
// ============================================================

TEST_CASE("Mesh - createPlane has 4 vertices and 6 indices", "[render3d]")
{
	const auto plane = mitiru::render::Mesh::createPlane(1.0f, 1.0f);

	REQUIRE(plane.vertexCount() == 4);
	REQUIRE(plane.indexCount() == 6);
}

TEST_CASE("Mesh - createPlane all normals point up", "[render3d]")
{
	const auto plane = mitiru::render::Mesh::createPlane(5.0f, 5.0f);

	for (const auto& v : plane.vertices())
	{
		REQUIRE(v.normal.x == Catch::Approx(0.0f));
		REQUIRE(v.normal.y == Catch::Approx(1.0f));
		REQUIRE(v.normal.z == Catch::Approx(0.0f));
	}
}

TEST_CASE("Mesh - createPlane Y coordinate is zero", "[render3d]")
{
	const auto plane = mitiru::render::Mesh::createPlane(10.0f, 10.0f);

	for (const auto& v : plane.vertices())
	{
		REQUIRE(v.position.y == Catch::Approx(0.0f));
	}
}

// ============================================================
// Mesh - setVertices / setIndices
// ============================================================

TEST_CASE("Mesh - empty mesh has zero counts", "[render3d]")
{
	const mitiru::render::Mesh mesh;

	REQUIRE(mesh.vertexCount() == 0);
	REQUIRE(mesh.indexCount() == 0);
}

TEST_CASE("Mesh - setVertices and setIndices populate data", "[render3d]")
{
	mitiru::render::Mesh mesh;

	std::vector<mitiru::render::Vertex3D> verts(10);
	std::vector<uint32_t> idx = {0, 1, 2, 3, 4, 5};

	mesh.setVertices(std::move(verts));
	mesh.setIndices(std::move(idx));

	REQUIRE(mesh.vertexCount() == 10);
	REQUIRE(mesh.indexCount() == 6);
}

// ============================================================
// Material
// ============================================================

TEST_CASE("Material - default values", "[render3d]")
{
	const mitiru::render::Material mat;

	REQUIRE(mat.ambient.r == Catch::Approx(0.1f));
	REQUIRE(mat.ambient.g == Catch::Approx(0.1f));
	REQUIRE(mat.ambient.b == Catch::Approx(0.1f));
	REQUIRE(mat.diffuse.r == Catch::Approx(0.8f));
	REQUIRE(mat.specular.r == Catch::Approx(1.0f));
	REQUIRE(mat.shininess == Catch::Approx(32.0f));
}

TEST_CASE("Material - defaultMaterial factory", "[render3d]")
{
	const auto mat = mitiru::render::Material::defaultMaterial();

	REQUIRE(mat.ambient.r == Catch::Approx(0.1f));
	REQUIRE(mat.diffuse.r == Catch::Approx(0.8f));
	REQUIRE(mat.shininess == Catch::Approx(32.0f));
}

TEST_CASE("Material - toJson contains expected fields", "[render3d]")
{
	const auto mat = mitiru::render::Material::defaultMaterial();
	const auto json = mat.toJson();

	REQUIRE(json.find("ambient") != std::string::npos);
	REQUIRE(json.find("diffuse") != std::string::npos);
	REQUIRE(json.find("specular") != std::string::npos);
	REQUIRE(json.find("shininess") != std::string::npos);
}

TEST_CASE("Material - custom values", "[render3d]")
{
	mitiru::render::Material mat;
	mat.diffuse = sgc::Colorf::red();
	mat.shininess = 64.0f;

	REQUIRE(mat.diffuse.r == Catch::Approx(1.0f));
	REQUIRE(mat.diffuse.g == Catch::Approx(0.0f));
	REQUIRE(mat.shininess == Catch::Approx(64.0f));
}

// ============================================================
// Light
// ============================================================

TEST_CASE("Light - default is directional", "[render3d]")
{
	const mitiru::render::Light light;

	REQUIRE(light.type == mitiru::render::LightType::Directional);
	REQUIRE(light.direction.y == Catch::Approx(-1.0f));
	REQUIRE(light.intensity == Catch::Approx(1.0f));
}

TEST_CASE("Light - directional factory", "[render3d]")
{
	const auto light = mitiru::render::Light::directional(
		sgc::Vec3f{1.0f, -1.0f, 0.0f},
		sgc::Colorf{1.0f, 0.9f, 0.8f, 1.0f});

	REQUIRE(light.type == mitiru::render::LightType::Directional);
	REQUIRE(light.direction.x == Catch::Approx(1.0f));
	REQUIRE(light.direction.y == Catch::Approx(-1.0f));
	REQUIRE(light.color.r == Catch::Approx(1.0f));
	REQUIRE(light.color.g == Catch::Approx(0.9f));
}

TEST_CASE("Light - directional factory with default color", "[render3d]")
{
	const auto light = mitiru::render::Light::directional(sgc::Vec3f{0, -1, 0});

	REQUIRE(light.color.r == Catch::Approx(1.0f));
	REQUIRE(light.color.g == Catch::Approx(1.0f));
	REQUIRE(light.color.b == Catch::Approx(1.0f));
}

TEST_CASE("Light - point factory", "[render3d]")
{
	const auto light = mitiru::render::Light::point(
		sgc::Vec3f{5.0f, 3.0f, 0.0f}, 50.0f);

	REQUIRE(light.type == mitiru::render::LightType::Point);
	REQUIRE(light.position.x == Catch::Approx(5.0f));
	REQUIRE(light.position.y == Catch::Approx(3.0f));
	REQUIRE(light.range == Catch::Approx(50.0f));
}

TEST_CASE("Light - point factory with default range", "[render3d]")
{
	const auto light = mitiru::render::Light::point(sgc::Vec3f{0, 5, 0});

	REQUIRE(light.range == Catch::Approx(100.0f));
}

TEST_CASE("Light - spot factory", "[render3d]")
{
	const auto light = mitiru::render::Light::spot(
		sgc::Vec3f{0, 10, 0},
		sgc::Vec3f{0, -1, 0},
		30.0f,
		75.0f);

	REQUIRE(light.type == mitiru::render::LightType::Spot);
	REQUIRE(light.position.y == Catch::Approx(10.0f));
	REQUIRE(light.direction.y == Catch::Approx(-1.0f));
	REQUIRE(light.spotAngle == Catch::Approx(30.0f));
	REQUIRE(light.range == Catch::Approx(75.0f));
}

// ============================================================
// Scene3D
// ============================================================

TEST_CASE("Scene3D - initial state is empty", "[render3d]")
{
	const mitiru::render::Scene3D scene;

	REQUIRE(scene.objectCount() == 0);
	REQUIRE(scene.lightCount() == 0);
	REQUIRE(scene.objects().empty());
	REQUIRE(scene.lights().empty());
}

TEST_CASE("Scene3D - addObject increases count", "[render3d]")
{
	mitiru::render::Scene3D scene;
	const auto cube = mitiru::render::Mesh::createCube(1.0f);

	scene.addObject({&cube, {}, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}});
	scene.addObject({&cube, {}, {5, 0, 0}, {0, 0, 0}, {2, 2, 2}});

	REQUIRE(scene.objectCount() == 2);
}

TEST_CASE("Scene3D - addLight increases count", "[render3d]")
{
	mitiru::render::Scene3D scene;

	scene.addLight(mitiru::render::Light::directional({0, -1, 0}));
	scene.addLight(mitiru::render::Light::point({0, 5, 0}));

	REQUIRE(scene.lightCount() == 2);
}

TEST_CASE("Scene3D - clear removes all", "[render3d]")
{
	mitiru::render::Scene3D scene;
	const auto cube = mitiru::render::Mesh::createCube(1.0f);

	scene.addObject({&cube, {}, {}, {}, {1, 1, 1}});
	scene.addLight(mitiru::render::Light::directional({0, -1, 0}));

	REQUIRE(scene.objectCount() == 1);
	REQUIRE(scene.lightCount() == 1);

	scene.clear();

	REQUIRE(scene.objectCount() == 0);
	REQUIRE(scene.lightCount() == 0);
}

TEST_CASE("Scene3D - object data is preserved", "[render3d]")
{
	mitiru::render::Scene3D scene;
	const auto cube = mitiru::render::Mesh::createCube(1.0f);

	mitiru::render::Material mat;
	mat.diffuse = sgc::Colorf::red();

	scene.addObject({&cube, mat, {10, 20, 30}, {0.1f, 0.2f, 0.3f}, {2, 3, 4}});

	const auto& obj = scene.objects()[0];
	REQUIRE(obj.mesh == &cube);
	REQUIRE(obj.material.diffuse.r == Catch::Approx(1.0f));
	REQUIRE(obj.position.x == Catch::Approx(10.0f));
	REQUIRE(obj.rotation.y == Catch::Approx(0.2f));
	REQUIRE(obj.scale.z == Catch::Approx(4.0f));
}

TEST_CASE("Scene3D - light data is preserved", "[render3d]")
{
	mitiru::render::Scene3D scene;

	scene.addLight(mitiru::render::Light::point(
		sgc::Vec3f{1, 2, 3}, 42.0f));

	const auto& light = scene.lights()[0];
	REQUIRE(light.type == mitiru::render::LightType::Point);
	REQUIRE(light.position.x == Catch::Approx(1.0f));
	REQUIRE(light.range == Catch::Approx(42.0f));
}
