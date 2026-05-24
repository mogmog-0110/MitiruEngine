#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sgc/graphics3d/MeshFactory.hpp"

using namespace sgc::graphics3d;
using Catch::Matchers::WithinAbs;

TEST_CASE("createCubeMesh produces correct counts", "[graphics3d][mesh]")
{
	const auto mesh = createCubeMesh(1.0f);
	// 6 faces * 4 vertices = 24
	CHECK(mesh.vertices.size() == 24);
	// 6 faces * 6 indices = 36
	CHECK(mesh.indices.size() == 36);
}

TEST_CASE("createSphereMesh produces correct counts", "[graphics3d][mesh]")
{
	const int segments = 8;
	const int rings = 8;
	const auto mesh = createSphereMesh(0.5f, segments, rings);

	const auto expectedVerts = static_cast<std::size_t>((rings + 1) * (segments + 1));
	CHECK(mesh.vertices.size() == expectedVerts);

	const auto expectedIdx = static_cast<std::size_t>(rings * segments * 6);
	CHECK(mesh.indices.size() == expectedIdx);
}

TEST_CASE("createPlaneMesh has unit normals pointing up", "[graphics3d][mesh]")
{
	const auto mesh = createPlaneMesh(2.0f, 2.0f, 2, 2);

	// 全頂点の法線が (0, 1, 0)
	for (const auto& v : mesh.vertices)
	{
		CHECK_THAT(v.normal.y, WithinAbs(1.0, 1e-6));
		CHECK_THAT(v.normal.x, WithinAbs(0.0, 1e-6));
		CHECK_THAT(v.normal.z, WithinAbs(0.0, 1e-6));
	}
}

TEST_CASE("createCylinderMesh produces non-empty data", "[graphics3d][mesh]")
{
	const auto mesh = createCylinderMesh(0.5f, 1.0f, 8);
	CHECK(mesh.vertices.size() > 0);
	CHECK(mesh.indices.size() > 0);
	// インデックスが頂点範囲内
	for (const auto idx : mesh.indices)
	{
		CHECK(idx < mesh.vertices.size());
	}
}
