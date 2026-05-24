/// @file TestResult.cpp
/// @brief Result.hpp のユニットテスト — 成功/エラー状態と変換操作を検証

#include <catch2/catch_test_macros.hpp>

#include "sgc/types/Result.hpp"

#include <string>

// ── 構築 ────────────────────────────────────────────────────────

TEST_CASE("Result can be constructed with a value", "[types][result]")
{
	sgc::Result<int> result{42};
	REQUIRE(result.hasValue());
	REQUIRE_FALSE(result.hasError());
	REQUIRE(result.value() == 42);
}

TEST_CASE("Result can be constructed with an error", "[types][result]")
{
	sgc::Result<int> result{sgc::ERROR_TAG, sgc::Error{"something failed"}};
	REQUIRE(result.hasError());
	REQUIRE_FALSE(result.hasValue());
	REQUIRE(result.error().message == "something failed");
}

// ── bool変換 ────────────────────────────────────────────────────

TEST_CASE("Result converts to bool", "[types][result]")
{
	sgc::Result<int> success{10};
	sgc::Result<int> failure{sgc::ERROR_TAG, sgc::Error{"fail"}};

	REQUIRE(static_cast<bool>(success));
	REQUIRE_FALSE(static_cast<bool>(failure));
}

// ── valueOr ─────────────────────────────────────────────────────

TEST_CASE("Result::valueOr returns value on success", "[types][result]")
{
	sgc::Result<int> result{42};
	REQUIRE(result.valueOr(0) == 42);
}

TEST_CASE("Result::valueOr returns default on error", "[types][result]")
{
	sgc::Result<int> result{sgc::ERROR_TAG, sgc::Error{"fail"}};
	REQUIRE(result.valueOr(99) == 99);
}

// ── map ─────────────────────────────────────────────────────────

TEST_CASE("Result::map transforms success value", "[types][result]")
{
	sgc::Result<int> result{10};
	auto doubled = result.map([](int x) { return x * 2; });
	REQUIRE(doubled.hasValue());
	REQUIRE(doubled.value() == 20);
}

TEST_CASE("Result::map propagates error", "[types][result]")
{
	sgc::Result<int> result{sgc::ERROR_TAG, sgc::Error{"fail"}};
	auto doubled = result.map([](int x) { return x * 2; });
	REQUIRE(doubled.hasError());
	REQUIRE(doubled.error().message == "fail");
}

// ── mapError ────────────────────────────────────────────────────

TEST_CASE("Result::mapError transforms error", "[types][result]")
{
	sgc::Result<int> result{sgc::ERROR_TAG, sgc::Error{"oops"}};
	auto mapped = result.mapError([](const sgc::Error& e)
	{
		return sgc::Error{"wrapped: " + e.message};
	});
	REQUIRE(mapped.hasError());
	REQUIRE(mapped.error().message == "wrapped: oops");
}

TEST_CASE("Result::mapError passes through success", "[types][result]")
{
	sgc::Result<int> result{42};
	auto mapped = result.mapError([](const sgc::Error& e)
	{
		return sgc::Error{"wrapped: " + e.message};
	});
	REQUIRE(mapped.hasValue());
	REQUIRE(mapped.value() == 42);
}

// ── andThen ─────────────────────────────────────────────────────

TEST_CASE("Result::andThen chains on success", "[types][result]")
{
	sgc::Result<int> result{10};
	auto chained = result.andThen([](int x) -> sgc::Result<std::string>
	{
		return std::to_string(x);
	});
	REQUIRE(chained.hasValue());
	REQUIRE(chained.value() == "10");
}

TEST_CASE("Result::andThen short-circuits on error", "[types][result]")
{
	sgc::Result<int> result{sgc::ERROR_TAG, sgc::Error{"fail"}};
	auto chained = result.andThen([](int x) -> sgc::Result<std::string>
	{
		return std::to_string(x);
	});
	REQUIRE(chained.hasError());
	REQUIRE(chained.error().message == "fail");
}

// ── orElse ──────────────────────────────────────────────────────

TEST_CASE("Result::orElse recovers from error", "[types][result]")
{
	sgc::Result<int> result{sgc::ERROR_TAG, sgc::Error{"fail"}};
	auto recovered = result.orElse([](const sgc::Error&) -> sgc::Result<int>
	{
		return 42;
	});
	REQUIRE(recovered.hasValue());
	REQUIRE(recovered.value() == 42);
}

TEST_CASE("Result::orElse passes through success", "[types][result]")
{
	sgc::Result<int> result{10};
	auto same = result.orElse([](const sgc::Error&) -> sgc::Result<int>
	{
		return 99;
	});
	REQUIRE(same.hasValue());
	REQUIRE(same.value() == 10);
}

TEST_CASE("Result::orElse can chain to a different error", "[types][result]")
{
	sgc::Result<int> result{sgc::ERROR_TAG, sgc::Error{"original"}};
	auto chained = result.orElse([](const sgc::Error& e) -> sgc::Result<int>
	{
		return {sgc::ERROR_TAG, sgc::Error{"wrapped: " + e.message}};
	});
	REQUIRE(chained.hasError());
	REQUIRE(chained.error().message == "wrapped: original");
}

// ── カスタムエラー型 ────────────────────────────────────────────

enum class ErrorCode { NotFound, InvalidInput };

TEST_CASE("Result works with custom error types", "[types][result]")
{
	sgc::Result<std::string, ErrorCode> ok{"hello"};
	REQUIRE(ok.hasValue());
	REQUIRE(ok.value() == "hello");

	sgc::Result<std::string, ErrorCode> err{sgc::ERROR_TAG, ErrorCode::NotFound};
	REQUIRE(err.hasError());
	REQUIRE(err.error() == ErrorCode::NotFound);
}

// ── ムーブセマンティクス ────────────────────────────────────────

TEST_CASE("Result supports move semantics", "[types][result]")
{
	sgc::Result<std::string> result{std::string("hello")};
	auto moved = std::move(result).value();
	REQUIRE(moved == "hello");
}
