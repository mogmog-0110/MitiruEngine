#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

#include "sgc/resource/HotReload.hpp"

/// @brief テスト用一時ファイルを作成するヘルパー
static std::string createTempFile(const std::string& name, const std::string& content)
{
	auto path = std::filesystem::temp_directory_path() / name;
	std::ofstream ofs(path);
	ofs << content;
	ofs.close();
	return path.string();
}

/// @brief テスト用一時ファイルを削除するヘルパー
static void removeTempFile(const std::string& path)
{
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

TEST_CASE("FileWatcher starts with zero watch count", "[resource][filewatcher]")
{
	sgc::FileWatcher watcher;
	REQUIRE(watcher.watchCount() == 0);
}

TEST_CASE("FileWatcher watch increases count", "[resource][filewatcher]")
{
	sgc::FileWatcher watcher;

	auto path = createTempFile("sgc_test_watch1.txt", "hello");
	watcher.watch(path, [](const std::string&) {});

	REQUIRE(watcher.watchCount() == 1);

	removeTempFile(path);
}

TEST_CASE("FileWatcher unwatch decreases count", "[resource][filewatcher]")
{
	sgc::FileWatcher watcher;

	auto path = createTempFile("sgc_test_watch2.txt", "hello");
	watcher.watch(path, [](const std::string&) {});
	watcher.unwatch(path);

	REQUIRE(watcher.watchCount() == 0);

	removeTempFile(path);
}

TEST_CASE("FileWatcher clear removes all watches", "[resource][filewatcher]")
{
	sgc::FileWatcher watcher;

	auto p1 = createTempFile("sgc_test_watch3a.txt", "a");
	auto p2 = createTempFile("sgc_test_watch3b.txt", "b");

	watcher.watch(p1, [](const std::string&) {});
	watcher.watch(p2, [](const std::string&) {});
	REQUIRE(watcher.watchCount() == 2);

	watcher.clear();
	REQUIRE(watcher.watchCount() == 0);

	removeTempFile(p1);
	removeTempFile(p2);
}

TEST_CASE("FileWatcher poll detects file change", "[resource][filewatcher]")
{
	auto path = createTempFile("sgc_test_watch4.txt", "initial");

	sgc::FileWatcher watcher;
	bool changed = false;
	std::string changedPath;

	watcher.watch(path, [&](const std::string& p) {
		changed = true;
		changedPath = p;
	});

	// 初回pollでは変更なし
	watcher.poll();
	REQUIRE_FALSE(changed);

	// ファイルを変更（タイムスタンプ更新のため少し待つ）
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	{
		std::ofstream ofs(path);
		ofs << "modified";
	}

	watcher.poll();
	REQUIRE(changed);
	REQUIRE(changedPath == path);

	removeTempFile(path);
}

TEST_CASE("FileWatcher poll ignores nonexistent file", "[resource][filewatcher]")
{
	sgc::FileWatcher watcher;
	bool changed = false;

	watcher.watch("nonexistent_sgc_test_file.txt", [&](const std::string&) {
		changed = true;
	});

	// 存在しないファイルではコールバックは呼ばれない
	watcher.poll();
	REQUIRE_FALSE(changed);
}

TEST_CASE("FileWatcher poll no change on unchanged file", "[resource][filewatcher]")
{
	auto path = createTempFile("sgc_test_watch5.txt", "stable");

	sgc::FileWatcher watcher;
	int changeCount = 0;

	watcher.watch(path, [&](const std::string&) {
		++changeCount;
	});

	watcher.poll();
	watcher.poll();
	watcher.poll();

	REQUIRE(changeCount == 0);

	removeTempFile(path);
}

TEST_CASE("FileWatcher multiple watches independent", "[resource][filewatcher]")
{
	auto p1 = createTempFile("sgc_test_watch6a.txt", "a");
	auto p2 = createTempFile("sgc_test_watch6b.txt", "b");

	sgc::FileWatcher watcher;
	bool changed1 = false;
	bool changed2 = false;

	watcher.watch(p1, [&](const std::string&) { changed1 = true; });
	watcher.watch(p2, [&](const std::string&) { changed2 = true; });

	// p1のみ変更
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	{
		std::ofstream ofs(p1);
		ofs << "changed";
	}

	watcher.poll();

	REQUIRE(changed1);
	REQUIRE_FALSE(changed2);

	removeTempFile(p1);
	removeTempFile(p2);
}
