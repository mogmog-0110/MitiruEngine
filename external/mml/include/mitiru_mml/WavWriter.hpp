#pragma once
/// @file WavWriter.hpp
/// @brief PCMバッファからメモリ上WAVを生成する

#include <mitiru_mml/MmlTypes.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mitiru_mml
{

/// @brief メモリ上WAV生成器
class WavWriter
{
public:
	/// @brief PCMバッファからWAVバイト列を生成する
	/// @param pcm 16bit モノラルPCMデータ
	/// @param sampleRate サンプルレート
	/// @return WAVファイルのバイト列
	[[nodiscard]] static std::vector<std::uint8_t> toWav(
		const PcmBuffer& pcm,
		std::uint32_t sampleRate = 44100)
	{
		constexpr std::uint16_t CHANNELS = 1;
		constexpr std::uint16_t BITS = 16;
		const auto dataSize = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
		const std::uint32_t fileSize = 44 + dataSize;

		std::vector<std::uint8_t> wav(fileSize);
		auto* p = wav.data();

		// RIFFヘッダー
		std::memcpy(p, "RIFF", 4); p += 4;
		write32(p, fileSize - 8); p += 4;
		std::memcpy(p, "WAVE", 4); p += 4;

		// fmtチャンク
		std::memcpy(p, "fmt ", 4); p += 4;
		write32(p, 16); p += 4;
		write16(p, 1); p += 2; // PCM
		write16(p, CHANNELS); p += 2;
		write32(p, sampleRate); p += 4;
		write32(p, sampleRate * CHANNELS * (BITS / 8)); p += 4;
		write16(p, CHANNELS * (BITS / 8)); p += 2;
		write16(p, BITS); p += 2;

		// dataチャンク
		std::memcpy(p, "data", 4); p += 4;
		write32(p, dataSize); p += 4;

		// PCMデータ（リトルエンディアン16bit）
		for (const auto sample : pcm)
		{
			write16(p, static_cast<std::uint16_t>(sample));
			p += 2;
		}

		return wav;
	}

	/// @brief WAVの有効性を簡易チェックする
	/// @param wav WAVバイト列
	/// @return 有効ならtrue
	[[nodiscard]] static bool isValidWav(const std::vector<std::uint8_t>& wav) noexcept
	{
		if (wav.size() < 44) return false;
		return wav[0] == 'R' && wav[1] == 'I' && wav[2] == 'F' && wav[3] == 'F'
			&& wav[8] == 'W' && wav[9] == 'A' && wav[10] == 'V' && wav[11] == 'E';
	}

private:
	static void write32(std::uint8_t* dst, std::uint32_t v) noexcept
	{
		dst[0] = static_cast<std::uint8_t>(v);
		dst[1] = static_cast<std::uint8_t>(v >> 8);
		dst[2] = static_cast<std::uint8_t>(v >> 16);
		dst[3] = static_cast<std::uint8_t>(v >> 24);
	}
	static void write16(std::uint8_t* dst, std::uint16_t v) noexcept
	{
		dst[0] = static_cast<std::uint8_t>(v);
		dst[1] = static_cast<std::uint8_t>(v >> 8);
	}
};

} // namespace mitiru_mml
