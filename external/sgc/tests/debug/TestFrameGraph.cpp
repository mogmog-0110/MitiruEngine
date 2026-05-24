#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <sgc/debug/FrameGraph.hpp>

using namespace sgc::debug;

TEST_CASE("FrameGraph - initially empty", "[debug]")
{
	FrameGraph graph;
	REQUIRE(graph.entryCount() == 0);
	REQUIRE(graph.entries().empty());
	REQUIRE(graph.currentDepth() == 0);
}

TEST_CASE("FrameGraph - begin and end section", "[debug]")
{
	FrameGraph graph;
	auto idx = graph.beginSection("Update", 0xFF0000);
	graph.endSection(idx);

	REQUIRE(graph.entryCount() == 1);
	REQUIRE(graph.entries()[0].name == "Update");
	REQUIRE(graph.entries()[0].color == 0xFF0000);
	REQUIRE(graph.entries()[0].depth == 0);
	REQUIRE(graph.entries()[0].durationMs >= 0.0);
}

TEST_CASE("FrameGraph - nested sections have increasing depth", "[debug]")
{
	FrameGraph graph;
	auto outer = graph.beginSection("Outer");
	auto inner = graph.beginSection("Inner");
	graph.endSection(inner);
	graph.endSection(outer);

	REQUIRE(graph.entryCount() == 2);
	REQUIRE(graph.entries()[0].depth == 0);  // Outer
	REQUIRE(graph.entries()[1].depth == 1);  // Inner
}

TEST_CASE("FrameGraph - clearFrame resets all data", "[debug]")
{
	FrameGraph graph;
	auto idx = graph.beginSection("Test");
	graph.endSection(idx);
	REQUIRE(graph.entryCount() == 1);

	graph.clearFrame();
	REQUIRE(graph.entryCount() == 0);
	REQUIRE(graph.currentDepth() == 0);
}

TEST_CASE("FrameGraph - addEntry manual entry", "[debug]")
{
	FrameGraph graph;
	FrameTimingEntry entry;
	entry.name = "Manual";
	entry.startMs = 0.0;
	entry.durationMs = 5.0;
	entry.depth = 0;
	entry.color = 0x00FF00;

	graph.addEntry(entry);
	REQUIRE(graph.entryCount() == 1);
	REQUIRE(graph.entries()[0].name == "Manual");
	REQUIRE(graph.entries()[0].durationMs == Catch::Approx(5.0));
}

TEST_CASE("FrameGraph - endSection with invalid index does not crash", "[debug]")
{
	FrameGraph graph;
	graph.endSection(999);  // 存在しないインデックス
	REQUIRE(graph.entryCount() == 0);
}

TEST_CASE("ScopedSection - RAII begin and end", "[debug]")
{
	FrameGraph graph;

	{
		ScopedSection section(graph, "Scope", 0x0000FF);
		REQUIRE(graph.currentDepth() == 1);
	}

	REQUIRE(graph.currentDepth() == 0);
	REQUIRE(graph.entryCount() == 1);
	REQUIRE(graph.entries()[0].name == "Scope");
	REQUIRE(graph.entries()[0].color == 0x0000FF);
	REQUIRE(graph.entries()[0].durationMs >= 0.0);
}

TEST_CASE("ScopedSection - nested RAII", "[debug]")
{
	FrameGraph graph;

	{
		ScopedSection outer(graph, "Render");
		{
			ScopedSection inner(graph, "DrawSprites");
			REQUIRE(graph.currentDepth() == 2);
		}
		REQUIRE(graph.currentDepth() == 1);
	}

	REQUIRE(graph.currentDepth() == 0);
	REQUIRE(graph.entryCount() == 2);
	REQUIRE(graph.entries()[0].depth == 0);
	REQUIRE(graph.entries()[1].depth == 1);
}

TEST_CASE("FrameGraph - multiple sequential sections", "[debug]")
{
	FrameGraph graph;

	{
		ScopedSection s1(graph, "Update");
	}
	{
		ScopedSection s2(graph, "Render");
	}
	{
		ScopedSection s3(graph, "Audio");
	}

	REQUIRE(graph.entryCount() == 3);
	REQUIRE(graph.entries()[0].name == "Update");
	REQUIRE(graph.entries()[1].name == "Render");
	REQUIRE(graph.entries()[2].name == "Audio");
}

TEST_CASE("FrameTimingEntry - default values", "[debug]")
{
	FrameTimingEntry entry;
	REQUIRE(entry.name.empty());
	REQUIRE(entry.startMs == 0.0);
	REQUIRE(entry.durationMs == 0.0);
	REQUIRE(entry.depth == 0);
	REQUIRE(entry.color == 0);
}
