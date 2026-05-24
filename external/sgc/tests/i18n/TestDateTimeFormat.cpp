#include <catch2/catch_test_macros.hpp>

#include "sgc/i18n/DateTimeFormat.hpp"

TEST_CASE("DateTimeFormatter YYYY-MM-DD format", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 3, 12, 0, 0, 0};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "YYYY-MM-DD");
	REQUIRE(result == "2026-03-12");
}

TEST_CASE("DateTimeFormatter HH:mm:ss format", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 1, 1, 14, 5, 9};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "HH:mm:ss");
	REQUIRE(result == "14:05:09");
}

TEST_CASE("DateTimeFormatter full datetime pattern", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 12, 25, 23, 59, 59};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "YYYY/MM/DD HH:mm:ss");
	REQUIRE(result == "2026/12/25 23:59:59");
}

TEST_CASE("DateTimeFormatter YY short year", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 1, 1, 0, 0, 0};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "YY-MM-DD");
	REQUIRE(result == "26-01-01");
}

TEST_CASE("DateTimeFormatter MONTH english name", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 3, 1, 0, 0, 0};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "DD MONTH YYYY", "en");
	REQUIRE(result == "01 March 2026");
}

TEST_CASE("DateTimeFormatter MONTH japanese name", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 3, 1, 0, 0, 0};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "YYYY MONTH DD", "ja");
	// "3月" in UTF-8
	REQUIRE(result == "2026 3\xE6\x9C\x88 01");
}

TEST_CASE("DateTimeFormatter plain text is preserved", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 1, 1, 0, 0, 0};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "Year: YYYY");
	REQUIRE(result == "Year: 2026");
}

TEST_CASE("DateTimeFormatter zero-padded single digits", "[i18n]")
{
	sgc::i18n::DateTime dt{2026, 1, 5, 3, 7, 2};
	const auto result = sgc::i18n::DateTimeFormatter::format(dt, "MM/DD HH:mm:ss");
	REQUIRE(result == "01/05 03:07:02");
}

TEST_CASE("englishMonthName returns correct names", "[i18n]")
{
	REQUIRE(std::string(sgc::i18n::DateTimeFormatter::englishMonthName(1)) == "January");
	REQUIRE(std::string(sgc::i18n::DateTimeFormatter::englishMonthName(6)) == "June");
	REQUIRE(std::string(sgc::i18n::DateTimeFormatter::englishMonthName(12)) == "December");
}

TEST_CASE("englishMonthName out of range returns Unknown", "[i18n]")
{
	REQUIRE(std::string(sgc::i18n::DateTimeFormatter::englishMonthName(0)) == "Unknown");
	REQUIRE(std::string(sgc::i18n::DateTimeFormatter::englishMonthName(13)) == "Unknown");
}
