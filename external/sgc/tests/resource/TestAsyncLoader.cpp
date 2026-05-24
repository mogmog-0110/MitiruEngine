#include <catch2/catch_test_macros.hpp>
#include <sgc/resource/AsyncLoader.hpp>

using namespace sgc;

TEST_CASE("AsyncLoader - enqueue and process single request", "[resource]")
{
	AsyncLoader loader;
	std::string loaded;

	loader.enqueue({"test.png", LoadPriority::Normal,
		[&loaded](const std::string& path) { loaded = path; }});

	REQUIRE(loader.hasPending());
	REQUIRE(loader.pendingCount() == 1);

	loader.processNext();
	REQUIRE(loaded == "test.png");
	REQUIRE_FALSE(loader.hasPending());
}

TEST_CASE("AsyncLoader - priority ordering (high before low)", "[resource]")
{
	AsyncLoader loader;
	std::vector<std::string> order;

	loader.enqueue({"low.png", LoadPriority::Low,
		[&order](const std::string& path) { order.push_back(path); }});
	loader.enqueue({"high.png", LoadPriority::High,
		[&order](const std::string& path) { order.push_back(path); }});
	loader.enqueue({"normal.png", LoadPriority::Normal,
		[&order](const std::string& path) { order.push_back(path); }});
	loader.enqueue({"critical.png", LoadPriority::Critical,
		[&order](const std::string& path) { order.push_back(path); }});

	loader.processAll();

	REQUIRE(order.size() == 4);
	REQUIRE(order[0] == "critical.png");
	REQUIRE(order[1] == "high.png");
	REQUIRE(order[2] == "normal.png");
	REQUIRE(order[3] == "low.png");
}

TEST_CASE("AsyncLoader - processAll empties queue", "[resource]")
{
	AsyncLoader loader;
	int count = 0;

	for (int i = 0; i < 5; ++i)
	{
		loader.enqueue({"file" + std::to_string(i), LoadPriority::Normal,
			[&count](const std::string&) { ++count; }});
	}

	loader.processAll();
	REQUIRE(count == 5);
	REQUIRE_FALSE(loader.hasPending());
}

TEST_CASE("AsyncLoader - progress tracking", "[resource]")
{
	AsyncLoader loader;

	loader.enqueue({"a.png", LoadPriority::Normal, {}});
	loader.enqueue({"b.png", LoadPriority::Normal, {}});
	loader.enqueue({"c.png", LoadPriority::Normal, {}});

	auto prog = loader.progress();
	REQUIRE(prog.total == 3);
	REQUIRE(prog.processed == 0);

	loader.processNext();
	prog = loader.progress();
	REQUIRE(prog.processed == 1);

	loader.processAll();
	prog = loader.progress();
	REQUIRE(prog.processed == 3);
	REQUIRE(prog.isComplete());
}

TEST_CASE("AsyncLoader - cancel removes request", "[resource]")
{
	AsyncLoader loader;
	std::vector<std::string> loaded;

	loader.enqueue({"a.png", LoadPriority::Normal,
		[&loaded](const std::string& path) { loaded.push_back(path); }});
	auto id = loader.enqueue({"b.png", LoadPriority::Normal,
		[&loaded](const std::string& path) { loaded.push_back(path); }});
	loader.enqueue({"c.png", LoadPriority::Normal,
		[&loaded](const std::string& path) { loaded.push_back(path); }});

	loader.cancel(id);
	loader.processAll();

	REQUIRE(loaded.size() == 2);
	REQUIRE(loaded[0] == "a.png");
	REQUIRE(loaded[1] == "c.png");
}

TEST_CASE("AsyncLoader - cancelAll clears queue", "[resource]")
{
	AsyncLoader loader;
	loader.enqueue({"a.png", LoadPriority::Normal, {}});
	loader.enqueue({"b.png", LoadPriority::Normal, {}});

	loader.cancelAll();
	REQUIRE_FALSE(loader.hasPending());
	REQUIRE(loader.pendingCount() == 0);
}

TEST_CASE("AsyncLoader - processNext on empty returns false", "[resource]")
{
	AsyncLoader loader;
	REQUIRE_FALSE(loader.processNext());
}

TEST_CASE("AsyncLoader - enqueue returns unique IDs", "[resource]")
{
	AsyncLoader loader;
	auto id1 = loader.enqueue({"a.png", LoadPriority::Normal, {}});
	auto id2 = loader.enqueue({"b.png", LoadPriority::Normal, {}});
	auto id3 = loader.enqueue({"c.png", LoadPriority::Normal, {}});

	REQUIRE(id1 != id2);
	REQUIRE(id2 != id3);
	REQUIRE(id1 != id3);
}
