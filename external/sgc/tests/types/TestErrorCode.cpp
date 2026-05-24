#include <catch2/catch_test_macros.hpp>

#include "sgc/types/ErrorCode.hpp"

using namespace sgc;

TEST_CASE("ErrorCode - enum values are distinct", "[types][errorcode]")
{
	REQUIRE(ErrorCode::None != ErrorCode::ParseError);
	REQUIRE(ErrorCode::ParseError != ErrorCode::FileNotFound);
	REQUIRE(ErrorCode::FileNotFound != ErrorCode::InvalidArgument);
	REQUIRE(ErrorCode::InvalidArgument != ErrorCode::OutOfRange);
	REQUIRE(ErrorCode::OutOfRange != ErrorCode::IOError);
	REQUIRE(ErrorCode::IOError != ErrorCode::Timeout);
	REQUIRE(ErrorCode::Timeout != ErrorCode::NotSupported);
	REQUIRE(ErrorCode::NotSupported != ErrorCode::AlreadyExists);
}

TEST_CASE("ErrorCode - ErrorInfo equality compares by code", "[types][errorcode]")
{
	const ErrorInfo a{ErrorCode::ParseError, "message A"};
	const ErrorInfo b{ErrorCode::ParseError, "message B"};
	const ErrorInfo c{ErrorCode::IOError, "message A"};

	REQUIRE(a == b);       // 同コード・異メッセージ → 等価
	REQUIRE_FALSE(a == c); // 異コード・同メッセージ → 非等価
}

TEST_CASE("ErrorCode - makeError creates correct ErrorInfo", "[types][errorcode]")
{
	const auto info = makeError(ErrorCode::Timeout, "timed out");
	REQUIRE(info.code == ErrorCode::Timeout);
	REQUIRE(info.message == "timed out");
}

TEST_CASE("ErrorCode - parseError sets ParseError code", "[types][errorcode]")
{
	const auto info = parseError("syntax error at line 5");
	REQUIRE(info.code == ErrorCode::ParseError);
	REQUIRE(info.message == "syntax error at line 5");
}

TEST_CASE("ErrorCode - fileNotFound sets FileNotFound code", "[types][errorcode]")
{
	const auto info = fileNotFound("config.json");
	REQUIRE(info.code == ErrorCode::FileNotFound);
	REQUIRE(info.message == "config.json");
}

TEST_CASE("ErrorCode - IOResult success case", "[types][errorcode]")
{
	IOResult<int> result{42};
	REQUIRE(result.hasValue());
	REQUIRE(result.value() == 42);
}

TEST_CASE("ErrorCode - IOResult error case", "[types][errorcode]")
{
	IOResult<int> result{ERROR_TAG, parseError("bad input")};
	REQUIRE(result.hasError());
	REQUIRE(result.error().code == ErrorCode::ParseError);
	REQUIRE(result.error().message == "bad input");
}

TEST_CASE("ErrorCode - IOResult map operation", "[types][errorcode]")
{
	IOResult<int> result{10};
	auto mapped = result.map([](int x) { return x * 3; });
	REQUIRE(mapped.hasValue());
	REQUIRE(mapped.value() == 30);

	// エラー時はmapが伝播する
	IOResult<int> err{ERROR_TAG, makeError(ErrorCode::IOError, "fail")};
	auto mapped2 = err.map([](int x) { return x * 3; });
	REQUIRE(mapped2.hasError());
	REQUIRE(mapped2.error().code == ErrorCode::IOError);
}

TEST_CASE("ErrorCode - IOResult andThen operation", "[types][errorcode]")
{
	auto doubleIfPositive = [](int x) -> IOResult<int>
	{
		if (x > 0)
		{
			return x * 2;
		}
		return {ERROR_TAG, makeError(ErrorCode::InvalidArgument, "not positive")};
	};

	IOResult<int> ok{5};
	auto result = ok.andThen(doubleIfPositive);
	REQUIRE(result.hasValue());
	REQUIRE(result.value() == 10);

	IOResult<int> negative{-1};
	auto result2 = negative.andThen(doubleIfPositive);
	REQUIRE(result2.hasError());
	REQUIRE(result2.error().code == ErrorCode::InvalidArgument);

	// エラー時はandThenが伝播する
	IOResult<int> err{ERROR_TAG, makeError(ErrorCode::IOError, "io fail")};
	auto result3 = err.andThen(doubleIfPositive);
	REQUIRE(result3.hasError());
	REQUIRE(result3.error().code == ErrorCode::IOError);
}
