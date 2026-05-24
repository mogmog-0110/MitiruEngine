#include <catch2/catch_test_macros.hpp>

#include "sgc/i18n/PluralRules.hpp"

TEST_CASE("English plural: 1 is One", "[i18n]")
{
	sgc::i18n::EnglishPluralRule rule;
	REQUIRE(rule.resolve(1) == sgc::i18n::PluralCategory::One);
}

TEST_CASE("English plural: 0 is Other", "[i18n]")
{
	sgc::i18n::EnglishPluralRule rule;
	REQUIRE(rule.resolve(0) == sgc::i18n::PluralCategory::Other);
}

TEST_CASE("English plural: multiple values are Other", "[i18n]")
{
	sgc::i18n::EnglishPluralRule rule;
	REQUIRE(rule.resolve(2) == sgc::i18n::PluralCategory::Other);
	REQUIRE(rule.resolve(5) == sgc::i18n::PluralCategory::Other);
	REQUIRE(rule.resolve(100) == sgc::i18n::PluralCategory::Other);
}

TEST_CASE("English plural: negative 1 is One", "[i18n]")
{
	sgc::i18n::EnglishPluralRule rule;
	REQUIRE(rule.resolve(-1) == sgc::i18n::PluralCategory::One);
}

TEST_CASE("Japanese plural: always Other", "[i18n]")
{
	sgc::i18n::JapanesePluralRule rule;
	REQUIRE(rule.resolve(0) == sgc::i18n::PluralCategory::Other);
	REQUIRE(rule.resolve(1) == sgc::i18n::PluralCategory::Other);
	REQUIRE(rule.resolve(5) == sgc::i18n::PluralCategory::Other);
	REQUIRE(rule.resolve(100) == sgc::i18n::PluralCategory::Other);
}

TEST_CASE("Russian plural: 1,21,31 are One", "[i18n]")
{
	sgc::i18n::RussianPluralRule rule;
	REQUIRE(rule.resolve(1) == sgc::i18n::PluralCategory::One);
	REQUIRE(rule.resolve(21) == sgc::i18n::PluralCategory::One);
	REQUIRE(rule.resolve(31) == sgc::i18n::PluralCategory::One);
	REQUIRE(rule.resolve(101) == sgc::i18n::PluralCategory::One);
}

TEST_CASE("Russian plural: 11 is Many (not One)", "[i18n]")
{
	sgc::i18n::RussianPluralRule rule;
	REQUIRE(rule.resolve(11) == sgc::i18n::PluralCategory::Many);
	REQUIRE(rule.resolve(111) == sgc::i18n::PluralCategory::Many);
	REQUIRE(rule.resolve(211) == sgc::i18n::PluralCategory::Many);
}

TEST_CASE("Russian plural: 2-4, 22-24 are Few", "[i18n]")
{
	sgc::i18n::RussianPluralRule rule;
	REQUIRE(rule.resolve(2) == sgc::i18n::PluralCategory::Few);
	REQUIRE(rule.resolve(3) == sgc::i18n::PluralCategory::Few);
	REQUIRE(rule.resolve(4) == sgc::i18n::PluralCategory::Few);
	REQUIRE(rule.resolve(22) == sgc::i18n::PluralCategory::Few);
	REQUIRE(rule.resolve(24) == sgc::i18n::PluralCategory::Few);
}

TEST_CASE("Russian plural: 12-14 are Many (not Few)", "[i18n]")
{
	sgc::i18n::RussianPluralRule rule;
	REQUIRE(rule.resolve(12) == sgc::i18n::PluralCategory::Many);
	REQUIRE(rule.resolve(13) == sgc::i18n::PluralCategory::Many);
	REQUIRE(rule.resolve(14) == sgc::i18n::PluralCategory::Many);
}

TEST_CASE("Russian plural: 0,5-9 are Many", "[i18n]")
{
	sgc::i18n::RussianPluralRule rule;
	REQUIRE(rule.resolve(0) == sgc::i18n::PluralCategory::Many);
	REQUIRE(rule.resolve(5) == sgc::i18n::PluralCategory::Many);
	REQUIRE(rule.resolve(9) == sgc::i18n::PluralCategory::Many);
	REQUIRE(rule.resolve(10) == sgc::i18n::PluralCategory::Many);
}

TEST_CASE("resolvePlural template function works", "[i18n]")
{
	sgc::i18n::EnglishPluralRule english;
	sgc::i18n::JapanesePluralRule japanese;

	REQUIRE(sgc::i18n::resolvePlural(1, english) == sgc::i18n::PluralCategory::One);
	REQUIRE(sgc::i18n::resolvePlural(1, japanese) == sgc::i18n::PluralCategory::Other);
}

TEST_CASE("PluralRule concept is satisfied", "[i18n]")
{
	STATIC_REQUIRE(sgc::i18n::PluralRule<sgc::i18n::EnglishPluralRule>);
	STATIC_REQUIRE(sgc::i18n::PluralRule<sgc::i18n::JapanesePluralRule>);
	STATIC_REQUIRE(sgc::i18n::PluralRule<sgc::i18n::RussianPluralRule>);
}
