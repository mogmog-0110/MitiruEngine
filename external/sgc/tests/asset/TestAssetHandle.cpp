#include <catch2/catch_test_macros.hpp>
#include <sgc/asset/AssetHandle.hpp>

#include <unordered_set>

using namespace sgc::asset;

/// テスト用ダミー型
struct DummyAsset {};
struct OtherAsset {};

TEST_CASE("AssetHandle - default is null", "[asset]")
{
	const AssetHandle<DummyAsset> h;
	REQUIRE_FALSE(h.isValid());
	REQUIRE_FALSE(static_cast<bool>(h));
}

TEST_CASE("AssetHandle - null() returns invalid handle", "[asset]")
{
	const auto h = AssetHandle<DummyAsset>::null();
	REQUIRE_FALSE(h.isValid());
	REQUIRE(h.index == AssetHandle<DummyAsset>::INVALID_INDEX);
}

TEST_CASE("AssetHandle - valid handle", "[asset]")
{
	const AssetHandle<DummyAsset> h{0, 1};
	REQUIRE(h.isValid());
	REQUIRE(static_cast<bool>(h));
	REQUIRE(h.index == 0);
	REQUIRE(h.generation == 1);
}

TEST_CASE("AssetHandle - equality comparison", "[asset]")
{
	const AssetHandle<DummyAsset> a{0, 1};
	const AssetHandle<DummyAsset> b{0, 1};
	const AssetHandle<DummyAsset> c{0, 2};
	const AssetHandle<DummyAsset> d{1, 1};

	REQUIRE(a == b);
	REQUIRE_FALSE(a == c);
	REQUIRE_FALSE(a == d);
}

TEST_CASE("AssetHandle - hashable for unordered containers", "[asset]")
{
	std::unordered_set<AssetHandle<DummyAsset>> set;
	set.insert(AssetHandle<DummyAsset>{0, 1});
	set.insert(AssetHandle<DummyAsset>{1, 1});
	set.insert(AssetHandle<DummyAsset>{0, 1});  // 重複

	REQUIRE(set.size() == 2);
}

TEST_CASE("AssetHandle - different asset types are distinct types", "[asset]")
{
	// コンパイル時の型安全性確認
	[[maybe_unused]] const AssetHandle<DummyAsset> a{0, 1};
	[[maybe_unused]] const AssetHandle<OtherAsset> b{0, 1};
	// a == b はコンパイルエラーになる（型が違う）
	REQUIRE(true);  // コンパイルが通ること自体がテスト
}
