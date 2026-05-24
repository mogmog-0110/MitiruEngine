#include <catch2/catch_test_macros.hpp>
#include <sgc/save/SaveMigration.hpp>

using namespace sgc::save;

TEST_CASE("MigrationChain - same version returns unchanged data", "[save]")
{
	MigrationChain chain;
	SaveData data;
	data.set("x", 42);
	auto result = chain.migrate(data, 1, 1);
	REQUIRE(result.has_value());
	REQUIRE(result->getAs<int>("x").value() == 42);
}

TEST_CASE("MigrationChain - single step migration", "[save]")
{
	MigrationChain chain;
	chain.addMigration(1, 2, [](SaveData data) {
		// v1->v2: "hp" int を float に変換
		if (auto hp = data.getAs<int>("hp"))
		{
			data.set("hp", static_cast<float>(*hp));
		}
		data.setVersion(2);
		return data;
	});

	SaveData data;
	data.set("hp", 100);
	data.setVersion(1);

	auto result = chain.migrate(data, 1, 2);
	REQUIRE(result.has_value());
	REQUIRE(result->getAs<float>("hp").value() == 100.0f);
	REQUIRE(result->version() == 2);
}

TEST_CASE("MigrationChain - multi step migration", "[save]")
{
	MigrationChain chain;
	chain.addMigration(1, 2, [](SaveData data) {
		data.set("newField", std::string("added"));
		data.setVersion(2);
		return data;
	});
	chain.addMigration(2, 3, [](SaveData data) {
		data.remove("oldField");
		data.setVersion(3);
		return data;
	});

	SaveData data;
	data.set("oldField", 1);
	data.setVersion(1);

	auto result = chain.migrate(data, 1, 3);
	REQUIRE(result.has_value());
	REQUIRE(result->has("newField"));
	REQUIRE_FALSE(result->has("oldField"));
	REQUIRE(result->version() == 3);
}

TEST_CASE("MigrationChain - missing migration path returns nullopt", "[save]")
{
	MigrationChain chain;
	chain.addMigration(1, 2, [](SaveData data) { return data; });
	// 2->3 is missing

	SaveData data;
	auto result = chain.migrate(data, 1, 3);
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("MigrationChain - downgrade returns nullopt", "[save]")
{
	MigrationChain chain;
	SaveData data;
	auto result = chain.migrate(data, 3, 1);
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("MigrationChain - hasMigration and migrationCount", "[save]")
{
	MigrationChain chain;
	REQUIRE(chain.migrationCount() == 0);
	chain.addMigration(1, 2, [](SaveData d) { return d; });
	chain.addMigration(2, 3, [](SaveData d) { return d; });
	REQUIRE(chain.migrationCount() == 2);
	REQUIRE(chain.hasMigration(1));
	REQUIRE(chain.hasMigration(2));
	REQUIRE_FALSE(chain.hasMigration(3));
}

TEST_CASE("MigrationChain - maxReachableVersion", "[save]")
{
	MigrationChain chain;
	chain.addMigration(1, 2, [](SaveData d) { return d; });
	chain.addMigration(2, 3, [](SaveData d) { return d; });
	chain.addMigration(3, 4, [](SaveData d) { return d; });
	REQUIRE(chain.maxReachableVersion(1) == 4);
	REQUIRE(chain.maxReachableVersion(2) == 4);
	REQUIRE(chain.maxReachableVersion(4) == 4); // no migration from 4
}
