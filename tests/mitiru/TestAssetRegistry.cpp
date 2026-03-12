#include <catch2/catch_test_macros.hpp>

#include "mitiru/asset/AssetRegistry.hpp"

using namespace mitiru::asset;

TEST_CASE("AssetRegistry - register and retrieve", "[mitiru][asset]")
{
	AssetRegistry registry;

	REQUIRE(registry.registerAsset({"tex_01", AssetType::Texture, "textures/player.png", 2048, false}));
	REQUIRE(registry.count() == 1);

	const auto* meta = registry.getMetadata("tex_01");
	REQUIRE(meta != nullptr);
	REQUIRE(meta->id == "tex_01");
	REQUIRE(meta->type == AssetType::Texture);
	REQUIRE(meta->path == "textures/player.png");
	REQUIRE(meta->sizeBytes == 2048);
	REQUIRE_FALSE(meta->loaded);
}

TEST_CASE("AssetRegistry - duplicate id rejected", "[mitiru][asset]")
{
	AssetRegistry registry;

	REQUIRE(registry.registerAsset({"id1", AssetType::Mesh, "a.obj", 100, false}));
	REQUIRE_FALSE(registry.registerAsset({"id1", AssetType::Shader, "b.glsl", 200, false}));
	REQUIRE(registry.count() == 1);
}

TEST_CASE("AssetRegistry - getMetadata returns nullptr for missing", "[mitiru][asset]")
{
	AssetRegistry registry;
	REQUIRE(registry.getMetadata("nonexistent") == nullptr);
}

TEST_CASE("AssetRegistry - findByType", "[mitiru][asset]")
{
	AssetRegistry registry;
	registry.registerAsset({"tex1", AssetType::Texture, "a.png", 100, false});
	registry.registerAsset({"tex2", AssetType::Texture, "b.png", 200, false});
	registry.registerAsset({"snd1", AssetType::Sound, "c.wav", 300, false});

	auto textures = registry.findByType(AssetType::Texture);
	REQUIRE(textures.size() == 2);

	auto sounds = registry.findByType(AssetType::Sound);
	REQUIRE(sounds.size() == 1);

	auto shaders = registry.findByType(AssetType::Shader);
	REQUIRE(shaders.empty());
}

TEST_CASE("AssetRegistry - findByPath", "[mitiru][asset]")
{
	AssetRegistry registry;
	registry.registerAsset({"mesh1", AssetType::Mesh, "models/cube.obj", 500, false});

	const auto* found = registry.findByPath("models/cube.obj");
	REQUIRE(found != nullptr);
	REQUIRE(found->id == "mesh1");

	REQUIRE(registry.findByPath("nonexistent.obj") == nullptr);
}

TEST_CASE("AssetRegistry - unregister", "[mitiru][asset]")
{
	AssetRegistry registry;
	registry.registerAsset({"id1", AssetType::Texture, "a.png", 100, false});

	REQUIRE(registry.unregister("id1"));
	REQUIRE(registry.count() == 0);
	REQUIRE(registry.getMetadata("id1") == nullptr);

	REQUIRE_FALSE(registry.unregister("id1"));
}

TEST_CASE("AssetRegistry - clear", "[mitiru][asset]")
{
	AssetRegistry registry;
	registry.registerAsset({"a", AssetType::Texture, "a.png", 100, false});
	registry.registerAsset({"b", AssetType::Mesh, "b.obj", 200, false});

	registry.clear();
	REQUIRE(registry.count() == 0);
}

TEST_CASE("AssetRegistry - loadManifest", "[mitiru][asset]")
{
	AssetRegistry registry;

	const std::string manifest =
		"player_tex,Texture,textures/player.png\n"
		"cube_mesh,Mesh,models/cube.obj\n"
		"bgm,Sound,audio/bgm.wav\n";

	auto count = registry.loadManifest(manifest);
	REQUIRE(count == 3);
	REQUIRE(registry.count() == 3);

	const auto* tex = registry.getMetadata("player_tex");
	REQUIRE(tex != nullptr);
	REQUIRE(tex->type == AssetType::Texture);
	REQUIRE(tex->path == "textures/player.png");

	const auto* mesh = registry.getMetadata("cube_mesh");
	REQUIRE(mesh != nullptr);
	REQUIRE(mesh->type == AssetType::Mesh);
}

TEST_CASE("AssetRegistry - saveManifest roundtrip", "[mitiru][asset]")
{
	AssetRegistry registry;
	registry.registerAsset({"tex1", AssetType::Texture, "a.png", 0, false});
	registry.registerAsset({"snd1", AssetType::Sound, "b.wav", 0, false});

	auto manifest = registry.saveManifest();
	REQUIRE_FALSE(manifest.empty());

	// 新しいレジストリでロードして確認
	AssetRegistry registry2;
	auto count = registry2.loadManifest(manifest);
	REQUIRE(count == 2);
	REQUIRE(registry2.getMetadata("tex1") != nullptr);
	REQUIRE(registry2.getMetadata("snd1") != nullptr);
}

TEST_CASE("AssetRegistry - loadManifest skips invalid lines", "[mitiru][asset]")
{
	AssetRegistry registry;

	const std::string manifest =
		"valid,Texture,path.png\n"
		"invalid_no_comma\n"
		"bad_type,UnknownType,path.dat\n"
		"\n";

	auto count = registry.loadManifest(manifest);
	REQUIRE(count == 1);
}

TEST_CASE("AssetRegistry - assetTypeToString and stringToAssetType", "[mitiru][asset]")
{
	REQUIRE(assetTypeToString(AssetType::Texture) == "Texture");
	REQUIRE(assetTypeToString(AssetType::Material) == "Material");

	REQUIRE(stringToAssetType("Mesh") == AssetType::Mesh);
	REQUIRE(stringToAssetType("Script") == AssetType::Script);
	REQUIRE_FALSE(stringToAssetType("Bogus").has_value());
}

TEST_CASE("AssetRegistry - mutable metadata access", "[mitiru][asset]")
{
	AssetRegistry registry;
	registry.registerAsset({"tex1", AssetType::Texture, "a.png", 100, false});

	auto* meta = registry.getMetadata("tex1");
	REQUIRE(meta != nullptr);
	meta->loaded = true;
	meta->sizeBytes = 4096;

	const auto* constMeta = registry.getMetadata("tex1");
	REQUIRE(constMeta->loaded);
	REQUIRE(constMeta->sizeBytes == 4096);
}
