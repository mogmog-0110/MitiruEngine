#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/graphics3d/Material.hpp"

using namespace sgc;
using namespace sgc::graphics3d;

// ── MaterialProperties ──────────────────────────────────────

TEST_CASE("MaterialProperties - default values", "[graphics3d][material-system]")
{
	MaterialProperties props;
	REQUIRE(props.ambient.r == Catch::Approx(0.2f));
	REQUIRE(props.diffuse.r == Catch::Approx(0.8f));
	REQUIRE(props.specular.r == Catch::Approx(1.0f));
	REQUIRE(props.shininess == Catch::Approx(32.0f));
	REQUIRE(props.opacity == Catch::Approx(1.0f));
}

TEST_CASE("MaterialProperties - equality", "[graphics3d][material-system]")
{
	MaterialProperties a;
	MaterialProperties b;
	REQUIRE(a == b);

	b.shininess = 64.0f;
	REQUIRE_FALSE(a == b);
}

// ── TextureSlot ─────────────────────────────────────────────

TEST_CASE("TextureSlot - enumeration values", "[graphics3d][material-system]")
{
	REQUIRE(static_cast<int>(TextureSlot::Diffuse) == 0);
	REQUIRE(static_cast<int>(TextureSlot::Normal) == 1);
	REQUIRE(static_cast<int>(TextureSlot::Specular) == 2);
	REQUIRE(static_cast<int>(TextureSlot::Count) == 8);
}

// ── MaterialSystem construction ─────────────────────────────

TEST_CASE("MaterialSystem - default construction", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	REQUIRE(mat.name() == "default");
	REQUIRE(mat.properties().shininess == Catch::Approx(32.0f));
	REQUIRE(mat.textureCount() == 0);
	REQUIRE(mat.shaderParamCount() == 0);
}

TEST_CASE("MaterialSystem - named construction", "[graphics3d][material-system]")
{
	MaterialSystem mat{"metal"};
	REQUIRE(mat.name() == "metal");
}

TEST_CASE("MaterialSystem - name and properties construction", "[graphics3d][material-system]")
{
	MaterialProperties props;
	props.shininess = 128.0f;
	MaterialSystem mat{"shiny", props};
	REQUIRE(mat.name() == "shiny");
	REQUIRE(mat.properties().shininess == Catch::Approx(128.0f));
}

// ── Texture slots ───────────────────────────────────────────

TEST_CASE("MaterialSystem - set and get texture", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setTexture(TextureSlot::Diffuse, "wood_diffuse");

	REQUIRE(mat.hasTexture(TextureSlot::Diffuse));
	REQUIRE(mat.texture(TextureSlot::Diffuse).value() == "wood_diffuse");
	REQUIRE(mat.textureCount() == 1);
}

TEST_CASE("MaterialSystem - texture not set returns nullopt", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	REQUIRE_FALSE(mat.hasTexture(TextureSlot::Normal));
	REQUIRE_FALSE(mat.texture(TextureSlot::Normal).has_value());
}

TEST_CASE("MaterialSystem - multiple texture slots", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setTexture(TextureSlot::Diffuse, "diffuse_tex");
	mat.setTexture(TextureSlot::Normal, "normal_tex");
	mat.setTexture(TextureSlot::Specular, "specular_tex");

	REQUIRE(mat.textureCount() == 3);
	REQUIRE(mat.texture(TextureSlot::Diffuse).value() == "diffuse_tex");
	REQUIRE(mat.texture(TextureSlot::Normal).value() == "normal_tex");
}

TEST_CASE("MaterialSystem - clear texture", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setTexture(TextureSlot::Diffuse, "tex");
	REQUIRE(mat.hasTexture(TextureSlot::Diffuse));

	mat.clearTexture(TextureSlot::Diffuse);
	REQUIRE_FALSE(mat.hasTexture(TextureSlot::Diffuse));
	REQUIRE(mat.textureCount() == 0);
}

TEST_CASE("MaterialSystem - overwrite texture", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setTexture(TextureSlot::Diffuse, "old_tex");
	mat.setTexture(TextureSlot::Diffuse, "new_tex");

	REQUIRE(mat.texture(TextureSlot::Diffuse).value() == "new_tex");
	REQUIRE(mat.textureCount() == 1);
}

// ── Shader parameters ───────────────────────────────────────

TEST_CASE("MaterialSystem - set float shader param", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setShaderParam("u_tiling", 2.0f);

	REQUIRE(mat.hasShaderParam("u_tiling"));
	const auto val = mat.shaderParam("u_tiling");
	REQUIRE(val.has_value());
	REQUIRE(std::get<float>(*val) == Catch::Approx(2.0f));
}

TEST_CASE("MaterialSystem - set int shader param", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setShaderParam("u_layer", int32_t{3});

	const auto val = mat.shaderParam("u_layer");
	REQUIRE(val.has_value());
	REQUIRE(std::get<int32_t>(*val) == 3);
}

TEST_CASE("MaterialSystem - set Vec3f shader param", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setShaderParam("u_offset", Vec3f{1.0f, 2.0f, 3.0f});

	const auto val = mat.shaderParam("u_offset");
	REQUIRE(val.has_value());
	const auto& v = std::get<Vec3f>(*val);
	REQUIRE(v.x == Catch::Approx(1.0f));
	REQUIRE(v.y == Catch::Approx(2.0f));
}

TEST_CASE("MaterialSystem - missing shader param returns nullopt", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	REQUIRE_FALSE(mat.hasShaderParam("u_missing"));
	REQUIRE_FALSE(mat.shaderParam("u_missing").has_value());
}

TEST_CASE("MaterialSystem - shader param count", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	mat.setShaderParam("a", 1.0f);
	mat.setShaderParam("b", int32_t{2});
	mat.setShaderParam("c", Vec3f{0, 0, 0});

	REQUIRE(mat.shaderParamCount() == 3);
}

// ── Sort key and comparison ─────────────────────────────────

TEST_CASE("MaterialSystem - opaque before transparent in sort key", "[graphics3d][material-system]")
{
	MaterialSystem opaque{"opaque"};

	MaterialProperties transparentProps;
	transparentProps.opacity = 0.5f;
	MaterialSystem transparent{"transparent", transparentProps};

	REQUIRE(opaque.sortKey() < transparent.sortKey());
	REQUIRE(opaque < transparent);
}

TEST_CASE("MaterialSystem - sort key varies by texture config", "[graphics3d][material-system]")
{
	MaterialSystem a{"mat_a"};
	MaterialSystem b{"mat_b"};
	b.setTexture(TextureSlot::Diffuse, "tex");

	// 異なるテクスチャ構成→異なるソートキー
	REQUIRE(a.sortKey() != b.sortKey());
}

TEST_CASE("MaterialSystem - sort key varies by name", "[graphics3d][material-system]")
{
	MaterialSystem a{"alpha"};
	MaterialSystem b{"beta"};

	// 異なる名前→異なるソートキー（同テクスチャ構成でも）
	REQUIRE(a.sortKey() != b.sortKey());
}

// ── Properties update ───────────────────────────────────────

TEST_CASE("MaterialSystem - setProperties updates", "[graphics3d][material-system]")
{
	MaterialSystem mat;
	MaterialProperties props;
	props.shininess = 256.0f;
	props.opacity = 0.5f;
	mat.setProperties(props);

	REQUIRE(mat.properties().shininess == Catch::Approx(256.0f));
	REQUIRE(mat.properties().opacity == Catch::Approx(0.5f));
}
