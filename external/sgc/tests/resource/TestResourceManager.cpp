#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <chrono>

#include "sgc/resource/ResourceManager.hpp"

/// @brief テスト用リソース型
struct TestResource
{
	std::string data;
	int value = 0;
};

/// @brief テスト用ローダー（成功）
struct TestLoader
{
	sgc::Result<TestResource> load(const std::string& path)
	{
		TestResource res;
		res.data = "loaded:" + path;
		res.value = 42;
		return res;
	}
};

/// @brief テスト用ローダー（失敗）
struct FailLoader
{
	sgc::Result<TestResource> load(const std::string& /*path*/)
	{
		return {sgc::ERROR_TAG, sgc::Error{"load failed"}};
	}
};

/// @brief 遅延ローダー（非同期テスト用）
struct SlowLoader
{
	sgc::Result<TestResource> load(const std::string& path)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		TestResource res;
		res.data = "async:" + path;
		res.value = 99;
		return res;
	}
};

TEST_CASE("ResourceManager sync load succeeds", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto handle = manager.load("test.txt", TestLoader{});

	REQUIRE(manager.getState(handle) == sgc::ResourceState::Loaded);

	auto* res = manager.get(handle);
	REQUIRE(res != nullptr);
	REQUIRE(res->data == "loaded:test.txt");
	REQUIRE(res->value == 42);
}

TEST_CASE("ResourceManager sync load failure sets Error state", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto handle = manager.load("bad.txt", FailLoader{});

	REQUIRE(manager.getState(handle) == sgc::ResourceState::Error);
	REQUIRE(manager.get(handle) == nullptr);
}

TEST_CASE("ResourceManager duplicate path returns same handle", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto h1 = manager.load("test.txt", TestLoader{});
	auto h2 = manager.load("test.txt", TestLoader{});

	REQUIRE(h1 == h2);
	REQUIRE(manager.size() == 1);
}

TEST_CASE("ResourceManager different paths return different handles", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto h1 = manager.load("a.txt", TestLoader{});
	auto h2 = manager.load("b.txt", TestLoader{});

	REQUIRE(h1 != h2);
	REQUIRE(manager.size() == 2);
}

TEST_CASE("ResourceManager release removes resource", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto handle = manager.load("test.txt", TestLoader{});
	REQUIRE(manager.size() == 1);

	manager.release(handle);
	REQUIRE(manager.get(handle) == nullptr);
	REQUIRE(manager.getState(handle) == sgc::ResourceState::Unloaded);
}

TEST_CASE("ResourceManager findByPath returns correct handle", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto handle = manager.load("test.txt", TestLoader{});

	auto found = manager.findByPath("test.txt");
	REQUIRE(found.has_value());
	REQUIRE(found.value() == handle);
}

TEST_CASE("ResourceManager findByPath returns nullopt for unknown path", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto found = manager.findByPath("nonexistent.txt");
	REQUIRE_FALSE(found.has_value());
}

TEST_CASE("ResourceManager findByPath returns nullopt after release", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto handle = manager.load("test.txt", TestLoader{});
	manager.release(handle);

	auto found = manager.findByPath("test.txt");
	REQUIRE_FALSE(found.has_value());
}

TEST_CASE("ResourceManager const get works", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto handle = manager.load("test.txt", TestLoader{});

	const auto& constManager = manager;
	const auto* res = constManager.get(handle);
	REQUIRE(res != nullptr);
	REQUIRE(res->data == "loaded:test.txt");
}

TEST_CASE("ResourceManager async load completes", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto handle = manager.loadAsync("async.txt", SlowLoader{});

	// 直後はLoading状態
	REQUIRE(manager.getState(handle) == sgc::ResourceState::Loading);

	// 完了を待つ
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	REQUIRE(manager.getState(handle) == sgc::ResourceState::Loaded);
	auto* res = manager.get(handle);
	REQUIRE(res != nullptr);
	REQUIRE(res->data == "async:async.txt");
	REQUIRE(res->value == 99);
}

TEST_CASE("ResourceManager async duplicate returns same handle", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto h1 = manager.loadAsync("async.txt", SlowLoader{});
	auto h2 = manager.loadAsync("async.txt", SlowLoader{});

	REQUIRE(h1 == h2);
	REQUIRE(manager.size() == 1);

	// クリーンアップ待ち
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST_CASE("ResourceManager collectGarbage removes completed futures", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	(void)manager.loadAsync("async.txt", SlowLoader{});

	// 完了を待つ
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	// collectGarbageがクラッシュしないことを確認
	manager.collectGarbage();
}

TEST_CASE("ResourceManager size tracks resources", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	REQUIRE(manager.size() == 0);

	auto h1 = manager.load("a.txt", TestLoader{});
	REQUIRE(manager.size() == 1);

	auto h2 = manager.load("b.txt", TestLoader{});
	REQUIRE(manager.size() == 2);

	manager.release(h1);
	REQUIRE(manager.size() == 1);
}

TEST_CASE("ResourceManager get returns nullptr for invalid handle", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	sgc::ResourceHandle invalidHandle;
	REQUIRE(manager.get(invalidHandle) == nullptr);
	REQUIRE(manager.getState(invalidHandle) == sgc::ResourceState::Unloaded);
}

TEST_CASE("ResourceManager reload after release uses new handle", "[resource][manager]")
{
	sgc::ThreadPool pool(2);
	sgc::ResourceManager<TestResource> manager(pool);

	auto h1 = manager.load("test.txt", TestLoader{});
	manager.release(h1);

	auto h2 = manager.load("test.txt", TestLoader{});

	// 新しいハンドルが発行される（世代が異なる）
	REQUIRE(manager.get(h2) != nullptr);
	REQUIRE(manager.size() == 1);
}
