/// @file TestInput.cpp
/// @brief mitiru入力モジュールのテスト
/// @details KeyCode, InputRecorder, InputReplayerの単体テスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CatchStringViewSupport.hpp"

#include "mitiru/input/KeyCode.hpp"
#include "mitiru/input/InputRecorder.hpp"
#include "mitiru/input/InputReplayer.hpp"

// ============================================================
// KeyCode tests
// ============================================================

TEST_CASE("KeyCode - keyCodeToString returns correct name for letter keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::A) == "A");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Z) == "Z");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::M) == "M");
}

TEST_CASE("KeyCode - keyCodeToString returns correct name for number keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Num0) == "0");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Num5) == "5");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Num9) == "9");
}

TEST_CASE("KeyCode - keyCodeToString returns correct name for control keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Enter) == "Enter");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Escape) == "Escape");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Space) == "Space");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Backspace) == "Backspace");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Tab) == "Tab");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Shift) == "Shift");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Ctrl) == "Ctrl");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Alt) == "Alt");
}

TEST_CASE("KeyCode - keyCodeToString returns correct name for arrow keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Left) == "Left");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Up) == "Up");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Right) == "Right");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Down) == "Down");
}

TEST_CASE("KeyCode - keyCodeToString returns correct name for function keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::F1) == "F1");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::F6) == "F6");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::F12) == "F12");
}

TEST_CASE("KeyCode - keyCodeToString returns correct name for misc keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Delete) == "Delete");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Insert) == "Insert");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Home) == "Home");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::End) == "End");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::PageUp) == "PageUp");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::PageDown) == "PageDown");
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::CapsLock) == "CapsLock");
}

TEST_CASE("KeyCode - keyCodeToString returns Unknown for invalid codes", "[mitiru][input][keycode]")
{
	CHECK(mitiru::keyCodeToString(mitiru::KeyCode::Unknown) == "Unknown");
	CHECK(mitiru::keyCodeToString(static_cast<mitiru::KeyCode>(999)) == "Unknown");
}

TEST_CASE("KeyCode - stringToKeyCode roundtrip for letter keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::stringToKeyCode("A") == mitiru::KeyCode::A);
	CHECK(mitiru::stringToKeyCode("Z") == mitiru::KeyCode::Z);
	CHECK(mitiru::stringToKeyCode("M") == mitiru::KeyCode::M);

	/// Roundtrip: keyCodeToString -> stringToKeyCode
	for (int ch = 'A'; ch <= 'Z'; ++ch)
	{
		const auto code = static_cast<mitiru::KeyCode>(ch);
		const auto name = mitiru::keyCodeToString(code);
		CHECK(mitiru::stringToKeyCode(name) == code);
	}
}

TEST_CASE("KeyCode - stringToKeyCode handles lowercase letters", "[mitiru][input][keycode]")
{
	CHECK(mitiru::stringToKeyCode("a") == mitiru::KeyCode::A);
	CHECK(mitiru::stringToKeyCode("z") == mitiru::KeyCode::Z);
}

TEST_CASE("KeyCode - stringToKeyCode roundtrip for number keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::stringToKeyCode("0") == mitiru::KeyCode::Num0);
	CHECK(mitiru::stringToKeyCode("5") == mitiru::KeyCode::Num5);
	CHECK(mitiru::stringToKeyCode("9") == mitiru::KeyCode::Num9);
}

TEST_CASE("KeyCode - stringToKeyCode roundtrip for named keys", "[mitiru][input][keycode]")
{
	CHECK(mitiru::stringToKeyCode("Enter") == mitiru::KeyCode::Enter);
	CHECK(mitiru::stringToKeyCode("Escape") == mitiru::KeyCode::Escape);
	CHECK(mitiru::stringToKeyCode("Space") == mitiru::KeyCode::Space);
	CHECK(mitiru::stringToKeyCode("Left") == mitiru::KeyCode::Left);
	CHECK(mitiru::stringToKeyCode("F1") == mitiru::KeyCode::F1);
	CHECK(mitiru::stringToKeyCode("F12") == mitiru::KeyCode::F12);
	CHECK(mitiru::stringToKeyCode("Delete") == mitiru::KeyCode::Delete);
	CHECK(mitiru::stringToKeyCode("PageUp") == mitiru::KeyCode::PageUp);
	CHECK(mitiru::stringToKeyCode("CapsLock") == mitiru::KeyCode::CapsLock);
}

TEST_CASE("KeyCode - stringToKeyCode returns Unknown for invalid names", "[mitiru][input][keycode]")
{
	CHECK(mitiru::stringToKeyCode("") == mitiru::KeyCode::Unknown);
	CHECK(mitiru::stringToKeyCode("InvalidKey") == mitiru::KeyCode::Unknown);
	CHECK(mitiru::stringToKeyCode("!@#") == mitiru::KeyCode::Unknown);
}

// ============================================================
// InputRecorder tests
// ============================================================

TEST_CASE("InputRecorder - initial state is not recording", "[mitiru][input][recorder]")
{
	mitiru::InputRecorder recorder;
	CHECK_FALSE(recorder.isRecording());
	CHECK(recorder.recordedFrameCount() == 0);
}

TEST_CASE("InputRecorder - beginRecording starts recording", "[mitiru][input][recorder]")
{
	mitiru::InputRecorder recorder;
	recorder.beginRecording(42, 30);
	CHECK(recorder.isRecording());
}

TEST_CASE("InputRecorder - recordFrame stores frame data", "[mitiru][input][recorder]")
{
	mitiru::InputRecorder recorder;
	recorder.beginRecording(100, 60);

	std::vector<mitiru::InputCommand> cmds1;
	cmds1.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 65,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});
	recorder.recordFrame(0, cmds1);
	CHECK(recorder.recordedFrameCount() == 1);

	std::vector<mitiru::InputCommand> cmds2;
	cmds2.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::MouseMove,
		.keyCode = 0,
		.mouseButton = 0,
		.mouseX = 100.0f,
		.mouseY = 200.0f
	});
	recorder.recordFrame(1, cmds2);
	CHECK(recorder.recordedFrameCount() == 2);
}

TEST_CASE("InputRecorder - recordFrame is ignored when not recording", "[mitiru][input][recorder]")
{
	mitiru::InputRecorder recorder;
	std::vector<mitiru::InputCommand> cmds;
	cmds.push_back(mitiru::InputCommand{});
	recorder.recordFrame(0, cmds);
	CHECK(recorder.recordedFrameCount() == 0);
}

TEST_CASE("InputRecorder - endRecording returns correct ReplayData", "[mitiru][input][recorder]")
{
	mitiru::InputRecorder recorder;
	recorder.beginRecording(42, 30);

	std::vector<mitiru::InputCommand> cmds;
	cmds.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 65,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});
	recorder.recordFrame(0, cmds);
	recorder.recordFrame(1, {});

	const auto data = recorder.endRecording();

	CHECK_FALSE(recorder.isRecording());
	CHECK(data.seed == 42);
	CHECK(data.tps == 30);
	CHECK(data.totalFrames() == 2);
	CHECK(data.frames[0].frameNumber == 0);
	CHECK(data.frames[0].commands.size() == 1);
	CHECK(data.frames[0].commands[0].keyCode == 65);
	CHECK(data.frames[1].frameNumber == 1);
	CHECK(data.frames[1].commands.empty());
}

TEST_CASE("InputRecorder - multiple recording sessions are independent", "[mitiru][input][recorder]")
{
	mitiru::InputRecorder recorder;

	/// First session
	recorder.beginRecording(1, 60);
	recorder.recordFrame(0, {mitiru::InputCommand{}});
	const auto data1 = recorder.endRecording();
	CHECK(data1.totalFrames() == 1);

	/// Second session starts fresh
	recorder.beginRecording(2, 30);
	CHECK(recorder.recordedFrameCount() == 0);
	const auto data2 = recorder.endRecording();
	CHECK(data2.seed == 2);
	CHECK(data2.tps == 30);
	CHECK(data2.totalFrames() == 0);
}

// ============================================================
// ReplayData JSON tests
// ============================================================

TEST_CASE("ReplayData - toJson produces valid JSON structure", "[mitiru][input][replaydata]")
{
	mitiru::ReplayData data;
	data.seed = 12345;
	data.tps = 30;

	const auto json = data.toJson();
	CHECK(json.find("\"seed\":12345") != std::string::npos);
	CHECK(json.find("\"tps\":30") != std::string::npos);
	CHECK(json.find("\"frames\":[]") != std::string::npos);
}

TEST_CASE("ReplayData - toJson includes frame data", "[mitiru][input][replaydata]")
{
	mitiru::ReplayData data;
	data.seed = 1;
	data.tps = 60;

	mitiru::InputFrame frame;
	frame.frameNumber = 5;
	frame.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 65,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});
	data.frames.push_back(frame);

	const auto json = data.toJson();
	CHECK(json.find("\"frame\":5") != std::string::npos);
	CHECK(json.find("\"keyCode\":65") != std::string::npos);
}

TEST_CASE("ReplayData - fromJson roundtrip preserves seed and tps", "[mitiru][input][replaydata]")
{
	mitiru::ReplayData original;
	original.seed = 99999;
	original.tps = 120;

	const auto json = original.toJson();
	const auto restored = mitiru::ReplayData::fromJson(json);

	CHECK(restored.seed == original.seed);
	CHECK(restored.tps == original.tps);
}

TEST_CASE("ReplayData - fromJson handles missing fields gracefully", "[mitiru][input][replaydata]")
{
	const auto data = mitiru::ReplayData::fromJson("{}");
	/// Default values when fields are missing
	CHECK(data.seed == 0);
	CHECK(data.tps == 60);
	CHECK(data.totalFrames() == 0);
}

// ============================================================
// InputFrame JSON tests
// ============================================================

TEST_CASE("InputFrame - toJson produces valid JSON", "[mitiru][input][inputframe]")
{
	mitiru::InputFrame frame;
	frame.frameNumber = 42;
	frame.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::MouseMove,
		.keyCode = 0,
		.mouseButton = 0,
		.mouseX = 10.5f,
		.mouseY = 20.5f
	});

	const auto json = frame.toJson();
	CHECK(json.find("\"frame\":42") != std::string::npos);
	CHECK(json.find("\"commands\":[") != std::string::npos);
	CHECK(json.find("\"mouseX\":10") != std::string::npos);
}

// ============================================================
// InputReplayer tests
// ============================================================

TEST_CASE("InputReplayer - initial state", "[mitiru][input][replayer]")
{
	mitiru::InputReplayer replayer;
	CHECK(replayer.totalFrames() == 0);
	CHECK(replayer.isFinished() == false);
	CHECK(replayer.currentIndex() == 0);
}

TEST_CASE("InputReplayer - load sets up replay data", "[mitiru][input][replayer]")
{
	mitiru::InputReplayer replayer;

	mitiru::ReplayData data;
	data.seed = 42;
	data.tps = 30;

	mitiru::InputFrame f0;
	f0.frameNumber = 0;
	f0.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 65,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});

	mitiru::InputFrame f1;
	f1.frameNumber = 1;
	f1.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyUp,
		.keyCode = 65,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});

	data.frames.push_back(f0);
	data.frames.push_back(f1);

	replayer.load(data);

	CHECK(replayer.totalFrames() == 2);
	CHECK(replayer.seed() == 42);
	CHECK(replayer.tps() == 30);
	CHECK_FALSE(replayer.isFinished());
	CHECK(replayer.currentIndex() == 0);
}

TEST_CASE("InputReplayer - getCommandsForFrame returns correct commands", "[mitiru][input][replayer]")
{
	mitiru::InputReplayer replayer;

	mitiru::ReplayData data;
	mitiru::InputFrame f0;
	f0.frameNumber = 10;
	f0.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::KeyDown,
		.keyCode = 87,
		.mouseButton = 0,
		.mouseX = 0.0f,
		.mouseY = 0.0f
	});
	data.frames.push_back(f0);

	mitiru::InputFrame f1;
	f1.frameNumber = 20;
	f1.commands.push_back(mitiru::InputCommand{
		.type = mitiru::InputCommandType::MouseMove,
		.keyCode = 0,
		.mouseButton = 0,
		.mouseX = 100.0f,
		.mouseY = 200.0f
	});
	data.frames.push_back(f1);

	replayer.load(data);

	/// Query frame 10 - should return KeyDown W
	const auto cmds10 = replayer.getCommandsForFrame(10);
	REQUIRE(cmds10.size() == 1);
	CHECK(cmds10[0].type == mitiru::InputCommandType::KeyDown);
	CHECK(cmds10[0].keyCode == 87);

	/// Query frame 20 - should return MouseMove
	const auto cmds20 = replayer.getCommandsForFrame(20);
	REQUIRE(cmds20.size() == 1);
	CHECK(cmds20[0].type == mitiru::InputCommandType::MouseMove);
	CHECK(cmds20[0].mouseX == Catch::Approx(100.0f));

	/// Query non-existent frame - should return empty
	const auto cmds99 = replayer.getCommandsForFrame(99);
	CHECK(cmds99.empty());
}

TEST_CASE("InputReplayer - consumeNextFrame returns frames in order", "[mitiru][input][replayer]")
{
	mitiru::InputReplayer replayer;

	mitiru::ReplayData data;
	for (int i = 0; i < 3; ++i)
	{
		mitiru::InputFrame f;
		f.frameNumber = static_cast<std::uint64_t>(i);
		f.commands.push_back(mitiru::InputCommand{
			.type = mitiru::InputCommandType::KeyDown,
			.keyCode = 65 + i,
			.mouseButton = 0,
			.mouseX = 0.0f,
			.mouseY = 0.0f
		});
		data.frames.push_back(f);
	}
	replayer.load(data);

	CHECK_FALSE(replayer.isFinished());

	const auto c0 = replayer.consumeNextFrame();
	REQUIRE(c0.size() == 1);
	CHECK(c0[0].keyCode == 65);
	CHECK(replayer.currentIndex() == 1);

	const auto c1 = replayer.consumeNextFrame();
	REQUIRE(c1.size() == 1);
	CHECK(c1[0].keyCode == 66);

	const auto c2 = replayer.consumeNextFrame();
	REQUIRE(c2.size() == 1);
	CHECK(c2[0].keyCode == 67);
	CHECK(replayer.isFinished());

	/// Past end returns empty
	const auto c3 = replayer.consumeNextFrame();
	CHECK(c3.empty());
}

TEST_CASE("InputReplayer - reset restores to beginning", "[mitiru][input][replayer]")
{
	mitiru::InputReplayer replayer;

	mitiru::ReplayData data;
	mitiru::InputFrame f;
	f.frameNumber = 0;
	f.commands.push_back(mitiru::InputCommand{});
	data.frames.push_back(f);
	replayer.load(data);

	/// Consume to end
	replayer.consumeNextFrame();
	CHECK(replayer.isFinished());

	/// Reset
	replayer.reset();
	CHECK_FALSE(replayer.isFinished());
	CHECK(replayer.currentIndex() == 0);

	/// Can consume again
	const auto cmds = replayer.consumeNextFrame();
	CHECK(cmds.size() == 1);
}

TEST_CASE("InputReplayer - totalFrames matches loaded data", "[mitiru][input][replayer]")
{
	mitiru::InputReplayer replayer;

	mitiru::ReplayData data;
	for (int i = 0; i < 5; ++i)
	{
		mitiru::InputFrame f;
		f.frameNumber = static_cast<std::uint64_t>(i);
		data.frames.push_back(f);
	}
	replayer.load(data);

	CHECK(replayer.totalFrames() == 5);
}

TEST_CASE("InputReplayer - load resets previous state", "[mitiru][input][replayer]")
{
	mitiru::InputReplayer replayer;

	/// Load first data
	mitiru::ReplayData data1;
	data1.seed = 1;
	mitiru::InputFrame f;
	f.frameNumber = 0;
	data1.frames.push_back(f);
	replayer.load(data1);
	replayer.consumeNextFrame();
	CHECK(replayer.isFinished());

	/// Load second data - should reset
	mitiru::ReplayData data2;
	data2.seed = 2;
	data2.frames.push_back(f);
	data2.frames.push_back(f);
	replayer.load(data2);

	CHECK_FALSE(replayer.isFinished());
	CHECK(replayer.currentIndex() == 0);
	CHECK(replayer.totalFrames() == 2);
	CHECK(replayer.seed() == 2);
}
