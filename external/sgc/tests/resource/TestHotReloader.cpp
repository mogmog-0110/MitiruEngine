#include <catch2/catch_test_macros.hpp>
#include <sgc/resource/HotReloader.hpp>

#include <filesystem>
#include <fstream>
#include <thread>

using namespace sgc::resource;

namespace
{

/// テスト用一時ファイルを作成するヘルパー
class TempFile
{
public:
	explicit TempFile(const std::string& name)
		: m_path(std::filesystem::temp_directory_path() / name)
	{
		write("initial");
	}

	~TempFile()
	{
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
	}

	void write(const std::string& content) const
	{
		std::ofstream ofs(m_path);
		ofs << content;
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	// コピー禁止
	TempFile(const TempFile&) = delete;
	TempFile& operator=(const TempFile&) = delete;

private:
	std::filesystem::path m_path;
};

} // namespace

TEST_CASE("HotReloader - initial state", "[resource]")
{
	HotReloader reloader(500);
	REQUIRE(reloader.watchCount() == 0);
	REQUIRE(reloader.pollIntervalMs() == 500);
}

TEST_CASE("HotReloader - watch existing file", "[resource]")
{
	TempFile file("sgc_test_hot_reload.txt");
	HotReloader reloader;

	bool result = reloader.watch(file.path(), [](const std::filesystem::path&) {});
	REQUIRE(result);
	REQUIRE(reloader.watchCount() == 1);
}

TEST_CASE("HotReloader - watch nonexistent file returns false", "[resource]")
{
	HotReloader reloader;
	bool result = reloader.watch("nonexistent_file_xyz.tmp", [](const std::filesystem::path&) {});
	REQUIRE_FALSE(result);
	REQUIRE(reloader.watchCount() == 0);
}

TEST_CASE("HotReloader - unwatch removes entry", "[resource]")
{
	TempFile file("sgc_test_hot_unwatch.txt");
	HotReloader reloader;
	reloader.watch(file.path(), [](const std::filesystem::path&) {});
	REQUIRE(reloader.watchCount() == 1);

	reloader.unwatch(file.path());
	REQUIRE(reloader.watchCount() == 0);
}

TEST_CASE("HotReloader - forcePoll detects changes", "[resource]")
{
	TempFile file("sgc_test_hot_force.txt");
	HotReloader reloader;

	bool changed = false;
	reloader.watch(file.path(), [&changed](const std::filesystem::path&)
	{
		changed = true;
	});

	// 初回チェック：変更なし
	REQUIRE(reloader.forcePoll() == 0);
	REQUIRE_FALSE(changed);

	// ファイルを変更（タイムスタンプの分解能のため少し待つ）
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	file.write("modified content");

	const auto count = reloader.forcePoll();
	REQUIRE(count == 1);
	REQUIRE(changed);
}

TEST_CASE("HotReloader - watchedPaths returns all paths", "[resource]")
{
	TempFile file1("sgc_test_hp1.txt");
	TempFile file2("sgc_test_hp2.txt");
	HotReloader reloader;

	reloader.watch(file1.path(), [](const std::filesystem::path&) {});
	reloader.watch(file2.path(), [](const std::filesystem::path&) {});

	const auto paths = reloader.watchedPaths();
	REQUIRE(paths.size() == 2);
}

TEST_CASE("HotReloader - clear removes all watches", "[resource]")
{
	TempFile file("sgc_test_hclear.txt");
	HotReloader reloader;
	reloader.watch(file.path(), [](const std::filesystem::path&) {});
	REQUIRE(reloader.watchCount() == 1);

	reloader.clear();
	REQUIRE(reloader.watchCount() == 0);
}

TEST_CASE("HotReloader - setPollInterval changes interval", "[resource]")
{
	HotReloader reloader(1000);
	REQUIRE(reloader.pollIntervalMs() == 1000);

	reloader.setPollInterval(500);
	REQUIRE(reloader.pollIntervalMs() == 500);
}
