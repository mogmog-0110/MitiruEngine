#pragma once
/// @file SfzFile.hpp
/// @brief SFZ サウンドフォントローダ — 再生サブセット。
/// @details SFZ テキスト（`<global>`/`<group>`/`<region>` 階層 + opcode カスケード）を解析し、
///          参照する外部 WAV を読み込んで MultiSampleInstrument を構築する(#9)。SF2 と同じ基盤に乗る。
///          対応 opcode: sample / lokey / hikey / key / pitch_keycenter / lovel / hivel /
///          loop_mode / loop_start / loop_end / ampeg_attack / ampeg_decay / ampeg_sustain /
///          ampeg_release / tune / transpose / volume / default_path。
///          割愛: filter / LFO / round-robin / トリガ条件 / 効果。
///
/// @code
/// mitiru_mml::SfzFile sfz;
/// if (sfz.loadFile("piano.sfz")) {
///     auto inst = sfz.buildInstrument(44100);
///     auto note = inst.renderNote(60, 100, 18000, 22050, 1.0f);
/// }
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/MultiSampleInstrument.hpp>
#include <mitiru_mml/WavReader.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mitiru_mml
{

/// @brief SFZ ローダ（再生サブセット）
class SfzFile
{
public:
	[[nodiscard]] bool loadFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs) { m_error = "cannot open file"; return false; }
		std::stringstream ss; ss << ifs.rdbuf();
		const std::string baseDir = std::filesystem::path(path).parent_path().string();
		return loadFromString(ss.str(), baseDir);
	}

	/// @brief SFZ テキストを解析し、参照 WAV を baseDir 起点で読み込む。
	[[nodiscard]] bool loadFromString(const std::string& text, const std::string& baseDir)
	{
		m_error.clear(); m_zones.clear();
		const auto tokens = tokenize(stripComments(text));

		using Map = std::map<std::string, std::string>;
		Map global, group, region, control;
		Map* cur = nullptr;
		std::string lastKey; Map* lastMap = nullptr;
		bool inRegion = false;

		auto flush = [&]() {
			if (inRegion) buildZone(merge(global, group, region), control, baseDir);
			inRegion = false;
		};

		for (const auto& tok : tokens)
		{
			if (tok.size() >= 2 && tok.front() == '<' && tok.back() == '>')
			{
				const std::string h = tok.substr(1, tok.size() - 2);
				if (h == "region")      { flush(); region.clear(); cur = &region; inRegion = true; }
				else if (h == "group")  { flush(); group.clear(); region.clear(); cur = &group; }
				else if (h == "global") { flush(); global.clear(); group.clear(); region.clear(); cur = &global; }
				else if (h == "control"){ cur = &control; }
				else                     { cur = nullptr; }
				lastKey.clear(); lastMap = nullptr;
				continue;
			}
			const auto eq = tok.find('=');
			if (eq != std::string::npos && cur)
			{
				lastKey = tok.substr(0, eq);
				(*cur)[lastKey] = tok.substr(eq + 1);
				lastMap = cur;
			}
			else if (!lastKey.empty() && lastMap)
			{
				(*lastMap)[lastKey] += " " + tok; // スペースを含むパスの継続
			}
		}
		flush();
		return !m_zones.empty();
	}

	[[nodiscard]] bool valid() const noexcept { return !m_zones.empty(); }
	[[nodiscard]] int zoneCount() const noexcept { return static_cast<int>(m_zones.size()); }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	/// @brief 読み込んだゾーンからマルチサンプル楽器を構築する。
	[[nodiscard]] MultiSampleInstrument buildInstrument(std::uint32_t outputRate) const
	{
		MultiSampleInstrument out;
		for (const auto& z : m_zones)
		{
			const float sr = z.sampleRate > 0 ? static_cast<float>(z.sampleRate) : static_cast<float>(outputRate);
			const float rootFreq = MultiSampleInstrument::midiToFreq(z.root - z.transpose)
			                       * std::pow(2.0f, -z.tuneCents / 1200.0f);
			const float baseFreq = rootFreq * static_cast<float>(outputRate) / sr;

			SampleInstrument voice;
			voice.setSample(z.pcm, baseFreq, outputRate);
			if (z.looped && z.loopEnd > z.loopStart + 1) voice.setLoop(z.loopStart, z.loopEnd);
			voice.setAdsr(z.adsr);
			out.addZone({z.keyLo, z.keyHi, z.velLo, z.velHi, std::move(voice), z.gain});
		}
		return out;
	}

private:
	struct RawZone
	{
		PcmBuffer pcm; std::uint32_t sampleRate = 44100;
		int keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
		int root = 60, transpose = 0; float tuneCents = 0.0f;
		bool looped = false; std::size_t loopStart = 0, loopEnd = 0;
		SampleInstrument::Adsr adsr; float gain = 1.0f;
	};
	using Map = std::map<std::string, std::string>;

	static std::string stripComments(const std::string& text)
	{
		std::string out; out.reserve(text.size());
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/')
			{ while (i < text.size() && text[i] != '\n') ++i; out.push_back('\n'); }
			else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*')
			{ i += 2; while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i; ++i; }
			else out.push_back(text[i]);
		}
		return out;
	}

	static std::vector<std::string> tokenize(const std::string& text)
	{
		std::vector<std::string> toks;
		std::string cur;
		for (char c : text)
		{
			if (std::isspace(static_cast<unsigned char>(c)))
			{ if (!cur.empty()) { toks.push_back(cur); cur.clear(); } }
			else cur.push_back(c);
		}
		if (!cur.empty()) toks.push_back(cur);
		return toks;
	}

	// region→group→global の順に上書きしたマージ済みマップを返す。
	static Map merge(const Map& global, const Map& group, const Map& region)
	{
		Map m = global;
		for (const auto& [k, v] : group) m[k] = v;
		for (const auto& [k, v] : region) m[k] = v;
		return m;
	}

	static std::string get(const Map& m, const std::string& key, const std::string& def = "")
	{
		const auto it = m.find(key);
		return it == m.end() ? def : it->second;
	}

	// SFZ のキー指定（ノート名 c4=60 / 整数）を MIDI ノート番号へ。
	static int parseKey(const std::string& s, int def)
	{
		if (s.empty()) return def;
		if (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '-' || s[0] == '+')
		{ try { return std::stoi(s); } catch (...) { return def; } }
		static const std::map<char, int> kBase = {{'c',0},{'d',2},{'e',4},{'f',5},{'g',7},{'a',9},{'b',11}};
		const auto it = kBase.find(static_cast<char>(std::tolower(static_cast<unsigned char>(s[0]))));
		if (it == kBase.end()) return def;
		int note = it->second, i = 1, acc = 0;
		while (i < static_cast<int>(s.size()) && (s[i] == '#' || s[i] == '+')) { ++acc; ++i; }
		while (i < static_cast<int>(s.size()) && (s[i] == 'b' || s[i] == '-')) { --acc; ++i; }
		int octave = def / 12 - 1;
		try { octave = std::stoi(s.substr(i)); } catch (...) {}
		return note + acc + (octave + 1) * 12;
	}

	static float getFloat(const Map& m, const std::string& key, float def)
	{
		const auto it = m.find(key);
		if (it == m.end()) return def;
		try { return std::stof(it->second); } catch (...) { return def; }
	}

	static int getInt(const Map& m, const std::string& key, int def)
	{
		const auto it = m.find(key);
		if (it == m.end()) return def;
		try { return std::stoi(it->second); } catch (...) { return def; }
	}

	void buildZone(const Map& z, const Map& control, const std::string& baseDir)
	{
		const std::string sample = get(z, "sample");
		if (sample.empty()) return;

		// パス解決: baseDir / default_path / sample（区切りは正規化）。
		std::string rel = get(control, "default_path") + sample;
		std::replace(rel.begin(), rel.end(), '\\', '/');
		std::filesystem::path full = baseDir.empty() ? std::filesystem::path(rel)
		                                             : std::filesystem::path(baseDir) / rel;
		const auto wav = WavReader::fromFile(full.string());
		if (!wav.ok) { m_error = "cannot read sample: " + full.string(); return; }

		RawZone rz;
		rz.pcm = wav.pcm;
		rz.sampleRate = wav.sampleRate;

		const bool hasKey = z.count("key") != 0;
		const int key = parseKey(get(z, "key"), 60);
		rz.root = parseKey(get(z, "pitch_keycenter"), hasKey ? key : 60);
		rz.keyLo = parseKey(get(z, "lokey"), hasKey ? key : 0);
		rz.keyHi = parseKey(get(z, "hikey"), hasKey ? key : 127);
		rz.velLo = getInt(z, "lovel", 0);
		rz.velHi = getInt(z, "hivel", 127);
		rz.transpose = getInt(z, "transpose", 0);
		rz.tuneCents = getFloat(z, "tune", 0.0f);

		const std::string mode = get(z, "loop_mode");
		rz.looped = (mode == "loop_continuous" || mode == "loop_sustain");
		rz.loopStart = static_cast<std::size_t>(std::max(0, getInt(z, "loop_start", 0)));
		rz.loopEnd = static_cast<std::size_t>(std::max(0, getInt(z, "loop_end",
			static_cast<int>(rz.pcm.size() > 0 ? rz.pcm.size() - 1 : 0))));

		// ampeg_*（秒。クリック回避のため最小値を確保）。sustain は % → 0..1。
		rz.adsr.attack  = std::max(0.0005f, getFloat(z, "ampeg_attack", 0.001f));
		rz.adsr.decay   = std::max(0.0f, getFloat(z, "ampeg_decay", 0.0f));
		rz.adsr.sustain = std::clamp(getFloat(z, "ampeg_sustain", 100.0f) / 100.0f, 0.0f, 1.0f);
		rz.adsr.release = std::max(0.005f, getFloat(z, "ampeg_release", 0.05f));

		const float volDb = getFloat(z, "volume", 0.0f);
		rz.gain = std::clamp(std::pow(10.0f, volDb / 20.0f), 0.0f, 1.0f);

		m_zones.push_back(std::move(rz));
	}

	std::string m_error;
	std::vector<RawZone> m_zones;
};

} // namespace mitiru_mml
