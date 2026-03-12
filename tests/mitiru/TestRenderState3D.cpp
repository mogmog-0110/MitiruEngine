#include <catch2/catch_test_macros.hpp>

#include <mitiru/render/RenderState3D.hpp>

// ============================================================
// RenderState3D - default values
// ============================================================

TEST_CASE("RenderState3D - default is opaque preset", "[renderstate3d]")
{
	constexpr mitiru::render::RenderState3D state;

	REQUIRE(state.wireframe == false);
	REQUIRE(state.cullMode == mitiru::render::CullMode::Back);
	REQUIRE(state.depthTest == true);
	REQUIRE(state.depthWrite == true);
	REQUIRE(state.blendEnabled == false);
}

// ============================================================
// RenderState3D - presets
// ============================================================

TEST_CASE("RenderState3D - opaque preset", "[renderstate3d]")
{
	constexpr auto state = mitiru::render::RenderState3D::opaque();

	REQUIRE(state.wireframe == false);
	REQUIRE(state.cullMode == mitiru::render::CullMode::Back);
	REQUIRE(state.depthTest == true);
	REQUIRE(state.depthWrite == true);
	REQUIRE(state.blendEnabled == false);
}

TEST_CASE("RenderState3D - transparent preset", "[renderstate3d]")
{
	constexpr auto state = mitiru::render::RenderState3D::transparent();

	REQUIRE(state.wireframe == false);
	REQUIRE(state.cullMode == mitiru::render::CullMode::Back);
	REQUIRE(state.depthTest == true);
	REQUIRE(state.depthWrite == false);
	REQUIRE(state.blendEnabled == true);
}

TEST_CASE("RenderState3D - wireframe preset", "[renderstate3d]")
{
	constexpr auto state = mitiru::render::RenderState3D::wireframeState();

	REQUIRE(state.wireframe == true);
	REQUIRE(state.cullMode == mitiru::render::CullMode::None);
	REQUIRE(state.depthTest == true);
	REQUIRE(state.depthWrite == true);
	REQUIRE(state.blendEnabled == false);
}

TEST_CASE("RenderState3D - noCull preset", "[renderstate3d]")
{
	constexpr auto state = mitiru::render::RenderState3D::noCull();

	REQUIRE(state.wireframe == false);
	REQUIRE(state.cullMode == mitiru::render::CullMode::None);
	REQUIRE(state.depthTest == true);
	REQUIRE(state.depthWrite == true);
	REQUIRE(state.blendEnabled == false);
}

// ============================================================
// RenderState3D - comparison
// ============================================================

TEST_CASE("RenderState3D - equality comparison same state", "[renderstate3d]")
{
	constexpr auto a = mitiru::render::RenderState3D::opaque();
	constexpr auto b = mitiru::render::RenderState3D::opaque();

	REQUIRE(a == b);
	REQUIRE_FALSE(a != b);
}

TEST_CASE("RenderState3D - inequality on wireframe change", "[renderstate3d]")
{
	auto a = mitiru::render::RenderState3D::opaque();
	auto b = mitiru::render::RenderState3D::opaque();
	b.wireframe = true;

	REQUIRE(a != b);
	REQUIRE_FALSE(a == b);
}

TEST_CASE("RenderState3D - inequality on cullMode change", "[renderstate3d]")
{
	auto a = mitiru::render::RenderState3D::opaque();
	auto b = mitiru::render::RenderState3D::opaque();
	b.cullMode = mitiru::render::CullMode::Front;

	REQUIRE(a != b);
}

TEST_CASE("RenderState3D - inequality on depthTest change", "[renderstate3d]")
{
	auto a = mitiru::render::RenderState3D::opaque();
	auto b = mitiru::render::RenderState3D::opaque();
	b.depthTest = false;

	REQUIRE(a != b);
}

TEST_CASE("RenderState3D - inequality on blendEnabled change", "[renderstate3d]")
{
	auto a = mitiru::render::RenderState3D::opaque();
	auto b = mitiru::render::RenderState3D::opaque();
	b.blendEnabled = true;

	REQUIRE(a != b);
}

TEST_CASE("RenderState3D - opaque differs from transparent", "[renderstate3d]")
{
	constexpr auto opaque = mitiru::render::RenderState3D::opaque();
	constexpr auto transparent = mitiru::render::RenderState3D::transparent();

	REQUIRE(opaque != transparent);
}

TEST_CASE("RenderState3D - opaque differs from wireframe", "[renderstate3d]")
{
	constexpr auto opaque = mitiru::render::RenderState3D::opaque();
	constexpr auto wire = mitiru::render::RenderState3D::wireframeState();

	REQUIRE(opaque != wire);
}

// ============================================================
// RenderState3D - state change detection pattern
// ============================================================

TEST_CASE("RenderState3D - detect state change for pipeline rebind", "[renderstate3d]")
{
	auto current = mitiru::render::RenderState3D::opaque();
	const auto next = mitiru::render::RenderState3D::transparent();

	/// 変更検出パターン
	const bool needsRebind = (current != next);
	REQUIRE(needsRebind == true);

	current = next;
	REQUIRE(current == next);
}
