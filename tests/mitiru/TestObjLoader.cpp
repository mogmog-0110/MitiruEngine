/// @file TestObjLoader.cpp
/// @brief OBJメッシュローダーのユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/render/ObjLoader.hpp>

using namespace mitiru::render;
using Catch::Approx;

// ── 基本パーステスト ────────────────────────────────

TEST_CASE("ObjLoader - parse simple triangle", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"f 1 2 3\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->vertexCount() == 3);
	REQUIRE(mesh->indexCount() == 3);
}

TEST_CASE("ObjLoader - parse quad as two triangles", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 1 1 0\n"
		"v 0 1 0\n"
		"f 1 2 3 4\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	/// 4頂点のクワッドは2三角形 = 6インデックス
	REQUIRE(mesh->indexCount() == 6);
}

TEST_CASE("ObjLoader - parse with normals", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"vn 0 0 1\n"
		"f 1//1 2//1 3//1\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->vertexCount() == 3);

	const auto& verts = mesh->vertices();
	REQUIRE(verts[0].normal.z == Approx(1.0f));
}

TEST_CASE("ObjLoader - parse with texcoords and normals", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"vt 0.0 0.0\n"
		"vt 1.0 0.0\n"
		"vt 0.0 1.0\n"
		"vn 0 0 1\n"
		"f 1/1/1 2/2/1 3/3/1\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->vertexCount() == 3);

	const auto& verts = mesh->vertices();
	REQUIRE(verts[1].texCoord.x == Approx(1.0f));
	REQUIRE(verts[1].texCoord.y == Approx(0.0f));
}

TEST_CASE("ObjLoader - parse with texcoords only (v/vt)", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n"
		"vt 0.5 0.5\n"
		"f 1/1 2/1 3/1\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->vertexCount() == 3);

	const auto& verts = mesh->vertices();
	REQUIRE(verts[0].texCoord.x == Approx(0.5f));
}

// ── 頂点位置テスト ──────────────────────────────────

TEST_CASE("ObjLoader - vertex positions are correct", "[render][obj]")
{
	const char* obj =
		"v 1.5 2.5 3.5\n"
		"v -1.0 0.0 0.0\n"
		"v 0.0 -1.0 0.0\n"
		"f 1 2 3\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());

	const auto& verts = mesh->vertices();
	REQUIRE(verts[0].position.x == Approx(1.5f));
	REQUIRE(verts[0].position.y == Approx(2.5f));
	REQUIRE(verts[0].position.z == Approx(3.5f));
	REQUIRE(verts[1].position.x == Approx(-1.0f));
}

// ── エラーケーステスト ──────────────────────────────

TEST_CASE("ObjLoader - empty input returns nullopt", "[render][obj]")
{
	auto mesh = loadObjFromString("");
	REQUIRE_FALSE(mesh.has_value());
}

TEST_CASE("ObjLoader - no faces returns nullopt", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 0 1 0\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE_FALSE(mesh.has_value());
}

TEST_CASE("ObjLoader - comments are skipped", "[render][obj]")
{
	const char* obj =
		"# This is a comment\n"
		"v 0 0 0\n"
		"v 1 0 0\n"
		"# Another comment\n"
		"v 0 1 0\n"
		"f 1 2 3\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->vertexCount() == 3);
}

// ── 複数フェーステスト ──────────────────────────────

TEST_CASE("ObjLoader - multiple faces", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 1 1 0\n"
		"v 0 1 0\n"
		"f 1 2 3\n"
		"f 1 3 4\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	REQUIRE(mesh->indexCount() == 6);
}

// ── 頂点共有テスト ──────────────────────────────────

TEST_CASE("ObjLoader - shared vertices are deduplicated", "[render][obj]")
{
	const char* obj =
		"v 0 0 0\n"
		"v 1 0 0\n"
		"v 1 1 0\n"
		"v 0 1 0\n"
		"f 1 2 3\n"
		"f 1 3 4\n";

	auto mesh = loadObjFromString(obj);
	REQUIRE(mesh.has_value());
	/// 頂点1と3は共有されるので4頂点のまま
	REQUIRE(mesh->vertexCount() == 4);
	REQUIRE(mesh->indexCount() == 6);
}

// ── ファイル読み込みテスト ──────────────────────────

TEST_CASE("ObjLoader - loadObjFromFile with invalid path returns nullopt", "[render][obj]")
{
	auto mesh = loadObjFromFile("nonexistent_file.obj");
	REQUIRE_FALSE(mesh.has_value());
}
