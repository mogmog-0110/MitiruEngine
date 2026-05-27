#pragma once
/// @file WavReader.hpp
/// @brief WAV (RIFF/WAVE) を読み込み 16bit モノラル PCM にする。
/// @details SFZ サウンドフォントが参照する外部 WAV サンプルの読み込み用(#9)。
///          PCM 16bit のみ対応（ステレオはモノにダウンミックス）。float/24bit 等は非対応。
///          ネイティブサンプルレートは保持し、リサンプルは MultiSampleInstrument 側に委ねる。
///
/// @code
/// auto r = mitiru_mml::WavReader::fromFile("kick.wav");
/// if (r.ok) { /* r.pcm, r.sampleRate */ }
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief WAV 読み込み（16bit PCM / モノ化）
class WavReader
{
public:
	struct Result { PcmBuffer pcm; std::uint32_t sampleRate = 44100; bool ok = false; };

	[[nodiscard]] static Result fromFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs) return {};
		std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(ifs)), {});
		return fromMemory(buf.data(), buf.size());
	}

	[[nodiscard]] static Result fromMemory(const std::uint8_t* data, std::size_t size)
	{
		Result r;
		if (!data || size < 12) return r;
		if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return r;

		std::uint16_t channels = 1, bits = 16, fmt = 1;
		std::uint32_t rate = 44100;
		const std::uint8_t* dataChunk = nullptr;
		std::uint32_t dataSize = 0;

		const std::uint8_t* p = data + 12;
		const std::uint8_t* end = data + size;
		while (p + 8 <= end)
		{
			const std::uint32_t csize = rd32(p + 4);
			const std::uint8_t* body = p + 8;
			if (body + csize > end) break;
			if (std::memcmp(p, "fmt ", 4) == 0 && csize >= 16)
			{
				fmt = rd16(body); channels = rd16(body + 2); rate = rd32(body + 4);
				bits = rd16(body + 14);
			}
			else if (std::memcmp(p, "data", 4) == 0) { dataChunk = body; dataSize = csize; }
			p = body + csize + (csize & 1);
		}
		if (!dataChunk || fmt != 1 || bits != 16 || channels < 1) return r; // PCM16 のみ

		const std::uint32_t frames = dataSize / (2u * channels);
		r.pcm.resize(frames);
		for (std::uint32_t f = 0; f < frames; ++f)
		{
			int acc = 0;
			for (std::uint16_t c = 0; c < channels; ++c)
				acc += static_cast<std::int16_t>(rd16(dataChunk + (f * channels + c) * 2));
			r.pcm[f] = static_cast<std::int16_t>(acc / channels); // モノダウンミックス
		}
		r.sampleRate = rate;
		r.ok = true;
		return r;
	}

private:
	static std::uint16_t rd16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
	static std::uint32_t rd32(const std::uint8_t* p)
	{ return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
	         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
};

} // namespace mitiru_mml
