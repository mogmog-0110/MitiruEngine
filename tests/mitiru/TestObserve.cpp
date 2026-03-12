#include <catch2/catch_test_macros.hpp>
#include "CatchStringViewSupport.hpp"

#include <string>

#include <mitiru/observe/SemanticLabel.hpp>
#include <mitiru/observe/Inspector.hpp>
#include <mitiru/observe/DiffTracker.hpp>
#include <mitiru/observe/Snapshot.hpp>
#include <mitiru/observe/SnapshotSchema.hpp>
#include <mitiru/observe/ObserveServer.hpp>
#include <mitiru/core/Engine.hpp>

// ============================================================
// SemanticLabel
// ============================================================

TEST_CASE("SemanticLabel - initial state is empty", "[observe]")
{
	const mitiru::observe::SemanticLabel label;

	REQUIRE(label.empty());
	REQUIRE(label.size() == 0);
}

TEST_CASE("SemanticLabel - addLabel adds a label", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("controllable");

	REQUIRE(label.size() == 1);
	REQUIRE(label.hasLabel("controllable"));
}

TEST_CASE("SemanticLabel - addLabel prevents duplicates", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("enemy");
	label.addLabel("enemy");

	REQUIRE(label.size() == 1);
}

TEST_CASE("SemanticLabel - hasLabel returns false for missing label", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("physics");

	REQUIRE_FALSE(label.hasLabel("enemy"));
}

TEST_CASE("SemanticLabel - removeLabel removes existing label", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("physics");
	label.addLabel("enemy");

	label.removeLabel("physics");

	REQUIRE(label.size() == 1);
	REQUIRE_FALSE(label.hasLabel("physics"));
	REQUIRE(label.hasLabel("enemy"));
}

TEST_CASE("SemanticLabel - removeLabel on missing label does nothing", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("controllable");

	label.removeLabel("nonexistent");

	REQUIRE(label.size() == 1);
}

TEST_CASE("SemanticLabel - multiple labels", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("controllable");
	label.addLabel("physics");
	label.addLabel("enemy");

	REQUIRE(label.size() == 3);
	REQUIRE(label.hasLabel("controllable"));
	REQUIRE(label.hasLabel("physics"));
	REQUIRE(label.hasLabel("enemy"));
}

TEST_CASE("SemanticLabel - toJson with no labels returns empty array", "[observe]")
{
	const mitiru::observe::SemanticLabel label;
	REQUIRE(label.toJson() == "[]");
}

TEST_CASE("SemanticLabel - toJson with one label", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("controllable");

	REQUIRE(label.toJson() == "[\"controllable\"]");
}

TEST_CASE("SemanticLabel - toJson with multiple labels", "[observe]")
{
	mitiru::observe::SemanticLabel label;
	label.addLabel("physics");
	label.addLabel("enemy");

	const auto json = label.toJson();
	REQUIRE(json == "[\"physics\",\"enemy\"]");
}

// ============================================================
// Inspector
// ============================================================

TEST_CASE("Inspector - initial state is empty", "[observe]")
{
	const mitiru::observe::Inspector inspector;

	REQUIRE(inspector.stateCount() == 0);
}

TEST_CASE("Inspector - registerState and queryState", "[observe]")
{
	mitiru::observe::Inspector inspector;
	inspector.registerState("player.health", "100");

	const auto result = inspector.queryState("player.health");
	REQUIRE(result.has_value());
	REQUIRE(result.value() == "100");
}

TEST_CASE("Inspector - queryState returns nullopt for missing key", "[observe]")
{
	const mitiru::observe::Inspector inspector;

	const auto result = inspector.queryState("nonexistent");
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Inspector - registerState overwrites existing value", "[observe]")
{
	mitiru::observe::Inspector inspector;
	inspector.registerState("score", "50");
	inspector.registerState("score", "100");

	REQUIRE(inspector.stateCount() == 1);
	REQUIRE(inspector.queryState("score").value() == "100");
}

TEST_CASE("Inspector - queryAll returns JSON with all states", "[observe]")
{
	mitiru::observe::Inspector inspector;
	inspector.registerState("a", "1");
	inspector.registerState("b", "2");

	const auto json = inspector.queryAll();
	/// map is sorted, so "a" comes before "b"
	REQUIRE(json == "{\"a\":\"1\",\"b\":\"2\"}");
}

TEST_CASE("Inspector - queryAll on empty returns empty object", "[observe]")
{
	const mitiru::observe::Inspector inspector;
	REQUIRE(inspector.queryAll() == "{}");
}

TEST_CASE("Inspector - queryByPrefix filters matching keys", "[observe]")
{
	mitiru::observe::Inspector inspector;
	inspector.registerState("player.x", "10");
	inspector.registerState("player.y", "20");
	inspector.registerState("enemy.x", "30");

	const auto json = inspector.queryByPrefix("player.");
	REQUIRE(json.find("player.x") != std::string::npos);
	REQUIRE(json.find("player.y") != std::string::npos);
	REQUIRE(json.find("enemy.x") == std::string::npos);
}

TEST_CASE("Inspector - queryByPrefix with no match returns empty object", "[observe]")
{
	mitiru::observe::Inspector inspector;
	inspector.registerState("player.x", "10");

	REQUIRE(inspector.queryByPrefix("enemy.") == "{}");
}

TEST_CASE("Inspector - removeState deletes a key", "[observe]")
{
	mitiru::observe::Inspector inspector;
	inspector.registerState("key", "value");

	REQUIRE(inspector.removeState("key") == true);
	REQUIRE(inspector.stateCount() == 0);
	REQUIRE_FALSE(inspector.queryState("key").has_value());
}

TEST_CASE("Inspector - removeState returns false for missing key", "[observe]")
{
	mitiru::observe::Inspector inspector;

	REQUIRE(inspector.removeState("nonexistent") == false);
}

TEST_CASE("Inspector - clear removes all states", "[observe]")
{
	mitiru::observe::Inspector inspector;
	inspector.registerState("a", "1");
	inspector.registerState("b", "2");

	inspector.clear();

	REQUIRE(inspector.stateCount() == 0);
	REQUIRE(inspector.queryAll() == "{}");
}

TEST_CASE("Inspector - stateCount tracks registrations", "[observe]")
{
	mitiru::observe::Inspector inspector;

	REQUIRE(inspector.stateCount() == 0);

	inspector.registerState("key1", "val1");
	REQUIRE(inspector.stateCount() == 1);

	inspector.registerState("key2", "val2");
	REQUIRE(inspector.stateCount() == 2);

	inspector.registerState("key1", "updated");
	REQUIRE(inspector.stateCount() == 2);
}

// ============================================================
// DiffTracker
// ============================================================

TEST_CASE("DiffTracker - no changes yields empty diff", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	/// Frame 0
	tracker.beginFrame(0);
	tracker.recordState("x", "10");
	const auto diffs0 = tracker.endFrame();

	/// Frame 1 with same state
	tracker.beginFrame(1);
	tracker.recordState("x", "10");
	const auto diffs1 = tracker.endFrame();

	/// No changes between frame 0 and 1
	REQUIRE(diffs1.empty());
}

TEST_CASE("DiffTracker - detects new key addition", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	/// Frame 0: empty
	tracker.beginFrame(0);
	const auto diffs0 = tracker.endFrame();

	/// Frame 1: add key
	tracker.beginFrame(1);
	tracker.recordState("health", "100");
	const auto diffs1 = tracker.endFrame();

	REQUIRE(diffs1.size() == 1);
	REQUIRE(diffs1[0].key == "health");
	REQUIRE(diffs1[0].oldValue.empty());
	REQUIRE(diffs1[0].newValue == "100");
	REQUIRE(diffs1[0].frame == 1);
}

TEST_CASE("DiffTracker - detects value change", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	tracker.beginFrame(0);
	tracker.recordState("score", "50");
	tracker.endFrame();

	tracker.beginFrame(1);
	tracker.recordState("score", "75");
	const auto diffs = tracker.endFrame();

	REQUIRE(diffs.size() == 1);
	REQUIRE(diffs[0].key == "score");
	REQUIRE(diffs[0].oldValue == "50");
	REQUIRE(diffs[0].newValue == "75");
}

TEST_CASE("DiffTracker - detects key deletion", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	tracker.beginFrame(0);
	tracker.recordState("temp", "active");
	tracker.endFrame();

	/// Frame 1: do not record "temp"
	tracker.beginFrame(1);
	const auto diffs = tracker.endFrame();

	REQUIRE(diffs.size() == 1);
	REQUIRE(diffs[0].key == "temp");
	REQUIRE(diffs[0].oldValue == "active");
	REQUIRE(diffs[0].newValue.empty());
}

TEST_CASE("DiffTracker - multiple changes in one frame", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	tracker.beginFrame(0);
	tracker.recordState("a", "1");
	tracker.recordState("b", "2");
	tracker.endFrame();

	tracker.beginFrame(1);
	tracker.recordState("a", "10");
	tracker.recordState("b", "20");
	tracker.recordState("c", "30");
	const auto diffs = tracker.endFrame();

	/// a changed, b changed, c added = 3 diffs
	REQUIRE(diffs.size() == 3);
}

TEST_CASE("DiffTracker - toJson produces valid JSON array", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	tracker.beginFrame(0);
	tracker.endFrame();

	tracker.beginFrame(1);
	tracker.recordState("x", "42");
	const auto diffs = tracker.endFrame();

	const auto json = mitiru::observe::DiffTracker::toJson(diffs);
	REQUIRE(json.front() == '[');
	REQUIRE(json.back() == ']');
	REQUIRE(json.find("\"key\":\"x\"") != std::string::npos);
	REQUIRE(json.find("\"newValue\":\"42\"") != std::string::npos);
}

TEST_CASE("DiffTracker - toJson with empty diffs returns empty array", "[observe]")
{
	const std::vector<mitiru::observe::DiffEntry> empty;
	REQUIRE(mitiru::observe::DiffTracker::toJson(empty) == "[]");
}

TEST_CASE("DiffEntry - toJson produces valid JSON object", "[observe]")
{
	mitiru::observe::DiffEntry entry;
	entry.key = "health";
	entry.oldValue = "100";
	entry.newValue = "80";
	entry.frame = 5;

	const auto json = entry.toJson();
	REQUIRE(json.front() == '{');
	REQUIRE(json.back() == '}');
	REQUIRE(json.find("\"key\":\"health\"") != std::string::npos);
	REQUIRE(json.find("\"frame\":5") != std::string::npos);
}

TEST_CASE("DiffTracker - reset clears all state", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	tracker.beginFrame(0);
	tracker.recordState("a", "1");
	tracker.endFrame();

	tracker.reset();

	/// After reset, recording same key should appear as new
	tracker.beginFrame(1);
	tracker.recordState("a", "1");
	const auto diffs = tracker.endFrame();

	/// "a" is new after reset
	REQUIRE(diffs.size() == 1);
	REQUIRE(diffs[0].oldValue.empty());
}

TEST_CASE("DiffTracker - first frame all entries are new additions", "[observe]")
{
	mitiru::observe::DiffTracker tracker;

	tracker.beginFrame(0);
	tracker.recordState("x", "10");
	tracker.recordState("y", "20");
	const auto diffs = tracker.endFrame();

	/// Both are new (no previous frame)
	REQUIRE(diffs.size() == 2);
	for (const auto& d : diffs)
	{
		REQUIRE(d.oldValue.empty());
	}
}

// ============================================================
// SnapshotData
// ============================================================

TEST_CASE("SnapshotData - default values", "[observe]")
{
	const mitiru::SnapshotData data;

	REQUIRE(data.frameNumber == 0);
	REQUIRE(data.timestamp == 0.0f);
	REQUIRE(data.entityCount == 0);
}

TEST_CASE("SnapshotData - toJson produces valid JSON", "[observe]")
{
	mitiru::SnapshotData data;
	data.frameNumber = 42;
	data.timestamp = 1.5f;
	data.entityCount = 10;

	const auto json = data.toJson();
	REQUIRE(json.front() == '{');
	REQUIRE(json.back() == '}');
	REQUIRE(json.find("\"frameNumber\":42") != std::string::npos);
	REQUIRE(json.find("\"entityCount\":10") != std::string::npos);
}

TEST_CASE("SnapshotData - toJson contains timestamp", "[observe]")
{
	mitiru::SnapshotData data;
	data.timestamp = 3.14f;

	const auto json = data.toJson();
	REQUIRE(json.find("\"timestamp\":") != std::string::npos);
}

// ============================================================
// SnapshotSchema
// ============================================================

TEST_CASE("SnapshotSchema - version is defined", "[observe]")
{
	REQUIRE_FALSE(mitiru::observe::SnapshotSchema::VERSION.empty());
	REQUIRE(mitiru::observe::SnapshotSchema::VERSION == "2.0.0");
}

TEST_CASE("SnapshotSchema - schemaJson returns non-empty string", "[observe]")
{
	const auto schema = mitiru::observe::SnapshotSchema::schemaJson();

	REQUIRE_FALSE(schema.empty());
	REQUIRE(schema.front() == '{');
	REQUIRE(schema.back() == '}');
}

TEST_CASE("SnapshotSchema - schemaJson contains expected properties", "[observe]")
{
	const auto schema = mitiru::observe::SnapshotSchema::schemaJson();

	REQUIRE(schema.find("\"frameNumber\"") != std::string::npos);
	REQUIRE(schema.find("\"timestamp\"") != std::string::npos);
	REQUIRE(schema.find("\"entityCount\"") != std::string::npos);
	REQUIRE(schema.find("\"entities\"") != std::string::npos);
	REQUIRE(schema.find("\"ui\"") != std::string::npos);
	REQUIRE(schema.find("\"states\"") != std::string::npos);
	REQUIRE(schema.find("\"diffs\"") != std::string::npos);
}

TEST_CASE("SnapshotSchema - schemaJson contains version", "[observe]")
{
	const auto schema = mitiru::observe::SnapshotSchema::schemaJson();
	REQUIRE(schema.find("2.0.0") != std::string::npos);
}

TEST_CASE("EntitySchema - schemaJson returns valid JSON", "[observe]")
{
	const auto schema = mitiru::observe::EntitySchema::schemaJson();

	REQUIRE_FALSE(schema.empty());
	REQUIRE(schema.find("\"id\"") != std::string::npos);
	REQUIRE(schema.find("\"position\"") != std::string::npos);
	REQUIRE(schema.find("\"labels\"") != std::string::npos);
}

TEST_CASE("UISchema - schemaJson returns valid JSON", "[observe]")
{
	const auto schema = mitiru::observe::UISchema::schemaJson();

	REQUIRE_FALSE(schema.empty());
	REQUIRE(schema.find("\"elementType\"") != std::string::npos);
	REQUIRE(schema.find("\"bounds\"") != std::string::npos);
	REQUIRE(schema.find("\"visible\"") != std::string::npos);
}

// ============================================================
// ObserveServer
// ============================================================

TEST_CASE("ObserveServer - default constructor", "[observe]")
{
	const mitiru::observe::ObserveServer server;

	REQUIRE_FALSE(server.isRunning());
	REQUIRE(server.config().port == 8080);
	REQUIRE(server.config().host == "0.0.0.0");
	REQUIRE(server.config().enableCors == true);
}

TEST_CASE("ObserveServer - config constructor", "[observe]")
{
	mitiru::observe::ObserveServerConfig config;
	config.port = 9090;
	config.host = "127.0.0.1";
	config.enableCors = false;

	const mitiru::observe::ObserveServer server(config);

	REQUIRE(server.config().port == 9090);
	REQUIRE(server.config().host == "127.0.0.1");
	REQUIRE(server.config().enableCors == false);
}

TEST_CASE("ObserveServer - start and stop", "[observe]")
{
	mitiru::Engine engine;
	mitiru::observe::Inspector inspector;
	mitiru::observe::ObserveServer server;

	REQUIRE(server.start(3000, engine, inspector) == true);
	REQUIRE(server.isRunning());
	REQUIRE(server.config().port == 3000);

	server.stop();
	REQUIRE_FALSE(server.isRunning());
}

TEST_CASE("ObserveServer - start updates port in config", "[observe]")
{
	mitiru::Engine engine;
	mitiru::observe::Inspector inspector;
	mitiru::observe::ObserveServer server;
	server.start(5555, engine, inspector);

	REQUIRE(server.config().port == 5555);

	server.stop();
}

TEST_CASE("ObserveServer - multiple start stop cycles", "[observe]")
{
	mitiru::Engine engine;
	mitiru::observe::Inspector inspector;
	mitiru::observe::ObserveServer server;

	server.start(8080, engine, inspector);
	REQUIRE(server.isRunning());

	server.stop();
	REQUIRE_FALSE(server.isRunning());

	server.start(9090, engine, inspector);
	REQUIRE(server.isRunning());
	REQUIRE(server.config().port == 9090);

	server.stop();
	REQUIRE_FALSE(server.isRunning());
}
