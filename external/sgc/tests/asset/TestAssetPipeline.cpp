#include <catch2/catch_test_macros.hpp>
#include <sgc/asset/AssetPipeline.hpp>

using namespace sgc::asset;

namespace
{

/// テスト用のシンプルなローダー
auto makeStringLoader()
{
	return [](const std::string& path) -> std::optional<std::string>
	{
		return "loaded:" + path;
	};
}

/// 常に失敗するローダー
auto makeFailingLoader()
{
	return [](const std::string&) -> std::optional<std::string>
	{
		return std::nullopt;
	};
}

} // namespace

TEST_CASE("AssetId - equality comparison", "[asset]")
{
	const AssetId a{"player"};
	const AssetId b{"player"};
	const AssetId c{"enemy"};
	REQUIRE(a == b);
	REQUIRE_FALSE(a == c);
}

TEST_CASE("AssetManifest - add and getPath", "[asset]")
{
	AssetManifest manifest;
	manifest.add(AssetId{"player"}, "textures/player.png");

	const auto path = manifest.getPath(AssetId{"player"});
	REQUIRE(path.has_value());
	REQUIRE(path.value() == "textures/player.png");
}

TEST_CASE("AssetManifest - getPath returns nullopt for unknown id", "[asset]")
{
	AssetManifest manifest;
	REQUIRE_FALSE(manifest.getPath(AssetId{"unknown"}).has_value());
}

TEST_CASE("AssetManifest - contains and size", "[asset]")
{
	AssetManifest manifest;
	REQUIRE(manifest.size() == 0);
	REQUIRE_FALSE(manifest.contains(AssetId{"x"}));

	manifest.add(AssetId{"x"}, "path/x");
	REQUIRE(manifest.size() == 1);
	REQUIRE(manifest.contains(AssetId{"x"}));
}

TEST_CASE("AssetManifest - remove entry", "[asset]")
{
	AssetManifest manifest;
	manifest.add(AssetId{"a"}, "path/a");
	manifest.remove(AssetId{"a"});
	REQUIRE_FALSE(manifest.contains(AssetId{"a"}));
	REQUIRE(manifest.size() == 0);
}

TEST_CASE("AssetCache - get loads and caches", "[asset]")
{
	AssetManifest manifest;
	manifest.add(AssetId{"item"}, "data/item.dat");

	int loadCount = 0;
	auto loader = [&loadCount](const std::string& path) -> std::optional<std::string>
	{
		++loadCount;
		return "loaded:" + path;
	};

	AssetCache<std::string> cache(loader, manifest);

	const auto* val = cache.get(AssetId{"item"});
	REQUIRE(val != nullptr);
	REQUIRE(*val == "loaded:data/item.dat");
	REQUIRE(loadCount == 1);

	// 2回目はキャッシュから取得
	const auto* val2 = cache.get(AssetId{"item"});
	REQUIRE(val2 != nullptr);
	REQUIRE(loadCount == 1);  // ローダーは呼ばれない
}

TEST_CASE("AssetCache - get returns nullptr for missing manifest entry", "[asset]")
{
	AssetManifest manifest;
	AssetCache<std::string> cache(makeStringLoader(), manifest);

	const auto* val = cache.get(AssetId{"missing"});
	REQUIRE(val == nullptr);
	REQUIRE(cache.state(AssetId{"missing"}) == AssetState::Error);
}

TEST_CASE("AssetCache - failing loader sets Error state", "[asset]")
{
	AssetManifest manifest;
	manifest.add(AssetId{"bad"}, "path/bad");
	AssetCache<std::string> cache(makeFailingLoader(), manifest);

	const auto* val = cache.get(AssetId{"bad"});
	REQUIRE(val == nullptr);
	REQUIRE(cache.state(AssetId{"bad"}) == AssetState::Error);
}

TEST_CASE("AssetCache - unload and clear", "[asset]")
{
	AssetManifest manifest;
	manifest.add(AssetId{"a"}, "p/a");
	manifest.add(AssetId{"b"}, "p/b");
	AssetCache<std::string> cache(makeStringLoader(), manifest);

	cache.get(AssetId{"a"});
	cache.get(AssetId{"b"});
	REQUIRE(cache.cachedCount() == 2);

	cache.unload(AssetId{"a"});
	REQUIRE(cache.cachedCount() == 1);
	REQUIRE(cache.state(AssetId{"a"}) == AssetState::NotLoaded);

	cache.clear();
	REQUIRE(cache.cachedCount() == 0);
}
