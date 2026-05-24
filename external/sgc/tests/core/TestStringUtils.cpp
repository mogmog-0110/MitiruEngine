#include <catch2/catch_test_macros.hpp>

#include <string>

#include "sgc/core/StringUtils.hpp"

// string_viewをstd::stringに変換するヘルパー（Catch2のStringMaker対策）
static std::string s(std::string_view sv) { return std::string(sv); }

TEST_CASE("trimLeft removes leading whitespace", "[core][stringutils]")
{
	REQUIRE(s(sgc::trimLeft("  hello")) == "hello");
	REQUIRE(s(sgc::trimLeft("\t\nhello")) == "hello");
	REQUIRE(s(sgc::trimLeft("hello")) == "hello");
	REQUIRE(s(sgc::trimLeft("")) == "");
	REQUIRE(s(sgc::trimLeft("   ")) == "");
}

TEST_CASE("trimRight removes trailing whitespace", "[core][stringutils]")
{
	REQUIRE(s(sgc::trimRight("hello  ")) == "hello");
	REQUIRE(s(sgc::trimRight("hello\t\n")) == "hello");
	REQUIRE(s(sgc::trimRight("hello")) == "hello");
	REQUIRE(s(sgc::trimRight("")) == "");
}

TEST_CASE("trim removes both sides whitespace", "[core][stringutils]")
{
	REQUIRE(s(sgc::trim("  hello  ")) == "hello");
	REQUIRE(s(sgc::trim("\t hello \n")) == "hello");
	REQUIRE(s(sgc::trim("hello")) == "hello");
	REQUIRE(s(sgc::trim("")) == "");
	REQUIRE(s(sgc::trim("   ")) == "");
}

TEST_CASE("split by char delimiter", "[core][stringutils]")
{
	auto parts = sgc::split("a,b,c", ',');
	REQUIRE(parts.size() == 3);
	REQUIRE(s(parts[0]) == "a");
	REQUIRE(s(parts[1]) == "b");
	REQUIRE(s(parts[2]) == "c");
}

TEST_CASE("split handles empty segments", "[core][stringutils]")
{
	auto parts = sgc::split(",a,,b,", ',');
	REQUIRE(parts.size() == 5);
	REQUIRE(s(parts[0]) == "");
	REQUIRE(s(parts[1]) == "a");
	REQUIRE(s(parts[2]) == "");
	REQUIRE(s(parts[3]) == "b");
	REQUIRE(s(parts[4]) == "");
}

TEST_CASE("split by string delimiter", "[core][stringutils]")
{
	auto parts = sgc::split("a::b::c", std::string_view("::"));
	REQUIRE(parts.size() == 3);
	REQUIRE(s(parts[0]) == "a");
	REQUIRE(s(parts[1]) == "b");
	REQUIRE(s(parts[2]) == "c");
}

TEST_CASE("split empty string", "[core][stringutils]")
{
	auto parts = sgc::split("", ',');
	REQUIRE(parts.size() == 1);
	REQUIRE(s(parts[0]) == "");
}

TEST_CASE("join combines parts with separator", "[core][stringutils]")
{
	std::vector<std::string_view> parts = {"a", "b", "c"};
	REQUIRE(sgc::join(parts, "-") == "a-b-c");
	REQUIRE(sgc::join(parts, "::") == "a::b::c");
	REQUIRE(sgc::join(parts, "") == "abc");
}

TEST_CASE("join with empty parts", "[core][stringutils]")
{
	std::vector<std::string_view> empty;
	REQUIRE(sgc::join(empty, ",") == "");

	std::vector<std::string_view> single = {"hello"};
	REQUIRE(sgc::join(single, ",") == "hello");
}

TEST_CASE("toLower converts ASCII uppercase", "[core][stringutils]")
{
	REQUIRE(sgc::toLower("HELLO") == "hello");
	REQUIRE(sgc::toLower("Hello World") == "hello world");
	REQUIRE(sgc::toLower("already") == "already");
	REQUIRE(sgc::toLower("") == "");
}

TEST_CASE("toUpper converts ASCII lowercase", "[core][stringutils]")
{
	REQUIRE(sgc::toUpper("hello") == "HELLO");
	REQUIRE(sgc::toUpper("Hello World") == "HELLO WORLD");
	REQUIRE(sgc::toUpper("ALREADY") == "ALREADY");
	REQUIRE(sgc::toUpper("") == "");
}

TEST_CASE("startsWith checks prefix", "[core][stringutils]")
{
	REQUIRE(sgc::startsWith("hello world", "hello"));
	REQUIRE(sgc::startsWith("hello", "hello"));
	REQUIRE_FALSE(sgc::startsWith("hello", "world"));
	REQUIRE(sgc::startsWith("hello", ""));
}

TEST_CASE("endsWith checks suffix", "[core][stringutils]")
{
	REQUIRE(sgc::endsWith("hello world", "world"));
	REQUIRE(sgc::endsWith("hello", "hello"));
	REQUIRE_FALSE(sgc::endsWith("hello", "world"));
	REQUIRE(sgc::endsWith("hello", ""));
}

TEST_CASE("contains checks substring", "[core][stringutils]")
{
	REQUIRE(sgc::contains("hello world", "lo wo"));
	REQUIRE(sgc::contains("hello", "hello"));
	REQUIRE_FALSE(sgc::contains("hello", "xyz"));
	REQUIRE(sgc::contains("hello", ""));
}

TEST_CASE("replace replaces first occurrence", "[core][stringutils]")
{
	REQUIRE(sgc::replace("aabaa", "a", "x") == "xabaa");
	REQUIRE(sgc::replace("hello", "xyz", "abc") == "hello");
	REQUIRE(sgc::replace("", "a", "b") == "");
}

TEST_CASE("replaceAll replaces all occurrences", "[core][stringutils]")
{
	REQUIRE(sgc::replaceAll("aabaa", "a", "x") == "xxbxx");
	REQUIRE(sgc::replaceAll("hello world", "o", "0") == "hell0 w0rld");
	REQUIRE(sgc::replaceAll("aaa", "a", "bb") == "bbbbbb");
	REQUIRE(sgc::replaceAll("hello", "", "x") == "hello");
}
