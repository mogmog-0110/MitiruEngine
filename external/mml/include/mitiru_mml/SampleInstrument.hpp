#pragma once
/// @file SampleInstrument.hpp
/// @brief PCM サンプルベースの楽器（ピッチシフト + ADSR + ループ）。
/// @details FM/PSG の硬い音でなく、サンプル波形を音程に合わせて再生する「サンプラー音色」。
///          スーファミ/2000年代同人ゲーム級の質感を狙う(#9)。外部 WAV/PCM をサンプルに
///          できるほか、内蔵の合成サンプル（倍音加算）も用意する。
///
/// @code
/// auto inst = mitiru_mml::SampleInstrument::warmSynth(44100); // 内蔵の柔らか音色
/// auto pcm  = inst.renderNote(440.0f, 22050, 33075, 0.8f);    // A4, gate 0.5s, 全0.75s
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief サンプルベース楽器（モノラル）
class SampleInstrument
{
public:
	/// @brief ADSR エンベロープ（秒・サステインは 0..1）
	struct Adsr { float attack = 0.005f; float decay = 0.08f; float sustain = 0.7f; float release = 0.12f; };

	/// @brief PCM サンプルを設定する。
	/// @param pcm 16bit モノラルサンプル波形
	/// @param baseFreq そのサンプルが表す音程（Hz）
	/// @param sampleRate サンプルのレート（= 出力レート）
	void setSample(const PcmBuffer& pcm, float baseFreq, std::uint32_t sampleRate)
	{
		m_sample.resize(pcm.size());
		for (std::size_t i = 0; i < pcm.size(); ++i)
			m_sample[i] = static_cast<float>(pcm[i]) / 32768.0f;
		m_baseFreq = baseFreq > 0.0f ? baseFreq : 440.0f;
		m_sampleRate = sampleRate;
		m_loopStart = 0;
		m_loopEnd = 0; // 既定はワンショット。ループは setLoop() で明示する。
	}

	/// @brief サステイン中にループする区間を設定する（未指定ならワンショット）。
	void setLoop(std::size_t start, std::size_t end)
	{
		if (end > m_sample.size()) end = m_sample.size();
		if (start < end) { m_loopStart = start; m_loopEnd = end; }
	}

	void setAdsr(const Adsr& a) { m_adsr = a; }
	[[nodiscard]] std::uint32_t sampleRate() const noexcept { return m_sampleRate; }
	[[nodiscard]] bool valid() const noexcept { return !m_sample.empty(); }

	/// @brief 1 音を描画する。
	/// @param freq 目標周波数（Hz）。playbackRate = freq / baseFreq でリサンプル。
	/// @param gateSamples キーオン区間（サステインまで）のサンプル数
	/// @param totalSamples 出力総サンプル数（release の尾を含む）
	/// @param volume 0..1
	[[nodiscard]] PcmBuffer renderNote(float freq, int gateSamples, int totalSamples, float volume) const
	{
		PcmBuffer out;
		if (m_sample.empty() || totalSamples <= 0) return out;
		out.resize(static_cast<std::size_t>(totalSamples));

		const double step = static_cast<double>(freq) / static_cast<double>(m_baseFreq);
		const float sr = static_cast<float>(m_sampleRate);
		const float aT = m_adsr.attack * sr, dT = m_adsr.decay * sr, rT = std::max(1.0f, m_adsr.release * sr);
		const float sus = m_adsr.sustain;

		double pos = 0.0;
		for (int n = 0; n < totalSamples; ++n)
		{
			// サンプル位置（サステイン中はループ）
			if (pos >= static_cast<double>(m_sample.size() - 1))
			{
				if (m_loopEnd > m_loopStart + 1)
				{
					const double loopLen = static_cast<double>(m_loopEnd - m_loopStart);
					pos = static_cast<double>(m_loopStart) + std::fmod(pos - static_cast<double>(m_loopStart), loopLen);
				}
				else { break; } // ワンショット終了
			}
			const auto i0 = static_cast<std::size_t>(pos);
			const double frac = pos - static_cast<double>(i0);
			const float s0 = m_sample[i0];
			const float s1 = m_sample[std::min(i0 + 1, m_sample.size() - 1)];
			const float samp = s0 + static_cast<float>(frac) * (s1 - s0);

			// ADSR エンベロープ
			float env;
			const float fn = static_cast<float>(n);
			if (n < gateSamples)
			{
				if (fn < aT)            env = aT > 0.0f ? fn / aT : 1.0f;                 // attack
				else if (fn < aT + dT)  env = 1.0f - (1.0f - sus) * ((fn - aT) / std::max(1.0f, dT)); // decay
				else                    env = sus;                                        // sustain
			}
			else
			{
				const float rel = (fn - static_cast<float>(gateSamples)) / rT;            // release
				env = sus * (1.0f - std::clamp(rel, 0.0f, 1.0f));
			}

			out[static_cast<std::size_t>(n)] = static_cast<std::int16_t>(
				std::clamp(samp * env * volume, -1.0f, 1.0f) * 32767.0f);
			pos += step;
		}
		return out;
	}

	/// @brief 内蔵の柔らかい合成サンプル音色（倍音加算 + 緩い減衰）。
	/// @details FM より角の取れた、サンプラー的な基準波形を 1 周期超ぶん合成して作る。
	[[nodiscard]] static SampleInstrument warmSynth(std::uint32_t sampleRate)
	{
		const float baseFreq = 220.0f; // A3 を基準サンプルにする
		const int len = static_cast<int>(static_cast<float>(sampleRate) * 1.0f); // 1秒
		PcmBuffer pcm(static_cast<std::size_t>(len));
		// 倍音（1,2,3,4,5）を緩く重ね、軽いビブラート的揺らぎを足す。
		const float harm[5] = {1.0f, 0.5f, 0.28f, 0.16f, 0.08f};
		for (int n = 0; n < len; ++n)
		{
			const float t = static_cast<float>(n) / static_cast<float>(sampleRate);
			float v = 0.0f;
			for (int h = 0; h < 5; ++h)
				v += harm[h] * std::sin(2.0f * 3.14159265f * baseFreq * static_cast<float>(h + 1) * t);
			v *= 0.5f;
			pcm[static_cast<std::size_t>(n)] = static_cast<std::int16_t>(
				std::clamp(v, -1.0f, 1.0f) * 24000.0f);
		}
		SampleInstrument inst;
		inst.setSample(pcm, baseFreq, sampleRate);
		// 定常波なので全域ループ可。柔らかい ADSR。
		inst.setLoop(0, pcm.size());
		inst.setAdsr({0.01f, 0.15f, 0.6f, 0.25f});
		return inst;
	}

private:
	std::vector<float> m_sample;
	float m_baseFreq = 440.0f;
	std::uint32_t m_sampleRate = 44100;
	std::size_t m_loopStart = 0;
	std::size_t m_loopEnd = 0;
	Adsr m_adsr;
};

} // namespace mitiru_mml
