#include <catch2/catch_test_macros.hpp>

#include "mitiru/asset/MeshCache.hpp"

using namespace mitiru::asset;

// 簡単な三角形のOBJデータ
static const std::string TRIANGLE_OBJ =
	"v 0.0 0.0 0.0\n"
	"v 1.0 0.0 0.0\n"
	"v 0.0 1.0 0.0\n"
	"f 1 2 3\n";

// 四角形のOBJデータ（2三角形に分割される）
static const std::string QUAD_OBJ =
	"v 0.0 0.0 0.0\n"
	"v 1.0 0.0 0.0\n"
	"v 1.0 1.0 0.0\n"
	"v 0.0 1.0 0.0\n"
	"f 1 2 3 4\n";

TEST_CASE("MeshCache - load triangle OBJ", "[mitiru][asset]")
{
	MeshCache cache;

	REQUIRE(cache.load("triangle", TRIANGLE_OBJ));
	REQUIRE(cache.has("triangle"));

	const auto* mesh = cache.get("triangle");
	REQUIRE(mesh != nullptr);
	REQUIRE(mesh->vertices.size() == 3);
	REQUIRE(mesh->indices.size() == 3);
}

TEST_CASE("MeshCache - load quad OBJ (fan triangulation)", "[mitiru][asset]")
{
	MeshCache cache;

	REQUIRE(cache.load("quad", QUAD_OBJ));

	const auto* mesh = cache.get("quad");
	REQUIRE(mesh != nullptr);
	REQUIRE(mesh->vertices.size() == 4);
	REQUIRE(mesh->indices.size() == 6);  // 2三角形 = 6インデックス
}

TEST_CASE("MeshCache - load returns false for empty data", "[mitiru][asset]")
{
	MeshCache cache;

	REQUIRE_FALSE(cache.load("empty", ""));
	REQUIRE_FALSE(cache.has("empty"));
}

TEST_CASE("MeshCache - load returns false for no vertices", "[mitiru][asset]")
{
	MeshCache cache;

	REQUIRE_FALSE(cache.load("no_verts", "# comment only\n"));
}

TEST_CASE("MeshCache - get returns nullptr for missing", "[mitiru][asset]")
{
	MeshCache cache;
	REQUIRE(cache.get("nonexistent") == nullptr);
}

TEST_CASE("MeshCache - unload removes mesh", "[mitiru][asset]")
{
	MeshCache cache;
	cache.load("tri", TRIANGLE_OBJ);

	REQUIRE(cache.unload("tri"));
	REQUIRE_FALSE(cache.has("tri"));
	REQUIRE(cache.get("tri") == nullptr);

	// 二重アンロードはfalse
	REQUIRE_FALSE(cache.unload("tri"));
}

TEST_CASE("MeshCache - clear removes all", "[mitiru][asset]")
{
	MeshCache cache;
	cache.load("a", TRIANGLE_OBJ);
	cache.load("b", QUAD_OBJ);

	cache.clear();

	auto s = cache.stats();
	REQUIRE(s.count == 0);
	REQUIRE(s.totalVertices == 0);
	REQUIRE(s.totalIndices == 0);
}

TEST_CASE("MeshCache - stats reports correct totals", "[mitiru][asset]")
{
	MeshCache cache;
	cache.load("tri", TRIANGLE_OBJ);
	cache.load("quad", QUAD_OBJ);

	auto s = cache.stats();
	REQUIRE(s.count == 2);
	REQUIRE(s.totalVertices == 7);   // 3 + 4
	REQUIRE(s.totalIndices == 9);    // 3 + 6
}

TEST_CASE("MeshCache - vertex positions are correct", "[mitiru][asset]")
{
	MeshCache cache;
	cache.load("tri", TRIANGLE_OBJ);

	const auto* mesh = cache.get("tri");
	REQUIRE(mesh != nullptr);

	REQUIRE(mesh->vertices[0].position.x == 0.0f);
	REQUIRE(mesh->vertices[0].position.y == 0.0f);
	REQUIRE(mesh->vertices[1].position.x == 1.0f);
	REQUIRE(mesh->vertices[2].position.y == 1.0f);
}

TEST_CASE("parseSimpleObj - handles comments", "[mitiru][asset]")
{
	const std::string obj =
		"# This is a comment\n"
		"v 1.0 2.0 3.0\n"
		"v 4.0 5.0 6.0\n"
		"v 7.0 8.0 9.0\n"
		"f 1 2 3\n";

	auto result = parseSimpleObj(obj);
	REQUIRE(result.has_value());
	REQUIRE(result->vertices.size() == 3);
}

TEST_CASE("parseSimpleObj - handles v//vn format", "[mitiru][asset]")
{
	const std::string obj =
		"v 0.0 0.0 0.0\n"
		"v 1.0 0.0 0.0\n"
		"v 0.0 1.0 0.0\n"
		"vn 0.0 0.0 1.0\n"
		"f 1//1 2//1 3//1\n";

	auto result = parseSimpleObj(obj);
	REQUIRE(result.has_value());
	REQUIRE(result->vertices.size() == 3);
	REQUIRE(result->indices.size() == 3);
}
