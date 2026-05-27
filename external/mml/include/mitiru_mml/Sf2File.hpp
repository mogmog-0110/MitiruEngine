#pragma once
/// @file Sf2File.hpp
/// @brief SoundFont 2 (.sf2) ローダ — 再生サブセット。
/// @details RIFF('sfbk') を解析し、preset→instrument→sample のジェネレータ階層から
///          鍵域 / ベロシティゾーン・ルートキー・ループ・アンプ ADSR・チューニング・減衰を
///          抽出して MultiSampleInstrument を構築する(#9)。modulator / LFO / filter /
///          24bit サンプル(sm24) は割愛（ゲーム音楽に効く範囲に限定）。
///
/// @code
/// mitiru_mml::Sf2File sf;
/// if (sf.loadFile("piano.sf2")) {
///     auto inst = sf.buildInstrument(0, 44100);   // 先頭プリセット → マルチサンプル楽器
///     auto note = inst.renderNote(60, 100, 18000, 22050, 1.0f); // C4
/// }
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/MultiSampleInstrument.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief SoundFont 2 ローダ（再生サブセット）
class Sf2File
{
public:
	/// @brief プリセットの識別情報。
	struct PresetInfo { std::string name; int bank = 0; int program = 0; };

	[[nodiscard]] bool loadFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs) { m_error = "cannot open file"; return false; }
		std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(ifs)), {});
		return loadFromMemory(buf.data(), buf.size());
	}

	/// @brief メモリ上の SF2 バイト列を解析する。
	[[nodiscard]] bool loadFromMemory(const std::uint8_t* data, std::size_t size)
	{
		reset();
		if (!data || size < 12) { m_error = "too small"; return false; }
		if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "sfbk", 4) != 0)
		{ m_error = "not a RIFF/sfbk"; return false; }

		const std::uint32_t riffSize = rd32(data + 4);
		const std::uint8_t* end = data + std::min<std::size_t>(size, 8 + riffSize);
		const std::uint8_t* p = data + 12;
		while (p + 8 <= end)
		{
			const std::uint32_t csize = rd32(p + 4);
			const std::uint8_t* body = p + 8;
			if (body + csize > end) break;
			if (std::memcmp(p, "LIST", 4) == 0 && csize >= 4)
			{
				if (std::memcmp(body, "sdta", 4) == 0) parseSdta(body + 4, body + csize);
				else if (std::memcmp(body, "pdta", 4) == 0) parsePdta(body + 4, body + csize);
			}
			p = body + csize + (csize & 1); // 奇数サイズは 1 バイトパディング
		}
		if (m_presetHeaders.size() < 2 || m_insts.size() < 2 || m_shdr.empty())
		{ m_error = "incomplete pdta"; return false; }
		return true;
	}

	[[nodiscard]] bool valid() const noexcept { return m_presetHeaders.size() >= 2; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	/// @brief 実プリセット数（終端 EOP レコードを除く）。
	[[nodiscard]] int presetCount() const noexcept
	{
		return m_presetHeaders.empty() ? 0 : static_cast<int>(m_presetHeaders.size()) - 1;
	}

	[[nodiscard]] PresetInfo presetInfo(int index) const
	{
		if (index < 0 || index >= presetCount()) return {};
		const auto& h = m_presetHeaders[static_cast<std::size_t>(index)];
		return {h.name, h.bank, h.program};
	}

	/// @brief プリセットからマルチサンプル楽器を構築する。
	/// @param presetIndex 実プリセット番号（0..presetCount()-1）
	/// @param outputRate 出力サンプルレート
	[[nodiscard]] MultiSampleInstrument buildInstrument(int presetIndex, std::uint32_t outputRate) const
	{
		MultiSampleInstrument out;
		if (presetIndex < 0 || presetIndex >= presetCount()) return out;

		const auto& ph = m_presetHeaders[static_cast<std::size_t>(presetIndex)];
		const auto& phNext = m_presetHeaders[static_cast<std::size_t>(presetIndex) + 1];
		Gens presetGlobal; // プリセットのグローバルゾーン（既定値）

		for (std::size_t b = ph.bagNdx; b + 1 < m_pbag.size() && b < phNext.bagNdx; ++b)
		{
			Gens pg = presetGlobal;
			const bool hasInst = parseGens(m_pgen, m_pbag[b].genNdx, m_pbag[b + 1].genNdx, pg);
			if (!hasInst)
			{
				if (b == ph.bagNdx) presetGlobal = pg; // グローバルゾーン
				continue;
			}
			resolveInstrument(pg.instrument, pg, outputRate, out);
		}
		return out;
	}

private:
	// ── 生レコード ───────────────────────────────────────────
	struct PHdr { std::string name; std::uint16_t program = 0, bank = 0, bagNdx = 0; };
	struct Bag  { std::uint16_t genNdx = 0, modNdx = 0; };
	struct Gen  { std::uint16_t oper = 0; std::uint16_t amount = 0; };
	struct Inst { std::string name; std::uint16_t bagNdx = 0; };
	struct Shdr { std::string name; std::uint32_t start = 0, end = 0, startloop = 0, endloop = 0, rate = 0;
	              std::uint8_t pitch = 60; std::int8_t corr = 0; std::uint16_t link = 0, type = 1; };

	// ── 解決済みジェネレータ集合 ─────────────────────────────
	struct Gens
	{
		int keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
		int sampleID = -1, instrument = -1, rootOverride = -1;
		int coarseTune = 0, fineTune = 0, sampleModes = 0, initialAtten = 0;
		int attackTc = -12000, decayTc = -12000, releaseTc = -12000, sustainCb = 0;
		int startOff = 0, endOff = 0, loopStartOff = 0, loopEndOff = 0;
	};

	void reset()
	{
		m_error.clear(); m_smpl.clear();
		m_presetHeaders.clear(); m_pbag.clear(); m_pgen.clear();
		m_insts.clear(); m_ibag.clear(); m_igen.clear(); m_shdr.clear();
	}

	// ── リトルエンディアン読み出し ──────────────────────────
	static std::uint16_t rd16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
	static std::uint32_t rd32(const std::uint8_t* p)
	{ return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
	         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
	static std::int16_t rds16(const std::uint8_t* p) { return static_cast<std::int16_t>(rd16(p)); }
	static std::string rdName(const std::uint8_t* p, std::size_t n)
	{
		std::size_t len = 0; while (len < n && p[len] != 0) ++len;
		return std::string(reinterpret_cast<const char*>(p), len);
	}

	void parseSdta(const std::uint8_t* begin, const std::uint8_t* end)
	{
		const std::uint8_t* p = begin;
		while (p + 8 <= end)
		{
			const std::uint32_t csize = rd32(p + 4);
			const std::uint8_t* body = p + 8;
			if (body + csize > end) break;
			if (std::memcmp(p, "smpl", 4) == 0)
			{
				const std::size_t n = csize / 2;
				m_smpl.resize(n);
				for (std::size_t i = 0; i < n; ++i) m_smpl[i] = rds16(body + i * 2);
			}
			p = body + csize + (csize & 1);
		}
	}

	void parsePdta(const std::uint8_t* begin, const std::uint8_t* end)
	{
		const std::uint8_t* p = begin;
		while (p + 8 <= end)
		{
			const std::uint32_t csize = rd32(p + 4);
			const std::uint8_t* body = p + 8;
			if (body + csize > end) break;
			if (std::memcmp(p, "phdr", 4) == 0) parsePhdr(body, csize);
			else if (std::memcmp(p, "pbag", 4) == 0) parseBags(body, csize, m_pbag);
			else if (std::memcmp(p, "pgen", 4) == 0) parseGenChunk(body, csize, m_pgen);
			else if (std::memcmp(p, "inst", 4) == 0) parseInst(body, csize);
			else if (std::memcmp(p, "ibag", 4) == 0) parseBags(body, csize, m_ibag);
			else if (std::memcmp(p, "igen", 4) == 0) parseGenChunk(body, csize, m_igen);
			else if (std::memcmp(p, "shdr", 4) == 0) parseShdr(body, csize);
			p = body + csize + (csize & 1);
		}
	}

	void parsePhdr(const std::uint8_t* p, std::uint32_t size)
	{
		for (std::uint32_t o = 0; o + 38 <= size; o += 38)
		{
			const std::uint8_t* r = p + o;
			m_presetHeaders.push_back({rdName(r, 20), rd16(r + 20), rd16(r + 22), rd16(r + 24)});
		}
	}

	static void parseBags(const std::uint8_t* p, std::uint32_t size, std::vector<Bag>& out)
	{
		for (std::uint32_t o = 0; o + 4 <= size; o += 4)
			out.push_back({rd16(p + o), rd16(p + o + 2)});
	}

	static void parseGenChunk(const std::uint8_t* p, std::uint32_t size, std::vector<Gen>& out)
	{
		for (std::uint32_t o = 0; o + 4 <= size; o += 4)
			out.push_back({rd16(p + o), rd16(p + o + 2)});
	}

	void parseInst(const std::uint8_t* p, std::uint32_t size)
	{
		for (std::uint32_t o = 0; o + 22 <= size; o += 22)
			m_insts.push_back({rdName(p + o, 20), rd16(p + o + 20)});
	}

	void parseShdr(const std::uint8_t* p, std::uint32_t size)
	{
		for (std::uint32_t o = 0; o + 46 <= size; o += 46)
		{
			const std::uint8_t* r = p + o;
			Shdr s;
			s.name = rdName(r, 20);
			s.start = rd32(r + 20); s.end = rd32(r + 24);
			s.startloop = rd32(r + 28); s.endloop = rd32(r + 32);
			s.rate = rd32(r + 36); s.pitch = r[40];
			s.corr = static_cast<std::int8_t>(r[41]); s.link = rd16(r + 42); s.type = rd16(r + 44);
			m_shdr.push_back(s);
		}
	}

	/// @brief ジェネレータ列を base に適用する。'instrument' か 'sampleID' があれば true。
	static bool parseGens(const std::vector<Gen>& gens, std::uint16_t begin, std::uint16_t end, Gens& g)
	{
		bool terminal = false;
		for (std::size_t i = begin; i < end && i < gens.size(); ++i)
			terminal |= applyGen(gens[i].oper, gens[i].amount, g);
		return terminal;
	}

	/// @brief 1 ジェネレータを適用。'instrument'(41) / 'sampleID'(53) なら true を返す。
	static bool applyGen(std::uint16_t oper, std::uint16_t amount, Gens& g)
	{
		const int lo = amount & 0xFF, hi = (amount >> 8) & 0xFF;
		const int s = static_cast<int>(static_cast<std::int16_t>(amount));
		switch (oper)
		{
		case 0:  g.startOff += s; break;          // startAddrsOffset
		case 1:  g.endOff += s; break;            // endAddrsOffset
		case 2:  g.loopStartOff += s; break;      // startloopAddrsOffset
		case 3:  g.loopEndOff += s; break;        // endloopAddrsOffset
		case 4:  g.startOff += s * 32768; break;  // startAddrsCoarseOffset
		case 34: g.attackTc = s; break;           // attackVolEnv (timecents)
		case 36: g.decayTc = s; break;            // decayVolEnv
		case 37: g.sustainCb = s; break;          // sustainVolEnv (centibels of attenuation)
		case 38: g.releaseTc = s; break;          // releaseVolEnv
		case 41: g.instrument = amount; return true;  // instrument index
		case 43: g.keyLo = lo; g.keyHi = hi; break;   // keyRange
		case 44: g.velLo = lo; g.velHi = hi; break;   // velRange
		case 45: g.loopStartOff += s * 32768; break;  // startloopAddrsCoarseOffset
		case 48: g.initialAtten = s; break;           // initialAttenuation (centibels)
		case 50: g.loopEndOff += s * 32768; break;    // endloopAddrsCoarseOffset
		case 51: g.coarseTune = s; break;             // coarseTune (semitones)
		case 52: g.fineTune = s; break;               // fineTune (cents)
		case 53: g.sampleID = amount; return true;    // sampleID
		case 54: g.sampleModes = amount; break;       // sampleModes (0=none,1/3=loop)
		case 58: g.rootOverride = amount; break;      // overridingRootKey
		default: break;
		}
		return false;
	}

	void resolveInstrument(int instIndex, const Gens& presetFilter,
	                       std::uint32_t outputRate, MultiSampleInstrument& out) const
	{
		if (instIndex < 0 || instIndex + 1 >= static_cast<int>(m_insts.size())) return;
		const auto& inst = m_insts[static_cast<std::size_t>(instIndex)];
		const auto& instNext = m_insts[static_cast<std::size_t>(instIndex) + 1];
		Gens instGlobal;

		for (std::size_t b = inst.bagNdx; b + 1 < m_ibag.size() && b < instNext.bagNdx; ++b)
		{
			Gens zg = instGlobal;
			const bool hasSample = parseGens(m_igen, m_ibag[b].genNdx, m_ibag[b + 1].genNdx, zg);
			if (!hasSample)
			{
				if (b == inst.bagNdx) instGlobal = zg; // インストグローバルゾーン
				continue;
			}
			addZoneFromGens(zg, presetFilter, outputRate, out);
		}
	}

	void addZoneFromGens(const Gens& zg, const Gens& presetFilter,
	                     std::uint32_t outputRate, MultiSampleInstrument& out) const
	{
		if (zg.sampleID < 0 || zg.sampleID >= static_cast<int>(m_shdr.size())) return;
		const Shdr& sh = m_shdr[static_cast<std::size_t>(zg.sampleID)];
		if (sh.end <= sh.start || sh.end > m_smpl.size()) return;

		// 鍵域・ベロシティ範囲はプリセットフィルタと積集合をとる。
		const int keyLo = std::max(zg.keyLo, presetFilter.keyLo);
		const int keyHi = std::min(zg.keyHi, presetFilter.keyHi);
		const int velLo = std::max(zg.velLo, presetFilter.velLo);
		const int velHi = std::min(zg.velHi, presetFilter.velHi);
		if (keyLo > keyHi || velLo > velHi) return;

		// サンプル抽出（オフセットジェネレータを反映）。
		const std::size_t start = clampIdx(static_cast<long long>(sh.start) + zg.startOff, m_smpl.size());
		const std::size_t fin   = clampIdx(static_cast<long long>(sh.end)   + zg.endOff, m_smpl.size());
		if (fin <= start) return;
		PcmBuffer pcm(m_smpl.begin() + static_cast<std::ptrdiff_t>(start),
		              m_smpl.begin() + static_cast<std::ptrdiff_t>(fin));

		// チューニング: coarse(半音) + fine(cents) + サンプル補正、プリセット側を加算。
		const int root = (zg.rootOverride >= 0 ? zg.rootOverride : static_cast<int>(sh.pitch));
		const int coarse = zg.coarseTune + presetFilter.coarseTune;
		const float cents = static_cast<float>(zg.fineTune + presetFilter.fineTune + sh.corr);
		// 音程を cents 分「上げる」= 再生を速める = baseFreq を下げる（符号は負）。
		const float rootFreq = MultiSampleInstrument::midiToFreq(root - coarse)
		                       * std::pow(2.0f, -cents / 1200.0f);
		const float sr = sh.rate > 0 ? static_cast<float>(sh.rate) : static_cast<float>(outputRate);
		const float baseFreq = rootFreq * static_cast<float>(outputRate) / sr;

		// アンプ ADSR（timecents→秒、sustain は centibels 減衰）。
		SampleInstrument::Adsr adsr;
		adsr.attack  = timecentsToSec(zg.attackTc);
		adsr.decay   = timecentsToSec(zg.decayTc);
		adsr.release = timecentsToSec(zg.releaseTc);
		adsr.sustain = std::clamp(std::pow(10.0f,
			-static_cast<float>(zg.sustainCb + presetFilter.initialAtten) / 200.0f), 0.0f, 1.0f);

		const bool looped = (zg.sampleModes == 1 || zg.sampleModes == 3);
		std::size_t ls = clampIdx(static_cast<long long>(sh.startloop) + zg.loopStartOff, fin);
		std::size_t le = clampIdx(static_cast<long long>(sh.endloop) + zg.loopEndOff, fin);
		if (ls >= start) ls -= start; else ls = 0;   // 抽出バッファ相対へ
		if (le >= start) le -= start; else le = 0;

		SampleInstrument voice;
		voice.setSample(pcm, baseFreq, outputRate);
		if (looped && le > ls + 1) voice.setLoop(ls, le);
		voice.setAdsr(adsr);

		const float gain = std::clamp(std::pow(10.0f,
			-static_cast<float>(zg.initialAtten) / 200.0f), 0.0f, 1.0f);
		out.addZone({keyLo, keyHi, velLo, velHi, std::move(voice), gain});
	}

	static std::size_t clampIdx(long long v, std::size_t hi)
	{
		if (v < 0) return 0;
		const auto u = static_cast<std::size_t>(v);
		return u > hi ? hi : u;
	}

	static float timecentsToSec(int tc)
	{
		if (tc <= -32000) return 0.0f; // 実質ゼロ
		return std::pow(2.0f, static_cast<float>(tc) / 1200.0f);
	}

	std::string m_error;
	std::vector<std::int16_t> m_smpl;
	std::vector<PHdr> m_presetHeaders;
	std::vector<Bag> m_pbag;
	std::vector<Gen> m_pgen;
	std::vector<Inst> m_insts;
	std::vector<Bag> m_ibag;
	std::vector<Gen> m_igen;
	std::vector<Shdr> m_shdr;
};

} // namespace mitiru_mml
