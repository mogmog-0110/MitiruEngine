#include <catch2/catch_test_macros.hpp>
#include <sgc/replay/ReplaySystem.hpp>

using namespace sgc::replay;

TEST_CASE("ReplayRecorder - starts in non-recording state", "[replay]")
{
	ReplayRecorder recorder;
	REQUIRE_FALSE(recorder.isRecording());
}

TEST_CASE("ReplayRecorder - startRecording sets state", "[replay]")
{
	ReplayRecorder recorder;
	recorder.startRecording(12345);
	REQUIRE(recorder.isRecording());
	REQUIRE(recorder.currentFrame() == 0);
}

TEST_CASE("ReplayRecorder - recordFrame increments frame number", "[replay]")
{
	ReplayRecorder recorder;
	recorder.startRecording();
	recorder.recordFrame({KeyEvent{65, true}});
	REQUIRE(recorder.currentFrame() == 1);
	recorder.recordFrame({MouseEvent{10, 20, 0, true}});
	REQUIRE(recorder.currentFrame() == 2);
}

TEST_CASE("ReplayRecorder - recordFrame fails when not recording", "[replay]")
{
	ReplayRecorder recorder;
	REQUIRE_FALSE(recorder.recordFrame({KeyEvent{65, true}}));
}

TEST_CASE("ReplayRecorder - stopRecording returns data", "[replay]")
{
	ReplayRecorder recorder;
	recorder.startRecording(999);
	recorder.recordFrame({KeyEvent{65, true}});
	recorder.recordFrame({MouseEvent{10, 20, 0, false}});
	const auto data = recorder.stopRecording();

	REQUIRE_FALSE(recorder.isRecording());
	REQUIRE(data.metadata.timestamp == 999);
	REQUIRE(data.frameCount() == 2);
	REQUIRE(data.frames[0].frameNumber == 0);
	REQUIRE(data.frames[1].frameNumber == 1);
}

TEST_CASE("ReplayRecorder - multiple events per frame", "[replay]")
{
	ReplayRecorder recorder;
	recorder.startRecording();

	std::vector<InputEvent> events;
	events.push_back(KeyEvent{65, true});
	events.push_back(MouseEvent{100, 200, 0, true});
	events.push_back(CustomEvent{"action", "jump"});
	recorder.recordFrame(events);

	const auto data = recorder.stopRecording();
	REQUIRE(data.frames[0].events.size() == 3);

	REQUIRE(std::holds_alternative<KeyEvent>(data.frames[0].events[0]));
	REQUIRE(std::holds_alternative<MouseEvent>(data.frames[0].events[1]));
	REQUIRE(std::holds_alternative<CustomEvent>(data.frames[0].events[2]));
}

TEST_CASE("ReplayPlayer - load and playback", "[replay]")
{
	ReplayData data;
	data.metadata.version = 1;
	data.frames.push_back(InputFrame{0, {KeyEvent{65, true}}});
	data.frames.push_back(InputFrame{1, {KeyEvent{65, false}}});

	ReplayPlayer player;
	player.load(data);

	REQUIRE(player.totalFrames() == 2);
	REQUIRE_FALSE(player.isFinished());

	const auto frame0 = player.nextFrame();
	REQUIRE(frame0.has_value());
	REQUIRE(frame0->frameNumber == 0);

	const auto frame1 = player.nextFrame();
	REQUIRE(frame1.has_value());
	REQUIRE(frame1->frameNumber == 1);

	REQUIRE(player.isFinished());
	REQUIRE_FALSE(player.nextFrame().has_value());
}

TEST_CASE("ReplayPlayer - reset restores playback position", "[replay]")
{
	ReplayData data;
	data.frames.push_back(InputFrame{0, {}});
	data.frames.push_back(InputFrame{1, {}});

	ReplayPlayer player;
	player.load(data);
	player.nextFrame();
	player.nextFrame();
	REQUIRE(player.isFinished());

	player.reset();
	REQUIRE_FALSE(player.isFinished());
	REQUIRE(player.currentIndex() == 0);

	const auto frame = player.nextFrame();
	REQUIRE(frame.has_value());
	REQUIRE(frame->frameNumber == 0);
}

TEST_CASE("ReplayPlayer - empty data is immediately finished", "[replay]")
{
	ReplayPlayer player;
	player.load(ReplayData{});
	REQUIRE(player.isFinished());
	REQUIRE(player.totalFrames() == 0);
}

TEST_CASE("ReplayPlayer - metadata is preserved", "[replay]")
{
	ReplayData data;
	data.metadata.version = 2;
	data.metadata.timestamp = 1234567890;
	data.metadata.description = "test replay";

	ReplayPlayer player;
	player.load(data);
	REQUIRE(player.metadata().version == 2);
	REQUIRE(player.metadata().timestamp == 1234567890);
	REQUIRE(player.metadata().description == "test replay");
}

TEST_CASE("ReplayRecorder - empty frame recording", "[replay]")
{
	ReplayRecorder recorder;
	recorder.startRecording();
	REQUIRE(recorder.recordEmptyFrame());
	const auto data = recorder.stopRecording();
	REQUIRE(data.frameCount() == 1);
	REQUIRE(data.frames[0].events.empty());
}
