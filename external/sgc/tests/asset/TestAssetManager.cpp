#include <catch2/catch_test_macros.hpp>
#include <sgc/asset/AssetManager.hpp>

using namespace sgc::asset;

namespace
{

/// テスト用アセット型
struct TestTexture
{
	int width;
	int height;
	std::string name;
};

/// テスト用ローダー（常に成功）
struct TestTextureLoader : IAssetLoader<TestTexture>
{
	sgc::Result<std::shared_ptr<TestTexture>> load(const std::string& path) override
	{
		return std::make_shared<TestTexture>(TestTexture{256, 256, path});
	}
};

/// テスト用ローダー（常に失敗）
struct FailingLoader : IAssetLoader<TestTexture>
{
	sgc::Result<std::shared_ptr<TestTexture>> load(const std::string&) override
	{
		return {sgc::ERROR_TAG, sgc::Error{"load failed"}};
	}
};

} // namespace

TEST_CASE("AssetManager - load and get asset", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	auto handle = manager.load("player.png");
	REQUIRE(handle.isValid());

	auto* tex = manager.get(handle);
	REQUIRE(tex != nullptr);
	REQUIRE(tex->width == 256);
	REQUIRE(tex->name == "player.png");
}

TEST_CASE("AssetManager - load returns same handle for same path", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	auto h1 = manager.load("player.png");
	auto h2 = manager.load("player.png");

	REQUIRE(h1 == h2);
	REQUIRE(manager.loadedCount() == 1);
}

TEST_CASE("AssetManager - has returns true for loaded asset", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	REQUIRE_FALSE(manager.has("player.png"));
	[[maybe_unused]] auto h = manager.load("player.png");
	REQUIRE(manager.has("player.png"));
}

TEST_CASE("AssetManager - unload removes asset", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	auto handle = manager.load("player.png");
	REQUIRE(manager.has("player.png"));

	manager.unload("player.png");
	REQUIRE_FALSE(manager.has("player.png"));
	REQUIRE(manager.get(handle) == nullptr);  // 旧ハンドルは無効
}

TEST_CASE("AssetManager - state tracking", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	auto null = AssetHandle<TestTexture>::null();
	REQUIRE(manager.state(null) == AssetState::Unloaded);

	auto handle = manager.load("player.png");
	REQUIRE(manager.state(handle) == AssetState::Loaded);

	manager.unload("player.png");
	REQUIRE(manager.state(handle) == AssetState::Unloaded);
}

TEST_CASE("AssetManager - failed load returns null handle", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<FailingLoader>());

	auto handle = manager.load("missing.png");
	REQUIRE_FALSE(handle.isValid());
}

TEST_CASE("AssetManager - load without loader returns null", "[asset]")
{
	AssetManager<TestTexture> manager;
	auto handle = manager.load("player.png");
	REQUIRE_FALSE(handle.isValid());
}

TEST_CASE("AssetManager - getShared returns shared_ptr", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	auto handle = manager.load("player.png");
	auto shared = manager.getShared(handle);
	REQUIRE(shared != nullptr);
	REQUIRE(shared->name == "player.png");
}

TEST_CASE("AssetManager - clear removes all assets", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	[[maybe_unused]] auto ha = manager.load("a.png");
	[[maybe_unused]] auto hb = manager.load("b.png");
	REQUIRE(manager.loadedCount() == 2);

	manager.clear();
	REQUIRE(manager.loadedCount() == 0);
	REQUIRE(manager.slotCount() == 0);
}

TEST_CASE("AssetManager - multiple assets", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	auto h1 = manager.load("a.png");
	auto h2 = manager.load("b.png");
	auto h3 = manager.load("c.png");

	REQUIRE(manager.loadedCount() == 3);
	REQUIRE(manager.get(h1)->name == "a.png");
	REQUIRE(manager.get(h2)->name == "b.png");
	REQUIRE(manager.get(h3)->name == "c.png");
}

TEST_CASE("AssetManager - generation invalidates old handles after unload+reload", "[asset]")
{
	AssetManager<TestTexture> manager;
	manager.setLoader(std::make_unique<TestTextureLoader>());

	auto oldHandle = manager.load("player.png");
	manager.unload("player.png");

	// 旧ハンドルではアクセスできない
	REQUIRE(manager.get(oldHandle) == nullptr);
}
