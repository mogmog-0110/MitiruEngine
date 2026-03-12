/// @file TestWin32AudioConfig.cpp
/// @brief Win32AudioOutput関連のユニットテスト（プラットフォーム非依存部分）
/// @details AudioOutputConfig構造体のバッファサイズ計算テスト。
///          実waveOut APIは使用しない。

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdint>

// ============================================================
// AudioOutputConfig (platform-independent struct) reimplemented
// for testing without Win32 headers
// ============================================================

namespace test_audio
{

/// @brief テスト用AudioOutputConfig（Win32AudioOutput.hppと同一ロジック）
struct AudioOutputConfig
{
	std::uint32_t sampleRate = 44100;
	std::uint16_t channels = 2;
	std::uint16_t bitsPerSample = 16;

	[[nodiscard]] constexpr std::uint32_t bytesPerSample() const noexcept
	{
		return static_cast<std::uint32_t>(channels) *
			(static_cast<std::uint32_t>(bitsPerSample) / 8u);
	}

	[[nodiscard]] constexpr std::uint32_t bytesPerSecond() const noexcept
	{
		return sampleRate * bytesPerSample();
	}

	[[nodiscard]] constexpr std::uint32_t bufferSizeForMs(std::uint32_t milliseconds) const noexcept
	{
		return (bytesPerSecond() * milliseconds) / 1000u;
	}
};

} // namespace test_audio

// ============================================================
// bytesPerSample tests
// ============================================================

TEST_CASE("AudioConfig - bytesPerSample for stereo 16bit", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.channels = 2;
	config.bitsPerSample = 16;

	CHECK(config.bytesPerSample() == 4);
}

TEST_CASE("AudioConfig - bytesPerSample for mono 8bit", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.channels = 1;
	config.bitsPerSample = 8;

	CHECK(config.bytesPerSample() == 1);
}

TEST_CASE("AudioConfig - bytesPerSample for stereo 24bit", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.channels = 2;
	config.bitsPerSample = 24;

	CHECK(config.bytesPerSample() == 6);
}

TEST_CASE("AudioConfig - bytesPerSample for 5.1ch 16bit", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.channels = 6;
	config.bitsPerSample = 16;

	CHECK(config.bytesPerSample() == 12);
}

// ============================================================
// bytesPerSecond tests
// ============================================================

TEST_CASE("AudioConfig - bytesPerSecond for CD quality", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.sampleRate = 44100;
	config.channels = 2;
	config.bitsPerSample = 16;

	/// CD quality: 44100 * 2 * 2 = 176400 bytes/sec
	CHECK(config.bytesPerSecond() == 176400u);
}

TEST_CASE("AudioConfig - bytesPerSecond for 48kHz stereo 16bit", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.sampleRate = 48000;
	config.channels = 2;
	config.bitsPerSample = 16;

	CHECK(config.bytesPerSecond() == 192000u);
}

TEST_CASE("AudioConfig - bytesPerSecond for mono 8bit 22050Hz", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.sampleRate = 22050;
	config.channels = 1;
	config.bitsPerSample = 8;

	CHECK(config.bytesPerSecond() == 22050u);
}

// ============================================================
// bufferSizeForMs tests
// ============================================================

TEST_CASE("AudioConfig - bufferSizeForMs for 10ms at CD quality", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.sampleRate = 44100;
	config.channels = 2;
	config.bitsPerSample = 16;

	/// 176400 * 10 / 1000 = 1764 bytes
	CHECK(config.bufferSizeForMs(10) == 1764u);
}

TEST_CASE("AudioConfig - bufferSizeForMs for 100ms at CD quality", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.sampleRate = 44100;
	config.channels = 2;
	config.bitsPerSample = 16;

	/// 176400 * 100 / 1000 = 17640 bytes
	CHECK(config.bufferSizeForMs(100) == 17640u);
}

TEST_CASE("AudioConfig - bufferSizeForMs for 1 second", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	config.sampleRate = 44100;
	config.channels = 2;
	config.bitsPerSample = 16;

	CHECK(config.bufferSizeForMs(1000) == 176400u);
}

TEST_CASE("AudioConfig - bufferSizeForMs for 0ms returns 0", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;
	CHECK(config.bufferSizeForMs(0) == 0u);
}

// ============================================================
// Default config tests
// ============================================================

TEST_CASE("AudioConfig - default values", "[mitiru][audio][config]")
{
	test_audio::AudioOutputConfig config;

	CHECK(config.sampleRate == 44100u);
	CHECK(config.channels == 2);
	CHECK(config.bitsPerSample == 16);
}

// ============================================================
// Volume calculation tests
// ============================================================

TEST_CASE("AudioConfig - volume to waveOut DWORD conversion", "[mitiru][audio][config]")
{
	/// [0.0, 1.0] → waveOutSetVolume DWORD (L16|R16)
	auto volumeToDword = [](float volume) -> std::uint32_t
	{
		const float clamped = (volume < 0.0f) ? 0.0f : (volume > 1.0f) ? 1.0f : volume;
		const auto level = static_cast<std::uint32_t>(clamped * 0xFFFF);
		return (level & 0xFFFF) | ((level & 0xFFFF) << 16);
	};

	CHECK(volumeToDword(0.0f) == 0x00000000u);
	CHECK(volumeToDword(1.0f) == 0xFFFFFFFFu);

	/// 中間値: 0x7FFF | (0x7FFF << 16) = 0x7FFF7FFF
	const auto half = volumeToDword(0.5f);
	CHECK((half & 0xFFFF) == (half >> 16));  ///< 左右同一
	CHECK((half & 0xFFFF) > 0x7F00u);       ///< 約50%
	CHECK((half & 0xFFFF) < 0x8100u);

	/// クランプ
	CHECK(volumeToDword(-1.0f) == 0x00000000u);
	CHECK(volumeToDword(2.0f) == 0xFFFFFFFFu);
}

// ============================================================
// Double buffering logic tests
// ============================================================

TEST_CASE("AudioConfig - double buffer index cycling", "[mitiru][audio][config]")
{
	/// NUM_BUFFERS = 2 のダブルバッファリングのインデックス循環テスト
	constexpr int NUM_BUFFERS = 2;
	int currentBuffer = 0;

	CHECK(currentBuffer == 0);

	currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
	CHECK(currentBuffer == 1);

	currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
	CHECK(currentBuffer == 0);

	currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
	CHECK(currentBuffer == 1);
}
