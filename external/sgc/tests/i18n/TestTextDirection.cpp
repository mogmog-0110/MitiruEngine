#include <catch2/catch_test_macros.hpp>

#include "sgc/i18n/TextDirection.hpp"

TEST_CASE("TextDirectionResolver English is LTR", "[i18n]")
{
	REQUIRE(sgc::i18n::TextDirectionResolver::resolve("en") ==
		sgc::i18n::TextDirection::LeftToRight);
}

TEST_CASE("TextDirectionResolver Japanese is LTR", "[i18n]")
{
	REQUIRE(sgc::i18n::TextDirectionResolver::resolve("ja") ==
		sgc::i18n::TextDirection::LeftToRight);
}

TEST_CASE("TextDirectionResolver Arabic is RTL", "[i18n]")
{
	REQUIRE(sgc::i18n::TextDirectionResolver::resolve("ar") ==
		sgc::i18n::TextDirection::RightToLeft);
}

TEST_CASE("TextDirectionResolver Hebrew is RTL", "[i18n]")
{
	REQUIRE(sgc::i18n::TextDirectionResolver::resolve("he") ==
		sgc::i18n::TextDirection::RightToLeft);
}

TEST_CASE("TextDirectionResolver isRtl helper", "[i18n]")
{
	REQUIRE(sgc::i18n::TextDirectionResolver::isRtl("ar") == true);
	REQUIRE(sgc::i18n::TextDirectionResolver::isRtl("fa") == true);
	REQUIRE(sgc::i18n::TextDirectionResolver::isRtl("en") == false);
	REQUIRE(sgc::i18n::TextDirectionResolver::isRtl("ja") == false);
}

TEST_CASE("charDirection detects Arabic as RTL", "[i18n]")
{
	// Arabic letter Alef: U+0627
	REQUIRE(sgc::i18n::charDirection(0x0627) ==
		sgc::i18n::TextDirection::RightToLeft);
}

TEST_CASE("charDirection detects Latin as LTR", "[i18n]")
{
	REQUIRE(sgc::i18n::charDirection(U'A') ==
		sgc::i18n::TextDirection::LeftToRight);
}

TEST_CASE("splitBiDi empty text returns empty", "[i18n]")
{
	auto segments = sgc::i18n::splitBiDi("");
	REQUIRE(segments.empty());
}

TEST_CASE("splitBiDi pure LTR text returns one segment", "[i18n]")
{
	auto segments = sgc::i18n::splitBiDi("Hello World");
	REQUIRE(segments.size() == 1);
	REQUIRE(segments[0].text == "Hello World");
	REQUIRE(segments[0].direction == sgc::i18n::TextDirection::LeftToRight);
}

TEST_CASE("decodeUtf8 handles ASCII", "[i18n]")
{
	std::string_view text = "ABC";
	std::size_t pos = 0;
	REQUIRE(sgc::i18n::decodeUtf8(text, pos) == U'A');
	REQUIRE(pos == 1);
	REQUIRE(sgc::i18n::decodeUtf8(text, pos) == U'B');
	REQUIRE(pos == 2);
}

TEST_CASE("decodeUtf8 handles multibyte", "[i18n]")
{
	// UTF-8 encoding of U+0627 (Arabic Alef): 0xD8 0xA7
	std::string text = "\xD8\xA7";
	std::size_t pos = 0;
	REQUIRE(sgc::i18n::decodeUtf8(text, pos) == 0x0627);
	REQUIRE(pos == 2);
}
