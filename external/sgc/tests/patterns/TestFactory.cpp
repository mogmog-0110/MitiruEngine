/// @file TestFactory.cpp
/// @brief Factory.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "sgc/patterns/Factory.hpp"

namespace
{

struct Animal
{
	virtual ~Animal() = default;
	[[nodiscard]] virtual std::string speak() const = 0;
};

struct Dog : Animal
{
	[[nodiscard]] std::string speak() const override { return "Woof"; }
};

struct Cat : Animal
{
	[[nodiscard]] std::string speak() const override { return "Meow"; }
};

struct Bird : Animal
{
	[[nodiscard]] std::string speak() const override { return "Tweet"; }
};

} // namespace

TEST_CASE("Factory register and create", "[patterns][factory]")
{
	sgc::Factory<Animal> factory;
	factory.registerType("dog", []() { return std::make_unique<Dog>(); });
	factory.registerType("cat", []() { return std::make_unique<Cat>(); });

	auto dog = factory.create("dog");
	REQUIRE(dog->speak() == "Woof");

	auto cat = factory.create("cat");
	REQUIRE(cat->speak() == "Meow");
}

TEST_CASE("Factory isRegistered", "[patterns][factory]")
{
	sgc::Factory<Animal> factory;
	REQUIRE_FALSE(factory.isRegistered("dog"));

	factory.registerType("dog", []() { return std::make_unique<Dog>(); });
	REQUIRE(factory.isRegistered("dog"));
	REQUIRE_FALSE(factory.isRegistered("cat"));
}

TEST_CASE("Factory throws on duplicate key", "[patterns][factory]")
{
	sgc::Factory<Animal> factory;
	factory.registerType("dog", []() { return std::make_unique<Dog>(); });

	REQUIRE_THROWS_AS(
		factory.registerType("dog", []() { return std::make_unique<Dog>(); }),
		std::runtime_error);
}

TEST_CASE("Factory throws on unknown key", "[patterns][factory]")
{
	sgc::Factory<Animal> factory;
	REQUIRE_THROWS_AS(factory.create("unknown"), std::runtime_error);
}

TEST_CASE("Factory registeredKeys", "[patterns][factory]")
{
	sgc::Factory<Animal> factory;
	factory.registerType("dog", []() { return std::make_unique<Dog>(); });
	factory.registerType("cat", []() { return std::make_unique<Cat>(); });
	factory.registerType("bird", []() { return std::make_unique<Bird>(); });

	const auto keys = factory.registeredKeys();
	REQUIRE(keys.size() == 3);
}

TEST_CASE("Factory size and clear", "[patterns][factory]")
{
	sgc::Factory<Animal> factory;
	REQUIRE(factory.size() == 0);

	factory.registerType("dog", []() { return std::make_unique<Dog>(); });
	factory.registerType("cat", []() { return std::make_unique<Cat>(); });
	REQUIRE(factory.size() == 2);

	factory.clear();
	REQUIRE(factory.size() == 0);
	REQUIRE_FALSE(factory.isRegistered("dog"));
}

TEST_CASE("Factory with constructor arguments", "[patterns][factory]")
{
	struct Enemy
	{
		virtual ~Enemy() = default;
		int hp = 0;
	};

	struct Goblin : Enemy
	{
		explicit Goblin(int baseHp) { hp = baseHp; }
	};

	struct Dragon : Enemy
	{
		explicit Dragon(int baseHp) { hp = baseHp * 10; }
	};

	sgc::Factory<Enemy, std::string, int> factory;
	factory.registerType("goblin", [](int hp) { return std::make_unique<Goblin>(hp); });
	factory.registerType("dragon", [](int hp) { return std::make_unique<Dragon>(hp); });

	auto goblin = factory.create("goblin", 10);
	REQUIRE(goblin->hp == 10);

	auto dragon = factory.create("dragon", 10);
	REQUIRE(dragon->hp == 100);
}

TEST_CASE("Factory with int key", "[patterns][factory]")
{
	sgc::Factory<Animal, int> factory;
	factory.registerType(1, []() { return std::make_unique<Dog>(); });
	factory.registerType(2, []() { return std::make_unique<Cat>(); });

	auto dog = factory.create(1);
	REQUIRE(dog->speak() == "Woof");

	auto cat = factory.create(2);
	REQUIRE(cat->speak() == "Meow");
}
