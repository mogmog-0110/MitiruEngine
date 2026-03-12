#include <catch2/catch_test_macros.hpp>

#include "mitiru/scene/SystemRunner.hpp"
#include "mitiru/scene/GameWorld.hpp"

using namespace mitiru::scene;

/// @brief テスト用の単純なシステム
class CounterSystem : public ISystem
{
public:
	explicit CounterSystem(const std::string& name, int priority = 0)
		: m_name(name), m_priority(priority) {}

	std::string name() const override { return m_name; }

	void update(GameWorld&, float dt) override
	{
		m_totalDt += dt;
		++m_updateCount;
	}

	int updateCount() const { return m_updateCount; }
	float totalDt() const { return m_totalDt; }

private:
	std::string m_name;
	int m_priority;
	int m_updateCount{0};
	float m_totalDt{0.0f};
};

/// @brief 実行順序を記録するシステム
class OrderTracker : public ISystem
{
public:
	OrderTracker(const std::string& name, std::vector<std::string>& log)
		: m_name(name), m_log(log) {}

	std::string name() const override { return m_name; }

	void update(GameWorld&, float) override
	{
		m_log.push_back(m_name);
	}

private:
	std::string m_name;
	std::vector<std::string>& m_log;
};

TEST_CASE("SystemRunner - add and update systems", "[mitiru][scene]")
{
	SystemRunner runner;
	GameWorld world;

	auto sys = std::make_unique<CounterSystem>("Test");
	auto* raw = sys.get();
	runner.addSystem(std::move(sys));

	REQUIRE(runner.systemCount() == 1);

	runner.updateAll(world, 0.016f);
	REQUIRE(raw->updateCount() == 1);

	runner.updateAll(world, 0.016f);
	REQUIRE(raw->updateCount() == 2);
}

TEST_CASE("SystemRunner - priority ordering", "[mitiru][scene]")
{
	SystemRunner runner;
	GameWorld world;

	std::vector<std::string> log;

	runner.addSystem(std::make_unique<OrderTracker>("C", log), 30);
	runner.addSystem(std::make_unique<OrderTracker>("A", log), 10);
	runner.addSystem(std::make_unique<OrderTracker>("B", log), 20);

	runner.updateAll(world, 0.0f);

	REQUIRE(log.size() == 3);
	REQUIRE(log[0] == "A");
	REQUIRE(log[1] == "B");
	REQUIRE(log[2] == "C");
}

TEST_CASE("SystemRunner - remove system by name", "[mitiru][scene]")
{
	SystemRunner runner;
	GameWorld world;

	runner.addSystem(std::make_unique<CounterSystem>("ToRemove"));
	runner.addSystem(std::make_unique<CounterSystem>("ToKeep"));

	REQUIRE(runner.removeSystem("ToRemove"));
	REQUIRE(runner.systemCount() == 1);

	REQUIRE_FALSE(runner.removeSystem("Nonexistent"));
}

TEST_CASE("SystemRunner - enable and disable", "[mitiru][scene]")
{
	SystemRunner runner;
	GameWorld world;

	auto sys = std::make_unique<CounterSystem>("Sys");
	auto* raw = sys.get();
	runner.addSystem(std::move(sys));

	REQUIRE(runner.isEnabled("Sys"));

	runner.setEnabled("Sys", false);
	REQUIRE_FALSE(runner.isEnabled("Sys"));

	runner.updateAll(world, 0.016f);
	REQUIRE(raw->updateCount() == 0);

	runner.setEnabled("Sys", true);
	runner.updateAll(world, 0.016f);
	REQUIRE(raw->updateCount() == 1);
}

TEST_CASE("SystemRunner - setEnabled returns false for missing", "[mitiru][scene]")
{
	SystemRunner runner;
	REQUIRE_FALSE(runner.setEnabled("missing", true));
	REQUIRE_FALSE(runner.isEnabled("missing"));
}

TEST_CASE("SystemRunner - profiles report timing", "[mitiru][scene]")
{
	SystemRunner runner;
	GameWorld world;

	runner.addSystem(std::make_unique<CounterSystem>("SysA"), 0);
	runner.addSystem(std::make_unique<CounterSystem>("SysB"), 1);

	runner.updateAll(world, 0.016f);

	auto profiles = runner.profiles();
	REQUIRE(profiles.size() == 2);
	REQUIRE(profiles[0].name == "SysA");
	REQUIRE(profiles[1].name == "SysB");
	// タイミングは0以上であること
	REQUIRE(profiles[0].lastUpdateMs >= 0.0);
	REQUIRE(profiles[1].lastUpdateMs >= 0.0);
}

TEST_CASE("SystemRunner - disabled systems have zero timing", "[mitiru][scene]")
{
	SystemRunner runner;
	GameWorld world;

	runner.addSystem(std::make_unique<CounterSystem>("Disabled"), 0);
	runner.setEnabled("Disabled", false);

	runner.updateAll(world, 0.016f);

	auto profiles = runner.profiles();
	REQUIRE(profiles.size() == 1);
	REQUIRE(profiles[0].lastUpdateMs == 0.0);
	REQUIRE_FALSE(profiles[0].enabled);
}

TEST_CASE("SystemRunner - addSystem ignores null", "[mitiru][scene]")
{
	SystemRunner runner;

	runner.addSystem(nullptr);
	REQUIRE(runner.systemCount() == 0);
}

TEST_CASE("SystemRunner - stable sort preserves insertion order for same priority", "[mitiru][scene]")
{
	SystemRunner runner;
	GameWorld world;

	std::vector<std::string> log;

	runner.addSystem(std::make_unique<OrderTracker>("First", log), 0);
	runner.addSystem(std::make_unique<OrderTracker>("Second", log), 0);
	runner.addSystem(std::make_unique<OrderTracker>("Third", log), 0);

	runner.updateAll(world, 0.0f);

	REQUIRE(log[0] == "First");
	REQUIRE(log[1] == "Second");
	REQUIRE(log[2] == "Third");
}
