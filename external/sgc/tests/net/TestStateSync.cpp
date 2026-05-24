/// @file TestStateSync.cpp
/// @brief StateSync.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include "sgc/net/StateSync.hpp"

TEST_CASE("StateSyncManager updateSlot increments version", "[net][sync]")
{
	sgc::StateSyncManager sync;

	std::vector<std::byte> data1 = {std::byte{0x01}, std::byte{0x02}};
	sync.updateSlot(1, data1);

	REQUIRE(sync.hasSlot(1));
	REQUIRE(sync.slotCount() == 1);

	auto versions = sync.versionMap();
	REQUIRE(versions[1] == 1);

	// 再更新でバージョンがインクリメントされる
	std::vector<std::byte> data2 = {std::byte{0x03}};
	sync.updateSlot(1, data2);

	versions = sync.versionMap();
	REQUIRE(versions[1] == 2);
}

TEST_CASE("StateSyncManager getChangedSlots returns only changed", "[net][sync]")
{
	sgc::StateSyncManager sync;

	sync.updateSlot(1, {std::byte{0x01}});
	sync.updateSlot(2, {std::byte{0x02}});
	sync.updateSlot(3, {std::byte{0x03}});

	// リモートはスロット1のv1とスロット2のv1を知っている
	std::unordered_map<std::uint32_t, std::uint64_t> known = {
		{1, 1},
		{2, 1}
	};

	// スロット3は未知なので差分に含まれる
	auto changed = sync.getChangedSlots(known);
	REQUIRE(changed.size() == 1);
	REQUIRE(changed[0].id == 3);

	// スロット1を更新
	sync.updateSlot(1, {std::byte{0x10}});

	// リモートが知っているv1よりスロット1がv2に更新されたので差分に含まれる
	changed = sync.getChangedSlots(known);
	REQUIRE(changed.size() == 2);
}

TEST_CASE("StateSyncManager getChangedSlots with empty known", "[net][sync]")
{
	sgc::StateSyncManager sync;
	sync.updateSlot(1, {std::byte{0x01}});
	sync.updateSlot(2, {std::byte{0x02}});

	// 空のknown → 全スロットが返る
	std::unordered_map<std::uint32_t, std::uint64_t> empty;
	auto changed = sync.getChangedSlots(empty);
	REQUIRE(changed.size() == 2);
}

TEST_CASE("StateSyncManager applyRemoteSlot updates local", "[net][sync]")
{
	sgc::StateSyncManager sync;
	sync.updateSlot(1, {std::byte{0x01}});  // v1

	// リモートからv5のデータを受信
	sgc::SyncSlot remote;
	remote.id = 1;
	remote.version = 5;
	remote.data = {std::byte{0xFF}};

	sync.applyRemoteSlot(remote);

	auto versions = sync.versionMap();
	REQUIRE(versions[1] == 5);

	const auto* data = sync.slotData(1);
	REQUIRE(data != nullptr);
	REQUIRE(data->size() == 1);
	REQUIRE(static_cast<unsigned char>((*data)[0]) == 0xFF);
}

TEST_CASE("StateSyncManager applyRemoteSlot ignores older version", "[net][sync]")
{
	sgc::StateSyncManager sync;
	sync.updateSlot(1, {std::byte{0x01}});
	sync.updateSlot(1, {std::byte{0x02}});
	sync.updateSlot(1, {std::byte{0x03}});  // v3

	// リモートからv1のデータ → 古いので無視
	sgc::SyncSlot remote;
	remote.id = 1;
	remote.version = 1;
	remote.data = {std::byte{0x00}};

	sync.applyRemoteSlot(remote);

	auto versions = sync.versionMap();
	REQUIRE(versions[1] == 3);  // 変更されない
}

TEST_CASE("StateSyncManager versionMap returns all versions", "[net][sync]")
{
	sgc::StateSyncManager sync;
	sync.updateSlot(10, {std::byte{0x0A}});
	sync.updateSlot(20, {std::byte{0x14}});
	sync.updateSlot(10, {std::byte{0x0B}});  // v2

	auto versions = sync.versionMap();
	REQUIRE(versions.size() == 2);
	REQUIRE(versions[10] == 2);
	REQUIRE(versions[20] == 1);
}

TEST_CASE("StateSyncManager removeSlot", "[net][sync]")
{
	sgc::StateSyncManager sync;
	sync.updateSlot(1, {std::byte{0x01}});
	sync.updateSlot(2, {std::byte{0x02}});

	sync.removeSlot(1);
	REQUIRE_FALSE(sync.hasSlot(1));
	REQUIRE(sync.hasSlot(2));
	REQUIRE(sync.slotCount() == 1);
}

TEST_CASE("StateSyncManager clear", "[net][sync]")
{
	sgc::StateSyncManager sync;
	sync.updateSlot(1, {std::byte{0x01}});
	sync.updateSlot(2, {std::byte{0x02}});

	sync.clear();
	REQUIRE(sync.slotCount() == 0);
	REQUIRE_FALSE(sync.hasSlot(1));
	REQUIRE_FALSE(sync.hasSlot(2));
}

TEST_CASE("StateSyncManager slotData returns nullptr for missing", "[net][sync]")
{
	sgc::StateSyncManager sync;
	REQUIRE(sync.slotData(999) == nullptr);
}

TEST_CASE("StateSyncManager applyRemoteSlot creates new slot", "[net][sync]")
{
	sgc::StateSyncManager sync;

	sgc::SyncSlot remote;
	remote.id = 42;
	remote.version = 3;
	remote.data = {std::byte{0xAB}, std::byte{0xCD}};

	sync.applyRemoteSlot(remote);

	REQUIRE(sync.hasSlot(42));
	auto versions = sync.versionMap();
	REQUIRE(versions[42] == 3);
}
