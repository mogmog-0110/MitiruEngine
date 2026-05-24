
/// @file TestAsyncLoadBatch.cpp
/// @brief AsyncLoadBatchおよびBatchProgressのテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/resource/AsyncLoadBatch.hpp"
#include "sgc/resource/LoadPriority.hpp"

// ─── BatchProgress ───────────────────────────────────────

TEST_CASE("BatchProgress initial ratio is 1.0 for 0/0", "[resource][BatchProgress]")
{
	const sgc::BatchProgress p{};
	REQUIRE(p.ratio() == Catch::Approx(1.0f));
}

TEST_CASE("BatchProgress ratio tracks loaded", "[resource][BatchProgress]")
{
	const sgc::BatchProgress p{10, 3, 0};
	REQUIRE(p.ratio() == Catch::Approx(0.3f));
}

TEST_CASE("BatchProgress isComplete when all loaded", "[resource][BatchProgress]")
{
	const sgc::BatchProgress p{5, 5, 0};
	REQUIRE(p.isComplete());
}

TEST_CASE("BatchProgress accounts for failures", "[resource][BatchProgress]")
{
	const sgc::BatchProgress p{4, 2, 2};
	REQUIRE(p.ratio() == Catch::Approx(1.0f));
	REQUIRE(p.isComplete());
}

// ─── AsyncLoadBatch ──────────────────────────────────────

TEST_CASE("AsyncLoadBatch starts with empty progress", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	const auto prog = batch.progress();
	REQUIRE(prog.total == 0);
	REQUIRE(prog.loaded == 0);
	REQUIRE(prog.failed == 0);
}

TEST_CASE("AsyncLoadBatch addPending increases total", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	batch.addPending();
	batch.addPending();
	batch.addPending();
	REQUIRE(batch.progress().total == 3);
}

TEST_CASE("AsyncLoadBatch notifyLoaded increases loaded", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	batch.addPending();
	batch.addPending();
	batch.notifyLoaded();
	REQUIRE(batch.progress().loaded == 1);
}

TEST_CASE("AsyncLoadBatch notifyFailed increases failed", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	batch.addPending();
	batch.notifyFailed();
	REQUIRE(batch.progress().failed == 1);
}

TEST_CASE("AsyncLoadBatch cancel sets cancelled flag", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	REQUIRE_FALSE(batch.isCancelled());
	batch.cancel();
	REQUIRE(batch.isCancelled());
}

TEST_CASE("AsyncLoadBatch onProgress callback fires", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	int callCount = 0;
	float lastRatio = 0.0f;

	batch.onProgress([&](const sgc::BatchProgress& p) {
		++callCount;
		lastRatio = p.ratio();
	});

	batch.addPending();
	batch.addPending();
	batch.notifyLoaded();
	batch.processPendingCallbacks();

	REQUIRE(callCount == 1);
	REQUIRE(lastRatio == Catch::Approx(0.5f));
}

TEST_CASE("AsyncLoadBatch onComplete callback fires when all done", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	bool completed = false;
	std::size_t finalLoaded = 0;
	std::size_t finalFailed = 0;

	batch.onComplete([&](const sgc::BatchProgress& p) {
		completed = true;
		finalLoaded = p.loaded;
		finalFailed = p.failed;
	});

	batch.addPending();
	batch.addPending();
	batch.addPending();

	batch.notifyLoaded();
	batch.processPendingCallbacks();
	REQUIRE_FALSE(completed);

	batch.notifyLoaded();
	batch.processPendingCallbacks();
	REQUIRE_FALSE(completed);

	batch.notifyFailed();
	batch.processPendingCallbacks();
	REQUIRE(completed);
	REQUIRE(finalLoaded == 2);
	REQUIRE(finalFailed == 1);
}

TEST_CASE("AsyncLoadBatch processPendingCallbacks dispatches", "[resource][AsyncLoadBatch]")
{
	sgc::AsyncLoadBatch batch;
	int progressCount = 0;

	batch.onProgress([&](const sgc::BatchProgress&) {
		++progressCount;
	});

	batch.addPending();
	batch.addPending();

	// 2回通知 → 2つのコールバックがキューに入る
	batch.notifyLoaded();
	batch.notifyLoaded();

	// まだ処理していないのでカウントは0
	REQUIRE(progressCount == 0);

	// 処理すると2回呼ばれる
	batch.processPendingCallbacks();
	REQUIRE(progressCount == 2);

	// 再度呼んでもキューは空
	batch.processPendingCallbacks();
	REQUIRE(progressCount == 2);
}

// ─── LoadPriority ────────────────────────────────────────

TEST_CASE("LoadPriority values are ordered", "[resource][LoadPriority]")
{
	REQUIRE(static_cast<int>(sgc::LoadPriority::Low) < static_cast<int>(sgc::LoadPriority::Normal));
	REQUIRE(static_cast<int>(sgc::LoadPriority::Normal) < static_cast<int>(sgc::LoadPriority::High));
	REQUIRE(static_cast<int>(sgc::LoadPriority::High) < static_cast<int>(sgc::LoadPriority::Critical));
}
