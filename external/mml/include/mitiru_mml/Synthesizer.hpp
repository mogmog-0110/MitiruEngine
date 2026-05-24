#pragma once
/// @file Synthesizer.hpp
/// @brief 波形合成エンジン
/// @details ノート番号と波形タイプからPCMサンプルを生成する。

#include <mitiru_mml/MmlTypes.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief 波形合成器
class Synthesizer
{
public:
	/// @brief コンストラクタ
	/// @param config オーディオ設定
	explicit Synthesizer(const AudioConfig& config = {}) noexcept
		: m_config(config)
	{
	}

	/// @brief MIDIノート番号から周波数を計算する
	/// @param noteNum MIDIノート番号（60=C4）
	/// @return 周波数(Hz)
	[[nodiscard]] static float noteToFreq(int noteNum) noexcept
	{
		return 440.0f * std::pow(2.0f, (static_cast<float>(noteNum) - 69.0f) / 12.0f);
	}

	/// @brief 指定波形・周波数・長さのPCMサンプルを生成する
	/// @param freq 周波数(Hz)
	/// @param durationSec 持続時間（秒）
	/// @param wave 波形タイプ
	/// @param volume ボリューム [0,1]
	/// @param gateRatio ゲートタイム比率 [0,1]（1=レガート）
	/// @return PCMサンプルバッファ
	[[nodiscard]] PcmBuffer generate(
		float freq,
		float durationSec,
		WaveType wave = WaveType::Square,
		float volume = 0.8f,
		float gateRatio = 0.875f) const
	{
		const auto numSamples = static_cast<std::uint32_t>(
			durationSec * static_cast<float>(m_config.sampleRate));
		if (numSamples == 0) return {};

		PcmBuffer buf(numSamples, 0);
		const float sr = static_cast<float>(m_config.sampleRate);
		const float amp = std::clamp(volume, 0.0f, 1.0f) * m_config.masterVolume * 24000.0f;
		const auto gateSamples = static_cast<std::uint32_t>(
			static_cast<float>(numSamples) * std::clamp(gateRatio, 0.0f, 1.0f));

		// ノイズ用状態
		std::uint32_t noiseState = 0xACE1u;

		for (std::uint32_t i = 0; i < numSamples; ++i)
		{
			if (i >= gateSamples)
			{
				buf[i] = 0;
				continue;
			}

			const float t = static_cast<float>(i) / sr;
			const float phase = std::fmod(freq * t, 1.0f);
			float sample = 0.0f;

			switch (wave)
			{
			case WaveType::Square:
				sample = (phase < 0.5f) ? 1.0f : -1.0f;
				break;
			case WaveType::Triangle:
				sample = (phase < 0.5f)
					? (4.0f * phase - 1.0f)
					: (3.0f - 4.0f * phase);
				break;
			case WaveType::Sine:
				sample = std::sin(2.0f * PI * phase);
				break;
			case WaveType::Sawtooth:
				sample = 2.0f * phase - 1.0f;
				break;
			case WaveType::Noise:
				noiseState ^= noiseState << 13;
				noiseState ^= noiseState >> 17;
				noiseState ^= noiseState << 5;
				sample = (static_cast<float>(noiseState & 0xFFFF) / 32767.5f) - 1.0f;
				break;
			}

			// エンベロープ: アタック(5ms) + リリース(最後10ms)
			float envelope = 1.0f;
			const float attackEnd = 0.005f * sr;
			const float releaseStart = static_cast<float>(gateSamples) - 0.01f * sr;
			if (static_cast<float>(i) < attackEnd)
			{
				envelope = static_cast<float>(i) / attackEnd;
			}
			else if (static_cast<float>(i) > releaseStart && releaseStart > 0.0f)
			{
				envelope = (static_cast<float>(gateSamples) - static_cast<float>(i))
					/ (static_cast<float>(gateSamples) - releaseStart);
			}

			buf[i] = static_cast<std::int16_t>(
				std::clamp(sample * amp * envelope, -32767.0f, 32767.0f));
		}

		return buf;
	}

		/// @brief 拡張ノートパラメータ
	struct NoteParams
	{
		float freq = 440.0f;           ///< 周波数(Hz)
		float durationSec = 0.5f;      ///< 持続時間（秒）
		WaveType wave = WaveType::Square;  ///< 波形タイプ
		float volume = 0.8f;           ///< ボリューム [0,1]
		float gateRatio = 0.875f;      ///< ゲートタイム比率 [0,1]
		float dutyRatio = 0.5f;        ///< デューティ比
		float detuneCents = 0.0f;      ///< デチューン（セント）
		float vibratoSpeed = 0.0f;     ///< LFO速度（Hz）
		float vibratoDepth = 0.0f;     ///< LFO深さ（セント）
		AdsrEnvelope adsr{};           ///< ADSRエンベロープ
		bool useFm = false;            ///< FM合成使用
		FmPreset fmPreset{};           ///< FMプリセット
		bool useAdsr = false;          ///< ADSR使用
	};

	/// @brief 拡張パラメータによるPCMサンプル生成
	/// @param params ノートパラメータ
	/// @return PCMサンプルバッファ
	[[nodiscard]] PcmBuffer generateAdvanced(const NoteParams& params) const
	{
		const auto numSamples = static_cast<std::uint32_t>(
			params.durationSec * static_cast<float>(m_config.sampleRate));
		if (numSamples == 0) return {};

		PcmBuffer buf(numSamples, 0);
		const float sr = static_cast<float>(m_config.sampleRate);
		const float amp = std::clamp(params.volume, 0.0f, 1.0f) * m_config.masterVolume * 24000.0f;
		const auto gateSamples = static_cast<std::uint32_t>(
			static_cast<float>(numSamples) * std::clamp(params.gateRatio, 0.0f, 1.0f));

		// デチューン: 2ボイスで生成して平均化
		float freqs[2] = { params.freq, params.freq };
		int numVoices = 1;
		if (std::abs(params.detuneCents) > 0.01f)
		{
			const float factor = std::pow(2.0f, params.detuneCents / 1200.0f);
			freqs[0] = params.freq * factor;
			freqs[1] = params.freq / factor;
			numVoices = 2;
		}

		std::uint32_t noiseState = 0xACE1u;
		constexpr float PI2 = 2.0f * PI;

		// FMフィードバック用: ボイスごとに前サンプル値を保持
		float prevSample[2] = { 0.0f, 0.0f };

		for (std::uint32_t i = 0; i < numSamples; ++i)
		{
			if (i >= gateSamples)
			{
				buf[i] = 0;
				continue;
			}

			const float t = static_cast<float>(i) / sr;

			// ビブラートLFO（セント単位の周波数変調）
			float freqModCents = 0.0f;
			if (params.vibratoSpeed > 0.0f && params.vibratoDepth > 0.0f)
			{
				const float lfo = std::sin(PI2 * params.vibratoSpeed * t);
				freqModCents = lfo * params.vibratoDepth;
			}

			float sampleSum = 0.0f;
			for (int v = 0; v < numVoices; ++v)
			{
				const float vFreq = freqs[v] * std::pow(2.0f, freqModCents / 1200.0f);
				float s = 0.0f;

				if (params.useFm)
				{
					// OPNA (YM2608) スタイルFM合成
					// output = sin(2π * fc * t + I * env * sin(2π * fm * t + fb))
					const auto& fm = params.fmPreset;

					// モジュレータADSRでインデックスを時間変化させる
					// （PC-98の音色変化の核心: アタック時に明るく、ディケイで丸くなる）
					float modEnv = 1.0f;
					if (params.useAdsr)
					{
						modEnv = computeAdsr(i, sr, params.durationSec, gateSamples, fm.modAdsr);
					}

					// フィードバック（前サンプルの出力をモジュレータ入力に加算）
					const float fbVal = prevSample[v] * fm.feedback;

					// モジュレータ: sin(2π * ratio * freq * t + feedback)
					const float modOutput = std::sin(PI2 * fm.ratio * vFreq * t + fbVal);

					// 変調指数 × モジュレータエンベロープ
					const float modulated = modOutput * fm.index * modEnv;

					// キャリア: sin(2π * freq * t + modulatedValue)
					s = std::sin(PI2 * vFreq * t + modulated);

					prevSample[v] = s; // フィードバック用に保持
				}
				else
				{
					const float phase = std::fmod(vFreq * t, 1.0f);
					s = generateWaveform(params.wave, phase, params.dutyRatio, noiseState);
				}
				sampleSum += s;
			}
			sampleSum /= static_cast<float>(numVoices);

			// キャリアエンベロープ
			float env = 1.0f;
			if (params.useAdsr)
			{
				env = computeAdsr(i, sr, params.durationSec, gateSamples, params.adsr);
			}
			else
			{
				env = computeSimpleEnvelope(i, sr, gateSamples);
			}

			buf[i] = static_cast<std::int16_t>(
				std::clamp(sampleSum * amp * env, -32767.0f, 32767.0f));
		}

		return buf;
	}

	/// @brief FMプリセットを取得する（OPNA風8プリセット）
	/// @param index プリセット番号（0-7）
	/// @return FMプリセットパラメータ
	[[nodiscard]] static FmPreset getFmPreset(int index) noexcept
	{
		switch (index)
		{
		case 0: // Piano — 明るいアタック、モジュレータ急速ディケイ
			return {1.0f, 5.0f, 0.0f, {0.001f, 0.01f, 0.0f, 0.3f}};
		case 1: // Bell — 非整数比率、モジュレータ緩やかディケイ
			return {3.5f, 4.0f, 0.0f, {0.001f, 0.8f, 0.1f, 1.0f}};
		case 2: // Brass — フィードバック + モジュレータ持続
			return {1.0f, 3.5f, 0.4f, {0.02f, 0.1f, 0.7f, 0.15f}};
		case 3: // Strings — 低インデックス、緩やかアタック
			return {2.0f, 1.5f, 0.0f, {0.05f, 0.2f, 0.6f, 0.3f}};
		case 4: // Organ — 全体的に持続
			return {1.0f, 2.0f, 0.2f, {0.003f, 0.05f, 0.9f, 0.02f}};
		case 5: // E.Piano — 中程度ディケイ、温かい音色
			return {2.0f, 3.0f, 0.0f, {0.001f, 0.15f, 0.2f, 0.4f}};
		case 6: // Bass — 高速ディケイ、パンチのある音色
			return {1.0f, 4.0f, 0.3f, {0.001f, 0.05f, 0.0f, 0.1f}};
		case 7: // Flute — 極低インデックス、純粋な音色
			return {1.0f, 0.7f, 0.0f, {0.02f, 0.1f, 0.5f, 0.2f}};
		default:
			return {1.0f, 2.0f, 0.0f, {0.001f, 0.1f, 0.3f, 0.2f}};
		}
	}

	/// @brief サンプルレートを取得する
	[[nodiscard]] std::uint32_t sampleRate() const noexcept { return m_config.sampleRate; }

private:
	static constexpr float PI = 3.14159265358979323846f;
	AudioConfig m_config;

	/// @brief 波形生成ヘルパー（デューティ比対応）
	/// @param wave 波形タイプ
	/// @param phase 位相 [0,1)
	/// @param dutyRatio デューティ比（矩形波用）
	/// @param noiseState ノイズ用LFSR状態
	/// @return サンプル値 [-1,1]
	static float generateWaveform(
		WaveType wave,
		float phase,
		float dutyRatio,
		std::uint32_t& noiseState) noexcept
	{
		switch (wave)
		{
		case WaveType::Square:
			return (phase < dutyRatio) ? 1.0f : -1.0f;
		case WaveType::Triangle:
			return (phase < 0.5f)
				? (4.0f * phase - 1.0f)
				: (3.0f - 4.0f * phase);
		case WaveType::Sine:
			return std::sin(2.0f * PI * phase);
		case WaveType::Sawtooth:
			return 2.0f * phase - 1.0f;
		case WaveType::Noise:
			noiseState ^= noiseState << 13;
			noiseState ^= noiseState >> 17;
			noiseState ^= noiseState << 5;
			return (static_cast<float>(noiseState & 0xFFFF) / 32767.5f) - 1.0f;
		default:
			return std::sin(2.0f * PI * phase);
		}
	}

	/// @brief ADSRエンベロープ計算
	/// @param sampleIdx サンプルインデックス
	/// @param sr サンプルレート
	/// @param totalDur ノート全体の持続時間（秒）
	/// @param gateSamples ゲートサンプル数
	/// @param adsr ADSRパラメータ
	/// @return エンベロープ値 [0,1]
	[[nodiscard]] static float computeAdsr(
		std::uint32_t sampleIdx,
		float sr,
		float totalDur,
		std::uint32_t gateSamples,
		const AdsrEnvelope& adsr) noexcept
	{
		const float t = static_cast<float>(sampleIdx) / sr;
		const float gateTime = static_cast<float>(gateSamples) / sr;

		// アタックフェーズ
		if (t < adsr.attack)
		{
			return (adsr.attack > 0.0f) ? (t / adsr.attack) : 1.0f;
		}

		// ディケイフェーズ
		const float afterAttack = t - adsr.attack;
		if (afterAttack < adsr.decay)
		{
			return (adsr.decay > 0.0f)
				? (1.0f - (1.0f - adsr.sustain) * (afterAttack / adsr.decay))
				: adsr.sustain;
		}

		// サステインフェーズ → リリース開始
		const float sustainEnd = gateTime - adsr.release;
		if (t < sustainEnd)
		{
			return adsr.sustain;
		}

		// リリースフェーズ
		if (adsr.release > 0.0f && t < gateTime)
		{
			const float releaseT = t - sustainEnd;
			return adsr.sustain * std::max(0.0f, 1.0f - releaseT / adsr.release);
		}

		return 0.0f;
	}

	/// @brief 簡易エンベロープ（アタック5ms + リリース10ms）
	/// @param sampleIdx サンプルインデックス
	/// @param sr サンプルレート
	/// @param gateSamples ゲートサンプル数
	/// @return エンベロープ値 [0,1]
	[[nodiscard]] static float computeSimpleEnvelope(
		std::uint32_t sampleIdx,
		float sr,
		std::uint32_t gateSamples) noexcept
	{
		const float attackEnd = 0.005f * sr;
		const float releaseStart = static_cast<float>(gateSamples) - 0.01f * sr;
		const float fi = static_cast<float>(sampleIdx);

		if (fi < attackEnd)
		{
			return fi / attackEnd;
		}
		if (fi > releaseStart && releaseStart > 0.0f)
		{
			return (static_cast<float>(gateSamples) - fi)
				/ (static_cast<float>(gateSamples) - releaseStart);
		}
		return 1.0f;
	}
};

} // namespace mitiru_mml
