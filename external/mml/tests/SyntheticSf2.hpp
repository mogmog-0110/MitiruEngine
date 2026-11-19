#pragma once
/// @file SyntheticSf2.hpp
/// @brief 検査とデモ用に、小さな SF2 をコードで作る (外部のサウンドフォントを使わない)
/// @details 中身は次の 3 プリセット。サンプルはすべて決まった式から作るので、バイト列は毎回同じ。
///          - バンク 0 プログラム 0 「Sine Keys」: 441 Hz の正弦波 100 サンプルを繰り返したループ (原音は A4)
///          - バンク 0 プログラム 33 「Saw Bass」: 441 Hz の鋸歯波のループ (原音は A4)
///          - バンク 128 プログラム 0 「Test Kit」: キック (36)、スネア (38)、クローズドハイハット (42)、クラップ (39) の 1 発もの

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mitiru_mml_test
{

namespace sf2_detail
{

inline void put16(std::vector<std::uint8_t>& b, std::uint32_t v)
{
	b.push_back(static_cast<std::uint8_t>(v));
	b.push_back(static_cast<std::uint8_t>(v >> 8));
}

inline void put32(std::vector<std::uint8_t>& b, std::uint32_t v)
{
	put16(b, v & 0xFFFFu);
	put16(b, v >> 16);
}

inline void putName(std::vector<std::uint8_t>& b, const std::string& name)
{
	for (std::size_t i = 0; i < 20; ++i)
	{
		b.push_back(i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0);
	}
}

inline void putTag(std::vector<std::uint8_t>& b, const char* tag)
{
	b.insert(b.end(), tag, tag + 4);
}

/// @brief チャンクを書く (奇数サイズなら 1 バイトのパディング)
inline void putChunk(std::vector<std::uint8_t>& out, const char* tag, const std::vector<std::uint8_t>& body)
{
	putTag(out, tag);
	put32(out, static_cast<std::uint32_t>(body.size()));
	out.insert(out.end(), body.begin(), body.end());
	if ((body.size() & 1) != 0)
	{
		out.push_back(0);
	}
}

inline std::vector<std::uint8_t> listChunk(const char* type, const std::vector<std::uint8_t>& inner)
{
	std::vector<std::uint8_t> body;
	putTag(body, type);
	body.insert(body.end(), inner.begin(), inner.end());
	std::vector<std::uint8_t> out;
	putChunk(out, "LIST", body);
	return out;
}

struct SampleDef
{
	std::string name;
	std::vector<std::int16_t> pcm;
	std::uint32_t loopStart = 0;
	std::uint32_t loopEnd = 0;
	int pitch = 69;
	int correction = 0;
};

struct ZoneDef
{
	int sample = 0;
	int keyLo = 0;
	int keyHi = 127;
	bool looped = false;
	int rootKey = -1;
	int attackTc = -12000;
	int releaseTc = -12000;
};

struct InstDef
{
	std::string name;
	std::vector<ZoneDef> zones;
};

struct PresetDef
{
	std::string name;
	int program = 0;
	int bank = 0;
	int instrument = 0;
};

} // namespace sf2_detail

/// @brief 検査用の SF2 を作る
[[nodiscard]] inline std::vector<std::uint8_t> buildSyntheticSf2()
{
	using namespace sf2_detail;
	constexpr double PI = 3.14159265358979323846;
	constexpr double RATE = 44100.0;

	std::vector<SampleDef> samples;
	{
		SampleDef sine;
		sine.name = "Sine441";
		for (int i = 0; i < 2000; ++i)
		{
			sine.pcm.push_back(static_cast<std::int16_t>(std::lround(20000.0 * std::sin(2.0 * PI * static_cast<double>(i) / 100.0))));
		}
		sine.loopStart = 0;
		sine.loopEnd = 2000;
		sine.pitch = 69;
		sine.correction = -4;
		samples.push_back(sine);

		SampleDef saw;
		saw.name = "Saw441";
		for (int i = 0; i < 2000; ++i)
		{
			saw.pcm.push_back(static_cast<std::int16_t>(std::lround(16000.0 * (2.0 * static_cast<double>(i % 100) / 100.0 - 1.0))));
		}
		saw.loopStart = 0;
		saw.loopEnd = 2000;
		saw.pitch = 69;
		saw.correction = -4;
		samples.push_back(saw);

		SampleDef kick;
		kick.name = "Kick";
		double phase = 0.0;
		for (int i = 0; i < 13230; ++i)
		{
			const double t = static_cast<double>(i) / RATE;
			const double freq = 45.0 + 120.0 * std::exp(-t * 28.0);
			phase += 2.0 * PI * freq / RATE;
			kick.pcm.push_back(static_cast<std::int16_t>(std::lround(30000.0 * std::exp(-t * 11.0) * std::sin(phase))));
		}
		kick.pitch = 36;
		samples.push_back(kick);

		std::uint32_t state = 12345u;
		const auto noise = [&state]()
		{
			state = state * 1664525u + 1013904223u;
			return static_cast<double>(static_cast<std::int32_t>(state)) / 2147483648.0;
		};

		SampleDef snare;
		snare.name = "Snare";
		for (int i = 0; i < 8820; ++i)
		{
			const double t = static_cast<double>(i) / RATE;
			const double body = 0.5 * std::sin(2.0 * PI * 190.0 * t) * std::exp(-t * 28.0);
			snare.pcm.push_back(static_cast<std::int16_t>(std::lround(26000.0 * (0.7 * noise() * std::exp(-t * 18.0) + body))));
		}
		snare.pitch = 38;
		samples.push_back(snare);

		SampleDef hat;
		hat.name = "Hat";
		double prev = 0.0;
		for (int i = 0; i < 3528; ++i)
		{
			const double t = static_cast<double>(i) / RATE;
			const double n = noise();
			const double hp = n - prev; // 差分で高域だけにする
			prev = n;
			hat.pcm.push_back(static_cast<std::int16_t>(std::lround(14000.0 * hp * std::exp(-t * 55.0))));
		}
		hat.pitch = 42;
		samples.push_back(hat);

		SampleDef clap;
		clap.name = "Clap";
		for (int i = 0; i < 6615; ++i)
		{
			const double t = static_cast<double>(i) / RATE;
			double env = std::exp(-t * 22.0);
			if (t < 0.03)
			{
				env *= (std::fmod(t, 0.01) < 0.004) ? 1.0 : 0.4;
			}
			clap.pcm.push_back(static_cast<std::int16_t>(std::lround(20000.0 * noise() * env)));
		}
		clap.pitch = 39;
		samples.push_back(clap);
	}

	std::vector<InstDef> insts(2);
	insts[0].name = "Sine Keys";
	insts[0].zones.push_back({0, 0, 127, true, -1, -7973, -2400});
	insts[1].name = "Saw Bass";
	insts[1].zones.push_back({1, 0, 127, true, -1, -7973, -3600});
	insts.resize(3);
	insts[2].name = "Test Kit";
	insts[2].zones.push_back({2, 36, 36, false, 36, -12000, -12000});
	insts[2].zones.push_back({3, 38, 38, false, 38, -12000, -12000});
	insts[2].zones.push_back({4, 42, 42, false, 42, -12000, -12000});
	insts[2].zones.push_back({5, 39, 39, false, 39, -12000, -12000});

	const std::vector<PresetDef> presets = {
		{"Sine Keys", 0, 0, 0},
		{"Saw Bass", 33, 0, 1},
		{"Test Kit", 0, 128, 2},
	};

	// sdta: 16bit のサンプルを並べる。各サンプルの後ろに 46 個のゼロを置く (SF2 の決まり)
	std::vector<std::uint8_t> smpl;
	struct Placed
	{
		std::uint32_t start;
		std::uint32_t end;
	};
	std::vector<Placed> placed;
	std::uint32_t cursor = 0;
	for (const auto& s : samples)
	{
		placed.push_back({cursor, cursor + static_cast<std::uint32_t>(s.pcm.size())});
		for (const auto v : s.pcm)
		{
			put16(smpl, static_cast<std::uint16_t>(v));
		}
		for (int i = 0; i < 46; ++i)
		{
			put16(smpl, 0);
		}
		cursor += static_cast<std::uint32_t>(s.pcm.size()) + 46;
	}
	std::vector<std::uint8_t> sdtaInner;
	putChunk(sdtaInner, "smpl", smpl);

	// pdta
	std::vector<std::uint8_t> phdr;
	std::vector<std::uint8_t> pbag;
	std::vector<std::uint8_t> pgen;
	for (std::size_t i = 0; i < presets.size(); ++i)
	{
		putName(phdr, presets[i].name);
		put16(phdr, static_cast<std::uint32_t>(presets[i].program));
		put16(phdr, static_cast<std::uint32_t>(presets[i].bank));
		put16(phdr, static_cast<std::uint32_t>(i));
		put32(phdr, 0);
		put32(phdr, 0);
		put32(phdr, 0);
		put16(pbag, static_cast<std::uint32_t>(pgen.size() / 4));
		put16(pbag, 0);
		put16(pgen, 41);
		put16(pgen, static_cast<std::uint32_t>(presets[i].instrument));
	}
	putName(phdr, "EOP");
	put16(phdr, 0);
	put16(phdr, 0);
	put16(phdr, static_cast<std::uint32_t>(presets.size()));
	put32(phdr, 0);
	put32(phdr, 0);
	put32(phdr, 0);
	put16(pbag, static_cast<std::uint32_t>(pgen.size() / 4));
	put16(pbag, 0);
	put16(pgen, 0);
	put16(pgen, 0);

	std::vector<std::uint8_t> inst;
	std::vector<std::uint8_t> ibag;
	std::vector<std::uint8_t> igen;
	std::uint32_t bagCount = 0;
	for (const auto& in : insts)
	{
		putName(inst, in.name);
		put16(inst, bagCount);
		for (const auto& z : in.zones)
		{
			put16(ibag, static_cast<std::uint32_t>(igen.size() / 4));
			put16(ibag, 0);
			++bagCount;
			if (z.keyLo != 0 || z.keyHi != 127)
			{
				put16(igen, 43);
				put16(igen, static_cast<std::uint32_t>(z.keyLo | (z.keyHi << 8)));
			}
			put16(igen, 34);
			put16(igen, static_cast<std::uint16_t>(static_cast<std::int16_t>(z.attackTc)));
			put16(igen, 38);
			put16(igen, static_cast<std::uint16_t>(static_cast<std::int16_t>(z.releaseTc)));
			put16(igen, 54);
			put16(igen, z.looped ? 1u : 0u);
			if (z.rootKey >= 0)
			{
				put16(igen, 58);
				put16(igen, static_cast<std::uint32_t>(z.rootKey));
			}
			put16(igen, 53);
			put16(igen, static_cast<std::uint32_t>(z.sample));
		}
	}
	putName(inst, "EOI");
	put16(inst, bagCount);
	put16(ibag, static_cast<std::uint32_t>(igen.size() / 4));
	put16(ibag, 0);
	put16(igen, 0);
	put16(igen, 0);

	std::vector<std::uint8_t> shdr;
	for (std::size_t i = 0; i < samples.size(); ++i)
	{
		const auto& s = samples[i];
		putName(shdr, s.name);
		put32(shdr, placed[i].start);
		put32(shdr, placed[i].end);
		put32(shdr, placed[i].start + s.loopStart);
		put32(shdr, placed[i].start + (s.loopEnd == 0 ? static_cast<std::uint32_t>(s.pcm.size()) : s.loopEnd));
		put32(shdr, 44100);
		shdr.push_back(static_cast<std::uint8_t>(s.pitch));
		shdr.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(s.correction)));
		put16(shdr, 0);
		put16(shdr, 1);
	}
	putName(shdr, "EOS");
	for (int i = 0; i < 26; ++i)
	{
		shdr.push_back(0);
	}

	const std::vector<std::uint8_t> emptyMod(10, 0);
	std::vector<std::uint8_t> pdtaInner;
	putChunk(pdtaInner, "phdr", phdr);
	putChunk(pdtaInner, "pbag", pbag);
	putChunk(pdtaInner, "pmod", emptyMod);
	putChunk(pdtaInner, "pgen", pgen);
	putChunk(pdtaInner, "inst", inst);
	putChunk(pdtaInner, "ibag", ibag);
	putChunk(pdtaInner, "imod", emptyMod);
	putChunk(pdtaInner, "igen", igen);
	putChunk(pdtaInner, "shdr", shdr);

	std::vector<std::uint8_t> infoInner;
	{
		std::vector<std::uint8_t> ifil;
		put16(ifil, 2);
		put16(ifil, 1);
		putChunk(infoInner, "ifil", ifil);
		std::vector<std::uint8_t> name = {'s', 'y', 'n', 't', 'h', 0, 0, 0};
		putChunk(infoInner, "INAM", name);
	}

	std::vector<std::uint8_t> body;
	putTag(body, "sfbk");
	const auto info = listChunk("INFO", infoInner);
	const auto sdta = listChunk("sdta", sdtaInner);
	const auto pdta = listChunk("pdta", pdtaInner);
	body.insert(body.end(), info.begin(), info.end());
	body.insert(body.end(), sdta.begin(), sdta.end());
	body.insert(body.end(), pdta.begin(), pdta.end());

	std::vector<std::uint8_t> file;
	putTag(file, "RIFF");
	put32(file, static_cast<std::uint32_t>(body.size()));
	file.insert(file.end(), body.begin(), body.end());
	return file;
}

} // namespace mitiru_mml_test
