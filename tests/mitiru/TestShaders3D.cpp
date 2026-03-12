/// @file TestShaders3D.cpp
/// @brief 3Dシェーダー定義のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include <mitiru/render/DefaultShaders3D.hpp>

#include <cstring>
#include <string_view>

using namespace mitiru::render;

TEST_CASE("DefaultShaders3D - Phong VS is non-empty", "[render][shader3d]")
{
	REQUIRE(DEFAULT_VS_3D != nullptr);
	REQUIRE(std::strlen(DEFAULT_VS_3D) > 0);
}

TEST_CASE("DefaultShaders3D - Phong PS is non-empty", "[render][shader3d]")
{
	REQUIRE(DEFAULT_PS_3D != nullptr);
	REQUIRE(std::strlen(DEFAULT_PS_3D) > 0);
}

TEST_CASE("DefaultShaders3D - VS contains main entry point", "[render][shader3d]")
{
	const std::string_view vs(DEFAULT_VS_3D);
	REQUIRE(vs.find("VSMain") != std::string_view::npos);
}

TEST_CASE("DefaultShaders3D - PS contains main entry point", "[render][shader3d]")
{
	const std::string_view ps(DEFAULT_PS_3D);
	REQUIRE(ps.find("PSMain") != std::string_view::npos);
}

TEST_CASE("DefaultShaders3D - VS contains transform matrices", "[render][shader3d]")
{
	const std::string_view vs(DEFAULT_VS_3D);
	REQUIRE(vs.find("World") != std::string_view::npos);
	REQUIRE(vs.find("View") != std::string_view::npos);
	REQUIRE(vs.find("Projection") != std::string_view::npos);
}

TEST_CASE("DefaultShaders3D - PS contains Phong lighting terms", "[render][shader3d]")
{
	const std::string_view ps(DEFAULT_PS_3D);
	REQUIRE(ps.find("LightDir") != std::string_view::npos);
	REQUIRE(ps.find("LightColor") != std::string_view::npos);
	REQUIRE(ps.find("AmbientColor") != std::string_view::npos);
	REQUIRE(ps.find("MaterialDiffuse") != std::string_view::npos);
	REQUIRE(ps.find("MaterialSpecular") != std::string_view::npos);
	REQUIRE(ps.find("MaterialShininess") != std::string_view::npos);
}

TEST_CASE("DefaultShaders3D - PS contains specular calculation", "[render][shader3d]")
{
	const std::string_view ps(DEFAULT_PS_3D);
	REQUIRE(ps.find("specular") != std::string_view::npos);
	REQUIRE(ps.find("diffuse") != std::string_view::npos);
	REQUIRE(ps.find("ambient") != std::string_view::npos);
}

TEST_CASE("DefaultShaders3D - Unlit VS is non-empty", "[render][shader3d]")
{
	REQUIRE(UNLIT_VS_3D != nullptr);
	REQUIRE(std::strlen(UNLIT_VS_3D) > 0);

	const std::string_view vs(UNLIT_VS_3D);
	REQUIRE(vs.find("VSMain") != std::string_view::npos);
}

TEST_CASE("DefaultShaders3D - Unlit PS is non-empty", "[render][shader3d]")
{
	REQUIRE(UNLIT_PS_3D != nullptr);
	REQUIRE(std::strlen(UNLIT_PS_3D) > 0);

	const std::string_view ps(UNLIT_PS_3D);
	REQUIRE(ps.find("PSMain") != std::string_view::npos);
}
