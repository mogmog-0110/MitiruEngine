/// @file TestDecorator.cpp
/// @brief Decorator.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "sgc/patterns/Decorator.hpp"

namespace
{

struct IWeapon
{
	virtual ~IWeapon() = default;
	[[nodiscard]] virtual int damage() const = 0;
	[[nodiscard]] virtual std::string name() const = 0;
};

struct Sword : IWeapon
{
	[[nodiscard]] int damage() const override { return 10; }
	[[nodiscard]] std::string name() const override { return "Sword"; }
};

struct Dagger : IWeapon
{
	[[nodiscard]] int damage() const override { return 5; }
	[[nodiscard]] std::string name() const override { return "Dagger"; }
};

struct FireEnchant : sgc::Decorator<IWeapon>, IWeapon
{
	using sgc::Decorator<IWeapon>::Decorator;
	[[nodiscard]] int damage() const override { return wrapped().damage() + 5; }
	[[nodiscard]] std::string name() const override { return "Fire " + wrapped().name(); }
};

struct IceEnchant : sgc::Decorator<IWeapon>, IWeapon
{
	using sgc::Decorator<IWeapon>::Decorator;
	[[nodiscard]] int damage() const override { return wrapped().damage() + 3; }
	[[nodiscard]] std::string name() const override { return "Ice " + wrapped().name(); }
};

} // namespace

TEST_CASE("Decorator wraps and delegates", "[patterns][decorator]")
{
	auto sword = std::make_shared<Sword>();
	FireEnchant enchanted(sword);

	REQUIRE(enchanted.damage() == 15);
	REQUIRE(enchanted.name() == "Fire Sword");
}

TEST_CASE("Decorator chain (double wrap)", "[patterns][decorator]")
{
	auto sword = std::make_shared<Sword>();
	auto firePtr = std::make_shared<FireEnchant>(sword);
	IceEnchant doubleEnchant(firePtr);

	// Sword(10) + Fire(5) + Ice(3) = 18
	REQUIRE(doubleEnchant.damage() == 18);
	REQUIRE(doubleEnchant.name() == "Ice Fire Sword");
}

TEST_CASE("Decorator wrapped() returns reference", "[patterns][decorator]")
{
	auto dagger = std::make_shared<Dagger>();
	FireEnchant enchanted(dagger);

	REQUIRE(enchanted.wrapped().damage() == 5);
	REQUIRE(enchanted.wrapped().name() == "Dagger");
}

TEST_CASE("Decorator wrappedPtr returns shared_ptr", "[patterns][decorator]")
{
	auto sword = std::make_shared<Sword>();
	FireEnchant enchanted(sword);

	REQUIRE(enchanted.wrappedPtr() == sword);
	REQUIRE(enchanted.wrappedPtr().use_count() >= 2);  // sword + m_wrapped (+ potential copies)
}

TEST_CASE("Decorator throws on null wrapped", "[patterns][decorator]")
{
	std::shared_ptr<IWeapon> null = nullptr;
	REQUIRE_THROWS_AS(FireEnchant(null), std::invalid_argument);
}

TEST_CASE("Decorator const access", "[patterns][decorator]")
{
	auto sword = std::make_shared<Sword>();
	const FireEnchant enchanted(sword);

	REQUIRE(enchanted.damage() == 15);
	REQUIRE(enchanted.wrapped().damage() == 10);
}

TEST_CASE("Decorator with different base type", "[patterns][decorator]")
{
	struct ILogger
	{
		virtual ~ILogger() = default;
		[[nodiscard]] virtual std::string log(const std::string& msg) const = 0;
	};

	struct ConsoleLogger : ILogger
	{
		[[nodiscard]] std::string log(const std::string& msg) const override
		{
			return msg;
		}
	};

	struct TimestampDecorator : sgc::Decorator<ILogger>, ILogger
	{
		using sgc::Decorator<ILogger>::Decorator;
		[[nodiscard]] std::string log(const std::string& msg) const override
		{
			return "[TIME] " + wrapped().log(msg);
		}
	};

	auto logger = std::make_shared<ConsoleLogger>();
	TimestampDecorator decorated(logger);

	REQUIRE(decorated.log("hello") == "[TIME] hello");
}
