#pragma once
/// @file Track.hpp
/// @brief MMLトラック（1パート分の状態管理と波形生成）

#include <mitiru_mml/MmlTypes.hpp>
#include <mitiru_mml/Synthesizer.hpp>
#include <algorithm>

namespace mitiru_mml
{

/// @brief 1トラック分のMML再生状態
class Track
{
public:
	/// @brief コンストラクタ
	/// @param synth 波形合成器への参照
	explicit Track(const Synthesizer& synth) noexcept
		: m_synth(synth)
	{
	}

	/// @brief ループコマンドを展開する（前処理）
	/// @param cmds ループ含みコマンド列
	/// @return ループ展開済みコマンド列
	[[nodiscard]] static CommandList expandLoops(const CommandList& cmds)
	{
		CommandList result;
		std::vector<std::size_t> loopStack; // ループ開始位置スタック

		for (std::size_t i = 0; i < cmds.size(); ++i)
		{
			if (cmds[i].type == CommandType::LoopStart)
			{
				loopStack.push_back(result.size());
			}
			else if (cmds[i].type == CommandType::LoopEnd)
			{
				if (!loopStack.empty())
				{
					std::size_t startIdx = loopStack.back();
					loopStack.pop_back();
					int repeatCount = std::max(1, cmds[i].value) - 1; // 既に1回分は追加済み

					// ループ本体をrepeatCount回追加する
					std::size_t bodyLen = result.size() - startIdx;
					for (int r = 0; r < repeatCount; ++r)
					{
						for (std::size_t j = 0; j < bodyLen; ++j)
						{
							result.push_back(result[startIdx + j]);
						}
					}
				}
			}
			else
			{
				result.push_back(cmds[i]);
			}
		}
		return result;
	}

	/// @brief コマンド列からPCMバッファを生成する
	/// @param commands MMLコマンド列
	/// @return PCMサンプルバッファ
	[[nodiscard]] PcmBuffer render(const CommandList& commands) const
	{
		// ループを展開してから処理する
		const auto expanded = expandLoops(commands);
		return renderExpanded(expanded);
	}

	/// @brief 展開済みコマンド列からPCMバッファを生成する（内部用）
	/// @param commands 展開済みMMLコマンド列
	/// @return PCMサンプルバッファ
	[[nodiscard]] PcmBuffer renderExpanded(const CommandList& commands) const
	{
		PcmBuffer result;

		int tempo = 120;
		int octave = 4;
		int defaultLength = 4;
		float volume = 0.8f;
		WaveType wave = WaveType::Square;
		float gateRatio = 0.875f; // Q7/8
		float dutyRatio = 0.5f;
		float detuneCents = 0.0f;
		float vibratoSpeed = 0.0f;
		float vibratoDepth = 0.0f;
		AdsrEnvelope adsr{};
		bool useAdsr = false;
		bool useFm = false;
		FmPreset fmPreset{};

		for (std::size_t i = 0; i < commands.size(); ++i)
		{
			const auto& cmd = commands[i];

			switch (cmd.type)
			{
			case CommandType::Tempo:
				tempo = std::clamp(cmd.value, 20, 300);
				break;

			case CommandType::Octave:
				octave = std::clamp(cmd.value, 1, 8);
				break;

			case CommandType::OctaveUp:
				octave = std::min(8, octave + 1);
				break;

			case CommandType::OctaveDown:
				octave = std::max(1, octave - 1);
				break;

			case CommandType::Length:
				defaultLength = std::max(1, cmd.value);
				break;

			case CommandType::Volume:
				volume = static_cast<float>(std::clamp(cmd.value, 0, 15)) / 15.0f;
				break;

			case CommandType::Waveform:
				wave = static_cast<WaveType>(std::clamp(cmd.value, 0, 12));
				useFm = false; // 通常波形選択時はFMをオフ
				break;

			case CommandType::Quantize:
				gateRatio = static_cast<float>(std::clamp(cmd.value, 1, 8)) / 8.0f;
				break;

			case CommandType::Duty:
			{
				// W12→0.125, W25→0.25, W50→0.5, W75→0.75
				switch (cmd.value)
				{
				case 12: dutyRatio = 0.125f; break;
				case 25: dutyRatio = 0.25f; break;
				case 50: dutyRatio = 0.5f; break;
				case 75: dutyRatio = 0.75f; break;
				default: dutyRatio = static_cast<float>(cmd.value) / 100.0f; break;
				}
				break;
			}

			case CommandType::Detune:
				detuneCents = static_cast<float>(cmd.value);
				break;

			case CommandType::Adsr:
			{
				useAdsr = true;
				const float val = static_cast<float>(cmd.extra);
				switch (cmd.value)
				{
				case 0: adsr.attack = val * 0.001f; break;   // ms → sec
				case 1: adsr.decay = val * 0.001f; break;
				case 2: adsr.sustain = val / 100.0f; break;  // 0-99 → 0-0.99
				case 3: adsr.release = val * 0.001f; break;
				default: break;
				}
				break;
			}

			case CommandType::Vibrato:
				vibratoSpeed = static_cast<float>(cmd.value);
				vibratoDepth = static_cast<float>(cmd.extra);
				break;

			case CommandType::FmWave:
				useFm = true;
				fmPreset = Synthesizer::getFmPreset(std::clamp(cmd.value, 0, 7));
				break;

			case CommandType::Note:
			{
				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				float durSec = lengthToSeconds(len, tempo);
				if (cmd.dotted) durSec *= 1.5f;

				// タイ処理: 持続時間を累積する
				float totalDur = durSec;
				std::size_t j = i;
				while (j < commands.size() && commands[j].tied && j + 1 < commands.size())
				{
					++j;
					if (commands[j].type == CommandType::Note)
					{
						const int tieLen = (commands[j].duration > 0) ? commands[j].duration : defaultLength;
						float tieDur = lengthToSeconds(tieLen, tempo);
						if (commands[j].dotted) tieDur *= 1.5f;
						totalDur += tieDur;
					}
				}
				i = j; // タイされた音符をスキップ

				const int midiNote = (octave + 1) * 12 + cmd.value;
				const float freq = Synthesizer::noteToFreq(midiNote);

				// 拡張合成を使用
				Synthesizer::NoteParams np;
				np.freq = freq;
				np.durationSec = totalDur;
				np.wave = wave;
				np.volume = volume;
				np.gateRatio = gateRatio;
				np.dutyRatio = dutyRatio;
				np.detuneCents = detuneCents;
				np.vibratoSpeed = vibratoSpeed;
				np.vibratoDepth = vibratoDepth;
				np.adsr = adsr;
				np.useAdsr = useAdsr;
				np.useFm = useFm;
				np.fmPreset = fmPreset;

				auto samples = m_synth.generateAdvanced(np);
				result.insert(result.end(), samples.begin(), samples.end());
				break;
			}

			case CommandType::Rest:
			{
				const int len = (cmd.duration > 0) ? cmd.duration : defaultLength;
				float durSec = lengthToSeconds(len, tempo);
				if (cmd.dotted) durSec *= 1.5f;

				const auto numSamples = static_cast<std::size_t>(
					durSec * static_cast<float>(m_synth.sampleRate()));
				result.resize(result.size() + numSamples, 0);
				break;
			}

			case CommandType::Tie:
			case CommandType::Loop:
			case CommandType::LoopStart:
			case CommandType::LoopEnd:
				break;
			}
		}

		return result;
	}

private:
	/// @brief 音長値（L値）を秒数に変換する
	/// @param lengthVal L値（1=全音符, 4=4分音符, 8=8分音符...）
	/// @param bpm テンポ
	/// @return 秒数
	[[nodiscard]] static float lengthToSeconds(int lengthVal, int bpm) noexcept
	{
		if (lengthVal <= 0) lengthVal = 4;
		if (bpm <= 0) bpm = 120;
		// 4分音符 = 60/bpm秒、全音符 = 4 * 60/bpm秒
		return (4.0f * 60.0f / static_cast<float>(bpm)) / static_cast<float>(lengthVal);
	}

	const Synthesizer& m_synth;
};

} // namespace mitiru_mml
