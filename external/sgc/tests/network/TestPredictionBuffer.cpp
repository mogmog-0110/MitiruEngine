#include <catch2/catch_test_macros.hpp>
#include "sgc/network/PredictionBuffer.hpp"

using namespace sgc::network;

/// @brief テスト用の簡易状態型
struct TestState
{
	float x = 0.0f;
	float y = 0.0f;

	bool operator==(const TestState& other) const
	{
		return x == other.x && y == other.y;
	}
};

/// @brief テスト用の簡易入力型
struct TestInput
{
	float dx = 0.0f;
	float dy = 0.0f;
};

/// @brief テスト用のシミュレーション関数
static TestState simulate(const TestState& state, const TestInput& input)
{
	return TestState{state.x + input.dx, state.y + input.dy};
}

TEST_CASE("PredictionBuffer records input and state", "[network][prediction]")
{
	PredictionBuffer<TestState, TestInput> buffer;

	buffer.recordInput(1, TestInput{1.0f, 0.0f}, TestState{1.0f, 0.0f});
	buffer.recordInput(2, TestInput{1.0f, 0.0f}, TestState{2.0f, 0.0f});

	REQUIRE(buffer.pendingCount() == 2);
	REQUIRE(buffer.latestState().x == 2.0f);
}

TEST_CASE("PredictionBuffer reconciles with server state", "[network][prediction]")
{
	PredictionBuffer<TestState, TestInput> buffer;

	buffer.recordInput(1, TestInput{1.0f, 0.0f}, TestState{1.0f, 0.0f});
	buffer.recordInput(2, TestInput{1.0f, 0.0f}, TestState{2.0f, 0.0f});
	buffer.recordInput(3, TestInput{1.0f, 0.0f}, TestState{3.0f, 0.0f});

	// サーバーがティック1を確認、位置は0.5（予測とずれている）
	auto resimCount = buffer.reconcile(1, TestState{0.5f, 0.0f}, simulate);

	// ティック2,3が再シミュレーションされる
	REQUIRE(resimCount == 2);
	REQUIRE(buffer.pendingCount() == 2);
	// 0.5 + 1.0 + 1.0 = 2.5
	REQUIRE(buffer.latestState().x == 2.5f);
}

TEST_CASE("PredictionBuffer reports pending count correctly", "[network][prediction]")
{
	PredictionBuffer<TestState, TestInput> buffer;

	REQUIRE(buffer.pendingCount() == 0);

	buffer.recordInput(1, TestInput{}, TestState{});
	REQUIRE(buffer.pendingCount() == 1);

	buffer.recordInput(2, TestInput{}, TestState{});
	buffer.recordInput(3, TestInput{}, TestState{});
	REQUIRE(buffer.pendingCount() == 3);

	buffer.reconcile(2, TestState{}, simulate);
	REQUIRE(buffer.pendingCount() == 1);
}

TEST_CASE("PredictionBuffer clears buffer", "[network][prediction]")
{
	PredictionBuffer<TestState, TestInput> buffer;

	buffer.recordInput(1, TestInput{1.0f, 2.0f}, TestState{1.0f, 2.0f});
	buffer.recordInput(2, TestInput{1.0f, 2.0f}, TestState{2.0f, 4.0f});

	buffer.clear();
	REQUIRE(buffer.pendingCount() == 0);
	REQUIRE(buffer.latestState() == TestState{});
}

TEST_CASE("PredictionBuffer respects max size", "[network][prediction]")
{
	PredictionBuffer<TestState, TestInput> buffer(3);

	buffer.recordInput(1, TestInput{}, TestState{1.0f, 0.0f});
	buffer.recordInput(2, TestInput{}, TestState{2.0f, 0.0f});
	buffer.recordInput(3, TestInput{}, TestState{3.0f, 0.0f});
	REQUIRE(buffer.pendingCount() == 3);

	// 4つ目を追加すると最古が削除される
	buffer.recordInput(4, TestInput{}, TestState{4.0f, 0.0f});
	REQUIRE(buffer.pendingCount() == 3);
}
