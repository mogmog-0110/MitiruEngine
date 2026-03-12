#include <catch2/catch_test_macros.hpp>

#include <mitiru/core/ServiceRegistry.hpp>

#include <memory>
#include <string>

// --- Test interfaces ---

/// @brief テスト用インターフェース
class ITestService
{
public:
	virtual ~ITestService() = default;
	[[nodiscard]] virtual int getValue() const = 0;
};

/// @brief テスト用実装
class TestServiceImpl : public ITestService
{
public:
	explicit TestServiceImpl(int val) : m_value(val) {}
	[[nodiscard]] int getValue() const override { return m_value; }
private:
	int m_value;
};

/// @brief 別のテスト用インターフェース
class IAnotherService
{
public:
	virtual ~IAnotherService() = default;
	[[nodiscard]] virtual std::string getName() const = 0;
};

class AnotherServiceImpl : public IAnotherService
{
public:
	explicit AnotherServiceImpl(std::string name) : m_name(std::move(name)) {}
	[[nodiscard]] std::string getName() const override { return m_name; }
private:
	std::string m_name;
};

// --- ServiceRegistry tests ---

TEST_CASE("ServiceRegistry initial state is empty", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	REQUIRE(registry.empty());
	REQUIRE(registry.size() == 0);
}

TEST_CASE("ServiceRegistry register and get", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	auto service = std::make_shared<TestServiceImpl>(42);
	registry.registerService<ITestService>(service);

	REQUIRE(registry.has<ITestService>());
	REQUIRE(registry.size() == 1);

	auto* retrieved = registry.get<ITestService>();
	REQUIRE(retrieved != nullptr);
	REQUIRE(retrieved->getValue() == 42);
}

TEST_CASE("ServiceRegistry get unregistered returns nullptr", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	auto* result = registry.get<ITestService>();
	REQUIRE(result == nullptr);
}

TEST_CASE("ServiceRegistry has returns false for unregistered", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	REQUIRE_FALSE(registry.has<ITestService>());
}

TEST_CASE("ServiceRegistry multiple services", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	auto svc1 = std::make_shared<TestServiceImpl>(10);
	auto svc2 = std::make_shared<AnotherServiceImpl>("hello");

	registry.registerService<ITestService>(svc1);
	registry.registerService<IAnotherService>(svc2);

	REQUIRE(registry.size() == 2);
	REQUIRE(registry.get<ITestService>()->getValue() == 10);
	REQUIRE(registry.get<IAnotherService>()->getName() == "hello");
}

TEST_CASE("ServiceRegistry overwrite replaces previous", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	auto svc1 = std::make_shared<TestServiceImpl>(1);
	auto svc2 = std::make_shared<TestServiceImpl>(2);

	registry.registerService<ITestService>(svc1);
	REQUIRE(registry.get<ITestService>()->getValue() == 1);

	registry.registerService<ITestService>(svc2);
	REQUIRE(registry.get<ITestService>()->getValue() == 2);
	REQUIRE(registry.size() == 1);
}

TEST_CASE("ServiceRegistry unregister removes service", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	auto svc = std::make_shared<TestServiceImpl>(99);
	registry.registerService<ITestService>(svc);
	REQUIRE(registry.has<ITestService>());

	const bool removed = registry.unregister<ITestService>();
	REQUIRE(removed);
	REQUIRE_FALSE(registry.has<ITestService>());
	REQUIRE(registry.get<ITestService>() == nullptr);
}

TEST_CASE("ServiceRegistry unregister nonexistent returns false", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	const bool removed = registry.unregister<ITestService>();
	REQUIRE_FALSE(removed);
}

TEST_CASE("ServiceRegistry clear removes all", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;

	registry.registerService<ITestService>(
		std::make_shared<TestServiceImpl>(1));
	registry.registerService<IAnotherService>(
		std::make_shared<AnotherServiceImpl>("x"));

	REQUIRE(registry.size() == 2);

	registry.clear();

	REQUIRE(registry.empty());
	REQUIRE(registry.size() == 0);
	REQUIRE_FALSE(registry.has<ITestService>());
	REQUIRE_FALSE(registry.has<IAnotherService>());
}

TEST_CASE("ServiceRegistry move semantics", "[mitiru][core][ServiceRegistry]")
{
	mitiru::ServiceRegistry registry;
	registry.registerService<ITestService>(
		std::make_shared<TestServiceImpl>(77));

	mitiru::ServiceRegistry moved(std::move(registry));

	REQUIRE(moved.has<ITestService>());
	REQUIRE(moved.get<ITestService>()->getValue() == 77);
}
