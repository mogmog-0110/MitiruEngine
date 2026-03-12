#include <catch2/catch_test_macros.hpp>

#include <mitiru/platform/sdl2/Sdl2Input.hpp>
#include <mitiru/platform/sdl2/Sdl2Audio.hpp>

// ============================================================
// Sdl2InputConfig
// ============================================================

TEST_CASE("Sdl2InputConfig - default values", "[sdl2]")
{
	const mitiru::Sdl2InputConfig config;

	REQUIRE(config.enableKeyboard == true);
	REQUIRE(config.enableMouse == true);
	REQUIRE(config.enableGamepad == true);
	REQUIRE(config.gamepadDeadzone == 8000);
	REQUIRE(config.enableTextInput == false);
	REQUIRE(config.mouseSensitivity == 1.0f);
}

TEST_CASE("Sdl2InputConfig - defaults factory", "[sdl2]")
{
	const auto config = mitiru::Sdl2InputConfig::defaults();

	REQUIRE(config.enableKeyboard == true);
	REQUIRE(config.enableMouse == true);
}

// ============================================================
// GamepadState
// ============================================================

TEST_CASE("GamepadState - default state", "[sdl2]")
{
	const mitiru::GamepadState state;

	REQUIRE(state.connected == false);
	REQUIRE(state.axes[0] == 0.0f);
	REQUIRE(state.buttons[0] == false);
}

TEST_CASE("GamepadState - isButtonJustPressed edge detection", "[sdl2]")
{
	mitiru::GamepadState state;

	/// ボタンを押す
	state.buttons[0] = true;

	/// beginFrameの前は前フレーム状態がfalseなのでエッジ検出成功
	REQUIRE(state.isButtonJustPressed(0) == true);

	/// beginFrameで前フレーム状態を更新
	state.beginFrame();

	/// 同じ状態が続いているのでエッジ検出は失敗
	REQUIRE(state.isButtonJustPressed(0) == false);
}

TEST_CASE("GamepadState - isButtonJustReleased edge detection", "[sdl2]")
{
	mitiru::GamepadState state;

	/// ボタンを押してbeginFrame
	state.buttons[0] = true;
	state.beginFrame();

	/// ボタンを離す
	state.buttons[0] = false;

	REQUIRE(state.isButtonJustReleased(0) == true);
}

TEST_CASE("GamepadState - out of range button returns false", "[sdl2]")
{
	const mitiru::GamepadState state;

	REQUIRE(state.isButtonJustPressed(-1) == false);
	REQUIRE(state.isButtonJustPressed(100) == false);
	REQUIRE(state.isButtonJustReleased(-1) == false);
	REQUIRE(state.isButtonJustReleased(100) == false);
}

// ============================================================
// MouseWheelState
// ============================================================

TEST_CASE("MouseWheelState - default values", "[sdl2]")
{
	const mitiru::MouseWheelState state;

	REQUIRE(state.scrollX == 0.0f);
	REQUIRE(state.scrollY == 0.0f);
}

TEST_CASE("MouseWheelState - reset", "[sdl2]")
{
	mitiru::MouseWheelState state;
	state.scrollX = 1.5f;
	state.scrollY = -2.0f;

	state.reset();

	REQUIRE(state.scrollX == 0.0f);
	REQUIRE(state.scrollY == 0.0f);
}

// ============================================================
// AudioDeviceConfig
// ============================================================

TEST_CASE("AudioDeviceConfig - default values", "[sdl2]")
{
	const mitiru::AudioDeviceConfig config;

	REQUIRE(config.sampleRate == 44100);
	REQUIRE(config.channels == 2);
	REQUIRE(config.format == mitiru::AudioSampleFormat::Float32);
	REQUIRE(config.bufferSize == 4096);
	REQUIRE(config.deviceName.empty());
}

TEST_CASE("AudioDeviceConfig - defaults factory", "[sdl2]")
{
	const auto config = mitiru::AudioDeviceConfig::defaults();

	REQUIRE(config.sampleRate == 44100);
	REQUIRE(config.channels == 2);
}

TEST_CASE("AudioDeviceConfig - lowLatency factory", "[sdl2]")
{
	const auto config = mitiru::AudioDeviceConfig::lowLatency();

	REQUIRE(config.sampleRate == 48000);
	REQUIRE(config.bufferSize == 512);
}

TEST_CASE("AudioDeviceConfig - bytesPerSample Int16", "[sdl2]")
{
	mitiru::AudioDeviceConfig config;
	config.format = mitiru::AudioSampleFormat::Int16;

	REQUIRE(config.bytesPerSample() == 2);
}

TEST_CASE("AudioDeviceConfig - bytesPerSample Float32", "[sdl2]")
{
	mitiru::AudioDeviceConfig config;
	config.format = mitiru::AudioSampleFormat::Float32;

	REQUIRE(config.bytesPerSample() == 4);
}

TEST_CASE("AudioDeviceConfig - bufferSizeBytes stereo float32", "[sdl2]")
{
	mitiru::AudioDeviceConfig config;
	config.bufferSize = 1024;
	config.channels = 2;
	config.format = mitiru::AudioSampleFormat::Float32;

	/// 1024 samples * 2 channels * 4 bytes = 8192
	REQUIRE(config.bufferSizeBytes() == 8192);
}

TEST_CASE("AudioDeviceConfig - bufferSizeBytes mono int16", "[sdl2]")
{
	mitiru::AudioDeviceConfig config;
	config.bufferSize = 512;
	config.channels = 1;
	config.format = mitiru::AudioSampleFormat::Int16;

	/// 512 samples * 1 channel * 2 bytes = 1024
	REQUIRE(config.bufferSizeBytes() == 1024);
}

// ============================================================
// ObtainedAudioFormat
// ============================================================

TEST_CASE("ObtainedAudioFormat - default is invalid", "[sdl2]")
{
	const mitiru::ObtainedAudioFormat format;

	REQUIRE(format.isValid() == false);
}

TEST_CASE("ObtainedAudioFormat - valid when populated", "[sdl2]")
{
	mitiru::ObtainedAudioFormat format;
	format.sampleRate = 44100;
	format.channels = 2;

	REQUIRE(format.isValid() == true);
}
