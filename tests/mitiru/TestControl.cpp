/// @file TestControl.cpp
/// @brief mitiru制御モジュールのテスト
/// @details CommandQueue, CommandSchema, ActionExecutor, ReplaySystem, Scriptingの単体テスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CatchStringViewSupport.hpp"

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "mitiru/control/CommandQueue.hpp"
#include "mitiru/control/CommandSchema.hpp"
#include "mitiru/control/ActionExecutor.hpp"
#include "mitiru/control/ReplaySystem.hpp"
#include "mitiru/control/Scripting.hpp"

// ============================================================
// CommandQueue tests
// ============================================================

TEST_CASE("CommandQueue - initially empty", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;
	CHECK(queue.empty());
	CHECK(queue.pendingCount() == 0);
}

TEST_CASE("CommandQueue - submit adds commands", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;

	queue.submit(mitiru::control::Command{"key_down", "65"});
	CHECK(queue.pendingCount() == 1);
	CHECK_FALSE(queue.empty());

	queue.submit(mitiru::control::Command{"key_up", "65"});
	CHECK(queue.pendingCount() == 2);
}

TEST_CASE("CommandQueue - submit with move semantics", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;

	mitiru::control::Command cmd{"snapshot", ""};
	queue.submit(std::move(cmd));
	CHECK(queue.pendingCount() == 1);
}

TEST_CASE("CommandQueue - consumeAll returns all and clears", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;

	queue.submit(mitiru::control::Command{"key_down", "65"});
	queue.submit(mitiru::control::Command{"key_up", "65"});
	queue.submit(mitiru::control::Command{"snapshot", ""});

	const auto commands = queue.consumeAll();
	CHECK(commands.size() == 3);
	CHECK(commands[0].type == "key_down");
	CHECK(commands[1].type == "key_up");
	CHECK(commands[2].type == "snapshot");

	/// Queue should be empty after consume
	CHECK(queue.empty());
	CHECK(queue.pendingCount() == 0);
}

TEST_CASE("CommandQueue - consumeAll on empty queue returns empty", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;
	const auto commands = queue.consumeAll();
	CHECK(commands.empty());
}

TEST_CASE("CommandQueue - clear empties the queue", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;
	queue.submit(mitiru::control::Command{"step", ""});
	queue.submit(mitiru::control::Command{"step", ""});
	CHECK(queue.pendingCount() == 2);

	queue.clear();
	CHECK(queue.empty());
	CHECK(queue.pendingCount() == 0);
}

TEST_CASE("CommandQueue - thread safety with concurrent submits", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;
	constexpr int THREADS = 4;
	constexpr int COMMANDS_PER_THREAD = 100;

	std::vector<std::thread> threads;
	threads.reserve(THREADS);

	for (int t = 0; t < THREADS; ++t)
	{
		threads.emplace_back([&queue, t]()
		{
			for (int i = 0; i < COMMANDS_PER_THREAD; ++i)
			{
				queue.submit(mitiru::control::Command{
					"thread_" + std::to_string(t),
					std::to_string(i)
				});
			}
		});
	}

	for (auto& th : threads)
	{
		th.join();
	}

	CHECK(queue.pendingCount() == THREADS * COMMANDS_PER_THREAD);

	const auto all = queue.consumeAll();
	CHECK(all.size() == THREADS * COMMANDS_PER_THREAD);
	CHECK(queue.empty());
}

TEST_CASE("CommandQueue - thread safety with concurrent submit and consume", "[mitiru][control][commandqueue]")
{
	mitiru::control::CommandQueue queue;
	std::atomic<int> totalConsumed{0};
	constexpr int TOTAL_COMMANDS = 200;

	/// Producer thread
	std::thread producer([&queue]()
	{
		for (int i = 0; i < TOTAL_COMMANDS; ++i)
		{
			queue.submit(mitiru::control::Command{"step", std::to_string(i)});
		}
	});

	/// Consumer thread
	std::thread consumer([&queue, &totalConsumed]()
	{
		while (totalConsumed.load() < TOTAL_COMMANDS)
		{
			const auto batch = queue.consumeAll();
			totalConsumed.fetch_add(static_cast<int>(batch.size()));
		}
	});

	producer.join();
	consumer.join();

	/// All commands eventually consumed
	CHECK(totalConsumed.load() == TOTAL_COMMANDS);
}

// ============================================================
// CommandSchema tests
// ============================================================

TEST_CASE("CommandSchema - parseCommand extracts type and payload", "[mitiru][control][schema]")
{
	const auto result = mitiru::control::parseCommand(
		R"({"type":"key_down","payload":"65"})");

	REQUIRE(result.has_value());
	CHECK(result->type == "key_down");
	CHECK(result->payload == "65");
}

TEST_CASE("CommandSchema - parseCommand with type only", "[mitiru][control][schema]")
{
	const auto result = mitiru::control::parseCommand(R"({"type":"snapshot"})");

	REQUIRE(result.has_value());
	CHECK(result->type == "snapshot");
	CHECK(result->payload.empty());
}

TEST_CASE("CommandSchema - parseCommand returns nullopt for missing type", "[mitiru][control][schema]")
{
	const auto result = mitiru::control::parseCommand(R"({"payload":"data"})");
	CHECK_FALSE(result.has_value());
}

TEST_CASE("CommandSchema - parseCommand returns nullopt for empty string", "[mitiru][control][schema]")
{
	const auto result = mitiru::control::parseCommand("");
	CHECK_FALSE(result.has_value());
}

TEST_CASE("CommandSchema - parseCommand handles all known command types", "[mitiru][control][schema]")
{
	const std::vector<std::string> types = {
		"key_down", "key_up", "mouse_move", "mouse_down", "mouse_up",
		"wait_frames", "snapshot", "capture", "step"
	};

	for (const auto& t : types)
	{
		const auto json = R"({"type":")" + t + R"("})";
		const auto result = mitiru::control::parseCommand(json);
		REQUIRE(result.has_value());
		CHECK(result->type == t);
	}
}

TEST_CASE("CommandSchema - commandToJson produces valid JSON", "[mitiru][control][schema]")
{
	mitiru::control::Command cmd{"key_down", "65"};
	const auto json = mitiru::control::commandToJson(cmd);

	CHECK(json.find("\"type\":\"key_down\"") != std::string::npos);
	CHECK(json.find("\"payload\":\"65\"") != std::string::npos);
}

TEST_CASE("CommandSchema - commandToJson omits empty payload", "[mitiru][control][schema]")
{
	mitiru::control::Command cmd{"snapshot", ""};
	const auto json = mitiru::control::commandToJson(cmd);

	CHECK(json.find("\"type\":\"snapshot\"") != std::string::npos);
	CHECK(json.find("payload") == std::string::npos);
}

TEST_CASE("CommandSchema - commandToJson and parseCommand roundtrip", "[mitiru][control][schema]")
{
	mitiru::control::Command original{"mouse_move", "xy"};
	const auto json = mitiru::control::commandToJson(original);
	const auto parsed = mitiru::control::parseCommand(json);

	REQUIRE(parsed.has_value());
	CHECK(parsed->type == original.type);
	CHECK(parsed->payload == original.payload);
}

TEST_CASE("CommandSchema - CommandType constants have expected values", "[mitiru][control][schema]")
{
	CHECK(mitiru::control::CommandType::KeyDown == "key_down");
	CHECK(mitiru::control::CommandType::KeyUp == "key_up");
	CHECK(mitiru::control::CommandType::MouseMove == "mouse_move");
	CHECK(mitiru::control::CommandType::MouseDown == "mouse_down");
	CHECK(mitiru::control::CommandType::MouseUp == "mouse_up");
	CHECK(mitiru::control::CommandType::WaitFrames == "wait_frames");
	CHECK(mitiru::control::CommandType::Snapshot == "snapshot");
	CHECK(mitiru::control::CommandType::Capture == "capture");
	CHECK(mitiru::control::CommandType::Step == "step");
}

TEST_CASE("CommandSchema - schemaJson returns non-empty schema", "[mitiru][control][schema]")
{
	const auto schema = mitiru::control::schemaJson();
	CHECK_FALSE(schema.empty());
	CHECK(schema.find("\"type\"") != std::string::npos);
	CHECK(schema.find("key_down") != std::string::npos);
	CHECK(schema.find("snapshot") != std::string::npos);
}

// ============================================================
// ActionExecutor tests
// ============================================================

TEST_CASE("ActionExecutor - initially has no actions", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	CHECK(executor.actionCount() == 0);
	CHECK(executor.listActions().empty());
}

TEST_CASE("ActionExecutor - registerAction adds handler", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	executor.registerAction("test_action", [](std::string_view) {});

	CHECK(executor.actionCount() == 1);
	CHECK(executor.hasAction("test_action"));
	CHECK_FALSE(executor.hasAction("nonexistent"));
}

TEST_CASE("ActionExecutor - execute dispatches to correct handler", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;

	std::string capturedPayload;
	executor.registerAction("key_down", [&capturedPayload](std::string_view payload)
	{
		capturedPayload = std::string(payload);
	});

	const bool result = executor.execute(mitiru::control::Command{"key_down", "test_payload"});
	CHECK(result == true);
	CHECK(capturedPayload == "test_payload");
}

TEST_CASE("ActionExecutor - execute returns false for unknown action", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	const bool result = executor.execute(mitiru::control::Command{"unknown", ""});
	CHECK(result == false);
}

TEST_CASE("ActionExecutor - executeAll processes multiple commands", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	int callCount = 0;

	executor.registerAction("step", [&callCount](std::string_view)
	{
		++callCount;
	});
	executor.registerAction("snapshot", [&callCount](std::string_view)
	{
		++callCount;
	});

	const std::vector<mitiru::control::Command> commands = {
		{"step", ""},
		{"snapshot", ""},
		{"unknown_cmd", ""},
		{"step", ""}
	};

	const auto executed = executor.executeAll(commands);
	CHECK(executed == 3);
	CHECK(callCount == 3);
}

TEST_CASE("ActionExecutor - listActions returns registered names", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	executor.registerAction("action_b", [](std::string_view) {});
	executor.registerAction("action_a", [](std::string_view) {});
	executor.registerAction("action_c", [](std::string_view) {});

	auto names = executor.listActions();
	CHECK(names.size() == 3);

	/// std::map keeps keys sorted
	CHECK(names[0] == "action_a");
	CHECK(names[1] == "action_b");
	CHECK(names[2] == "action_c");
}

TEST_CASE("ActionExecutor - removeAction removes handler", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	executor.registerAction("temp", [](std::string_view) {});
	CHECK(executor.hasAction("temp"));

	const bool removed = executor.removeAction("temp");
	CHECK(removed == true);
	CHECK_FALSE(executor.hasAction("temp"));
	CHECK(executor.actionCount() == 0);
}

TEST_CASE("ActionExecutor - removeAction returns false for nonexistent", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	const bool removed = executor.removeAction("nonexistent");
	CHECK(removed == false);
}

TEST_CASE("ActionExecutor - clear removes all handlers", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;
	executor.registerAction("a", [](std::string_view) {});
	executor.registerAction("b", [](std::string_view) {});
	CHECK(executor.actionCount() == 2);

	executor.clear();
	CHECK(executor.actionCount() == 0);
	CHECK(executor.listActions().empty());
}

TEST_CASE("ActionExecutor - registerAction replaces existing handler", "[mitiru][control][executor]")
{
	mitiru::control::ActionExecutor executor;

	int firstCalled = 0;
	int secondCalled = 0;

	executor.registerAction("action", [&firstCalled](std::string_view) { ++firstCalled; });
	executor.registerAction("action", [&secondCalled](std::string_view) { ++secondCalled; });

	CHECK(executor.actionCount() == 1);
	executor.execute(mitiru::control::Command{"action", ""});

	CHECK(firstCalled == 0);
	CHECK(secondCalled == 1);
}

// ============================================================
// ReplaySystem tests
// ============================================================

TEST_CASE("ReplaySystem - initial state is Idle", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	CHECK(system.state() == mitiru::control::ReplayState::Idle);
	CHECK_FALSE(system.isPlaying());
	CHECK_FALSE(system.isRecording());
}

TEST_CASE("ReplaySystem - startRecording transitions to Recording", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	system.startRecording(42, 30);

	CHECK(system.isRecording());
	CHECK_FALSE(system.isPlaying());
	CHECK(system.state() == mitiru::control::ReplayState::Recording);
}

TEST_CASE("ReplaySystem - stopRecording returns data and transitions to Idle", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	system.startRecording(100, 60);

	/// Record some commands
	std::vector<mitiru::InputCommand> cmds;
	cmds.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 65,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});
	system.recordFrameCommands(0, cmds);
	system.recordFrameCommands(1, {});

	const auto data = system.stopRecording();
	CHECK(data.seed == 100);
	CHECK(data.tps == 60);
	CHECK(data.totalFrames() == 2);
	CHECK(system.state() == mitiru::control::ReplayState::Idle);
}

TEST_CASE("ReplaySystem - stopRecording when not recording returns empty data", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	const auto data = system.stopRecording();
	CHECK(data.totalFrames() == 0);
}

TEST_CASE("ReplaySystem - startPlayback transitions to Playing", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;

	mitiru::ReplayData data;
	data.seed = 1;
	mitiru::InputFrame f;
	f.frameNumber = 0;
	f.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 65,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});
	data.frames.push_back(f);

	system.startPlayback(data);
	CHECK(system.isPlaying());
	CHECK_FALSE(system.isRecording());
	CHECK(system.state() == mitiru::control::ReplayState::Playing);
}

TEST_CASE("ReplaySystem - getPlaybackCommands returns correct commands", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;

	mitiru::ReplayData data;
	mitiru::InputFrame f;
	f.frameNumber = 5;
	f.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 87,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});
	data.frames.push_back(f);

	system.startPlayback(data);
	const auto cmds = system.getPlaybackCommands(5);
	REQUIRE(cmds.size() == 1);
	CHECK(cmds[0].keyCode == 87);
}

TEST_CASE("ReplaySystem - getPlaybackCommands returns empty when not playing", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	const auto cmds = system.getPlaybackCommands(0);
	CHECK(cmds.empty());
}

TEST_CASE("ReplaySystem - recording with InputState", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	system.startRecording(0, 60);

	mitiru::InputState state;
	state.setKeyDown(65, true);  // A key
	state.setMousePosition(50.0f, 75.0f);

	system.recordFrame(0, state);

	const auto data = system.stopRecording();
	REQUIRE(data.totalFrames() == 1);
	/// Should have at least a KeyDown for A and a MouseMove
	CHECK(data.frames[0].commands.size() >= 2);
}

TEST_CASE("ReplaySystem - recordFrameCommands is ignored when not recording", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	system.recordFrameCommands(0, {mitiru::InputCommand{}});

	/// Not recording, so stopRecording gives empty
	const auto data = system.stopRecording();
	CHECK(data.totalFrames() == 0);
}

TEST_CASE("ReplaySystem - startPlayback stops active recording", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	system.startRecording(1, 60);
	CHECK(system.isRecording());

	mitiru::ReplayData data;
	system.startPlayback(data);

	CHECK_FALSE(system.isRecording());
	CHECK(system.isPlaying());
}

TEST_CASE("ReplaySystem - startRecording stops active playback", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;

	mitiru::ReplayData data;
	system.startPlayback(data);
	CHECK(system.isPlaying());

	system.startRecording(1, 60);
	CHECK_FALSE(system.isPlaying());
	CHECK(system.isRecording());
}

TEST_CASE("ReplaySystem - reset transitions to Idle", "[mitiru][control][replay]")
{
	mitiru::control::ReplaySystem system;
	system.startRecording(1, 60);
	system.reset();

	CHECK(system.state() == mitiru::control::ReplayState::Idle);
	CHECK_FALSE(system.isRecording());
	CHECK_FALSE(system.isPlaying());
}

// ============================================================
// Scripting tests
// ============================================================

TEST_CASE("Scripting - parseScript extracts actions from JSON", "[mitiru][control][scripting]")
{
	const std::string json = R"([{"type":"key_down","params":{"keyCode":"65"}},{"type":"wait","params":{"frames":"10"}}])";
	const auto seq = mitiru::control::parseScript(json);

	REQUIRE(seq.size() == 2);
	CHECK(seq[0].type == "key_down");
	CHECK(seq[0].params.at("keyCode") == "65");
	CHECK(seq[1].type == "wait");
	CHECK(seq[1].params.at("frames") == "10");
}

TEST_CASE("Scripting - parseScript returns empty for empty array", "[mitiru][control][scripting]")
{
	const auto seq = mitiru::control::parseScript("[]");
	CHECK(seq.empty());
}

TEST_CASE("Scripting - parseScript returns empty for empty string", "[mitiru][control][scripting]")
{
	const auto seq = mitiru::control::parseScript("");
	CHECK(seq.empty());
}

TEST_CASE("Scripting - parseScript handles action without params", "[mitiru][control][scripting]")
{
	const std::string json = R"([{"type":"snapshot"}])";
	const auto seq = mitiru::control::parseScript(json);

	REQUIRE(seq.size() == 1);
	CHECK(seq[0].type == "snapshot");
	CHECK(seq[0].params.empty());
}

TEST_CASE("Scripting - ScriptAction toJson produces valid output", "[mitiru][control][scripting]")
{
	mitiru::control::ScriptAction action;
	action.type = "key_down";
	action.params["keyCode"] = "65";
	action.params["duration"] = "100";

	const auto json = action.toJson();
	CHECK(json.find("\"type\":\"key_down\"") != std::string::npos);
	CHECK(json.find("\"keyCode\":\"65\"") != std::string::npos);
	CHECK(json.find("\"duration\":\"100\"") != std::string::npos);
}

TEST_CASE("Scripting - scriptToJson produces JSON array", "[mitiru][control][scripting]")
{
	mitiru::control::ScriptSequence seq;

	mitiru::control::ScriptAction a1;
	a1.type = "step";
	seq.push_back(a1);

	mitiru::control::ScriptAction a2;
	a2.type = "snapshot";
	seq.push_back(a2);

	const auto json = mitiru::control::scriptToJson(seq);
	CHECK(json.front() == '[');
	CHECK(json.back() == ']');
	CHECK(json.find("\"type\":\"step\"") != std::string::npos);
	CHECK(json.find("\"type\":\"snapshot\"") != std::string::npos);
}

TEST_CASE("Scripting - scriptToJson for empty sequence", "[mitiru][control][scripting]")
{
	const mitiru::control::ScriptSequence seq;
	const auto json = mitiru::control::scriptToJson(seq);
	CHECK(json == "[]");
}

TEST_CASE("Scripting - parseScript with multiple params", "[mitiru][control][scripting]")
{
	const std::string json = R"([{"type":"mouse_move","params":{"x":"100","y":"200"}}])";
	const auto seq = mitiru::control::parseScript(json);

	REQUIRE(seq.size() == 1);
	CHECK(seq[0].type == "mouse_move");
	CHECK(seq[0].params.at("x") == "100");
	CHECK(seq[0].params.at("y") == "200");
}
