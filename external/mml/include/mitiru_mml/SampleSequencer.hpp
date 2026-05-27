#pragma once
/// @file SampleSequencer.hpp
/// @brief サンプルベース楽器で MML を再生するシーケンサー（+ 任意でマスターリバーブ）。
/// @details 各トラックに SampleInstrument を割り当て、MML を解析して 1 音ずつ
///          instrument.renderNote() で描画・ミックスする。スーファミ/同人ゲーム級の
///          サンプル音色 + 残響を狙う(#9)。オフライン WAV 書き出しに対応。
///          MVP 対応コマンド: T/O/L/V/オクターブ増減/音符/休符（ループ・タイは未対応）。
///
/// @code
/// mitiru_mml::SampleSequencer seq;
/// seq.addTrack("T120 O4 L8 CDEFGAB>C", mitiru_mml::SampleInstrument::warmSynth(44100));
/// seq.setReverb(true);
/// seq.exportWav("out.wav");
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/MmlParser.hpp>
#include <mitiru_mml/SampleInstrument.hpp>
#include <mitiru_mml/MultiSampleInstrument.hpp>
#include <mitiru_mml/Reverb.hpp>
#include <mitiru_mml/WavWriter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief サンプル音色 MML シーケンサー（モノラル）
class SampleSequencer
{
public:
	explicit SampleSequencer(std::uint32_t sampleRate = 44100) : m_sampleRate(sampleRate) {}

	/// @brief 単一サンプル楽器でトラックを追加する（MML + 楽器）。
	void addTrack(std::string_view mml, SampleInstrument instrument)
	{
		auto inst = std::make_shared<SampleInstrument>(std::move(instrument));
		m_tracks.push_back({std::string(mml),
			[inst](int midiKey, int /*vel*/, int gate, int total, float vol) {
				return inst->renderNote(MultiSampleInstrument::midiToFreq(midiKey), gate, total, vol);
			}});
	}

	/// @brief マルチサンプル楽器（SF2 等）でトラックを追加する。
	void addTrack(std::string_view mml, MultiSampleInstrument instrument)
	{
		auto inst = std::make_shared<MultiSampleInstrument>(std::move(instrument));
		m_tracks.push_back({std::string(mml),
			[inst](int midiKey, int vel, int gate, int total, float vol) {
				return inst->renderNote(midiKey, vel, gate, total, vol);
			}});
	}

	void clear() { m_tracks.clear(); }
	[[nodiscard]] int trackCount() const noexcept { return static_cast<int>(m_tracks.size()); }
	void setReverb(bool on) { m_reverb = on; }
	[[nodiscard]] std::uint32_t sampleRate() const noexcept { return m_sampleRate; }

	/// @brief 全トラックをレンダリングしてミックスする。
	[[nodiscard]] PcmBuffer render() const
	{
		std::vector<float> mix;
		for (const auto& tr : m_tracks)
		{
			renderTrackInto(tr, mix);
		}
		PcmBuffer out(mix.size());
		for (std::size_t i = 0; i < mix.size(); ++i)
			out[i] = static_cast<std::int16_t>(std::clamp(mix[i], -1.0f, 1.0f) * 32767.0f);

		if (m_reverb)
		{
			Reverb rv;
			rv.setRoomSize(0.6f).setWet(0.25f).setDry(0.92f);
			out = rv.process(out, m_sampleRate);
		}
		return out;
	}

	/// @brief WAV ファイルに書き出す。
	bool exportWav(const std::string& path) const
	{
		auto pcm = render();
		if (pcm.empty()) return false;
		auto wav = WavWriter::toWav(pcm, m_sampleRate);
		std::ofstream ofs(path, std::ios::binary);
		if (!ofs) return false;
		ofs.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
		return ofs.good();
	}

private:
	/// @brief ノート描画関数: (midiKey, velocity, gateSamples, totalSamples, volume) → PCM。
	using NoteRenderer = std::function<PcmBuffer(int, int, int, int, float)>;
	struct TrackData { std::string mml; NoteRenderer render; };

	[[nodiscard]] static float lengthToSeconds(int lengthVal, int bpm) noexcept
	{
		if (lengthVal <= 0) lengthVal = 4;
		return (4.0f * 60.0f / static_cast<float>(bpm)) / static_cast<float>(lengthVal);
	}

	void renderTrackInto(const TrackData& tr, std::vector<float>& mix) const
	{
		if (!tr.render) return;
		const auto cmds = MmlParser::parse(tr.mml);
		int tempo = 120, octave = 4, defaultLength = 4, volume = 12;
		std::size_t offset = 0; // 出力サンプルオフセット

		for (const auto& cmd : cmds)
		{
			switch (cmd.type)
			{
			case CommandType::Tempo:      tempo = std::clamp(cmd.value, 20, 300); break;
			case CommandType::Octave:     octave = std::clamp(cmd.value, 1, 8); break;
			case CommandType::OctaveUp:   octave = std::min(8, octave + 1); break;
			case CommandType::OctaveDown: octave = std::max(1, octave - 1); break;
			case CommandType::Length:     defaultLength = std::max(1, cmd.value); break;
			case CommandType::Volume:     volume = std::clamp(cmd.value, 0, 15); break;
			case CommandType::Rest:
			{
				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				offset += durationSamples(len, cmd.dotted, tempo);
				break;
			}
			case CommandType::Note:
			{
				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				const std::size_t dur = durationSamples(len, cmd.dotted, tempo);
				const int midiKey = std::clamp((octave + 1) * 12 + cmd.value, 0, 127);
				const int velocity = std::clamp(volume * 127 / 15, 0, 127);
				const int gate = static_cast<int>(static_cast<float>(dur) * 0.85f);
				const float vol = static_cast<float>(volume) / 15.0f;
				const auto note = tr.render(midiKey, velocity, gate, static_cast<int>(dur), vol);
				mixAt(mix, note, offset);
				offset += dur;
				break;
			}
			default: break;
			}
		}
	}

	[[nodiscard]] std::size_t durationSamples(int len, bool dotted, int tempo) const
	{
		float sec = lengthToSeconds(len, tempo);
		if (dotted) sec *= 1.5f;
		return static_cast<std::size_t>(sec * static_cast<float>(m_sampleRate));
	}

	static void mixAt(std::vector<float>& mix, const PcmBuffer& note, std::size_t offset)
	{
		const std::size_t end = offset + note.size();
		if (mix.size() < end) mix.resize(end, 0.0f);
		for (std::size_t i = 0; i < note.size(); ++i)
			mix[offset + i] += static_cast<float>(note[i]) / 32768.0f;
	}

	std::uint32_t m_sampleRate;
	std::vector<TrackData> m_tracks;
	bool m_reverb = false;
};

} // namespace mitiru_mml
