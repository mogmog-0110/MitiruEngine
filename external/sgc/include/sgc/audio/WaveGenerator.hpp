#pragma once

/// @file WaveGenerator.hpp
/// @brief 手続き的な波形生成器
///
/// 正弦波・矩形波・三角波・ノコギリ波・ノイズ等の波形を生成し、
/// ADSRエンベロープやLFO変調を適用する。
///
/// @code
/// sgc::audio::WaveGenerator gen;
/// auto sine = gen.generateSine(440.0f, 0.5f, 1.0f);
/// auto env = gen.applyEnvelope(sine, 0.01f, 0.1f, 0.7f, 0.2f);
/// @endcode

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <numbers>
#include <numeric>
#include <functional>

namespace sgc::audio
{

/// @brief 波形の種類
enum class WaveType
{
	Sine,      ///< 正弦波
	Square,    ///< 矩形波
	Triangle,  ///< 三角波
	Sawtooth,  ///< ノコギリ波
	Noise,     ///< ホワイトノイズ
	Custom     ///< カスタム波形
};

/// @brief 波形生成パラメータ
struct WaveParams
{
	WaveType type = WaveType::Sine;   ///< 波形の種類
	float frequency = 440.0f;         ///< 周波数（Hz）
	float amplitude = 1.0f;           ///< 振幅
	float phase = 0.0f;               ///< 初期位相（ラジアン）
	float duration = 1.0f;            ///< 持続時間（秒）
	uint32_t noiseSeed = 12345;       ///< ノイズ用シード
};

/// @brief 手続き的な波形生成器
///
/// 各種波形の生成、ミキシング、ADSRエンベロープ、LFO変調をサポート。
/// ノイズ以外は決定的（同じパラメータで同じ結果）。
class WaveGenerator
{
public:
	/// @brief パラメータ構造体から波形を生成する
	/// @param params 波形パラメータ
	/// @param sampleRate サンプルレート（Hz）
	/// @return 生成されたサンプル列
	[[nodiscard]] std::vector<float> generate(
		const WaveParams& params,
		uint32_t sampleRate = 44100) const
	{
		switch (params.type)
		{
		case WaveType::Sine:
			return generateSineImpl(params.frequency, params.amplitude, params.phase, params.duration, sampleRate);
		case WaveType::Square:
			return generateSquareImpl(params.frequency, params.amplitude, params.phase, params.duration, sampleRate);
		case WaveType::Triangle:
			return generateTriangleImpl(params.frequency, params.amplitude, params.phase, params.duration, sampleRate);
		case WaveType::Sawtooth:
			return generateSawtoothImpl(params.frequency, params.amplitude, params.phase, params.duration, sampleRate);
		case WaveType::Noise:
			return generateNoiseImpl(params.amplitude, params.duration, sampleRate, params.noiseSeed);
		default:
			return {};
		}
	}

	/// @brief 正弦波を生成する
	/// @param frequency 周波数（Hz）
	/// @param amplitude 振幅
	/// @param duration 持続時間（秒）
	/// @param sampleRate サンプルレート（Hz）
	/// @return 生成されたサンプル列
	[[nodiscard]] std::vector<float> generateSine(
		float frequency,
		float amplitude = 1.0f,
		float duration = 1.0f,
		uint32_t sampleRate = 44100) const
	{
		return generateSineImpl(frequency, amplitude, 0.0f, duration, sampleRate);
	}

	/// @brief 矩形波を生成する
	/// @param frequency 周波数（Hz）
	/// @param amplitude 振幅
	/// @param duration 持続時間（秒）
	/// @param sampleRate サンプルレート（Hz）
	/// @return 生成されたサンプル列
	[[nodiscard]] std::vector<float> generateSquare(
		float frequency,
		float amplitude = 1.0f,
		float duration = 1.0f,
		uint32_t sampleRate = 44100) const
	{
		return generateSquareImpl(frequency, amplitude, 0.0f, duration, sampleRate);
	}

	/// @brief 三角波を生成する
	/// @param frequency 周波数（Hz）
	/// @param amplitude 振幅
	/// @param duration 持続時間（秒）
	/// @param sampleRate サンプルレート（Hz）
	/// @return 生成されたサンプル列
	[[nodiscard]] std::vector<float> generateTriangle(
		float frequency,
		float amplitude = 1.0f,
		float duration = 1.0f,
		uint32_t sampleRate = 44100) const
	{
		return generateTriangleImpl(frequency, amplitude, 0.0f, duration, sampleRate);
	}

	/// @brief ノコギリ波を生成する
	/// @param frequency 周波数（Hz）
	/// @param amplitude 振幅
	/// @param duration 持続時間（秒）
	/// @param sampleRate サンプルレート（Hz）
	/// @return 生成されたサンプル列
	[[nodiscard]] std::vector<float> generateSawtooth(
		float frequency,
		float amplitude = 1.0f,
		float duration = 1.0f,
		uint32_t sampleRate = 44100) const
	{
		return generateSawtoothImpl(frequency, amplitude, 0.0f, duration, sampleRate);
	}

	/// @brief ホワイトノイズを生成する
	/// @param amplitude 振幅
	/// @param duration 持続時間（秒）
	/// @param sampleRate サンプルレート（Hz）
	/// @param seed 乱数シード（決定的生成用）
	/// @return 生成されたサンプル列
	[[nodiscard]] std::vector<float> generateNoise(
		float amplitude = 1.0f,
		float duration = 1.0f,
		uint32_t sampleRate = 44100,
		uint32_t seed = 12345) const
	{
		return generateNoiseImpl(amplitude, duration, sampleRate, seed);
	}

	/// @brief 複数チャンネルをミックスする
	/// @param channels ミックスするチャンネル列
	/// @return ミックス結果（各サンプルの平均）
	[[nodiscard]] static std::vector<float> mix(const std::vector<std::vector<float>>& channels)
	{
		if (channels.empty()) return {};

		// 最長チャンネルのサイズに合わせる
		std::size_t maxLen = 0;
		for (const auto& ch : channels)
		{
			if (ch.size() > maxLen) maxLen = ch.size();
		}

		std::vector<float> result(maxLen, 0.0f);
		const float invCount = 1.0f / static_cast<float>(channels.size());

		for (const auto& ch : channels)
		{
			for (std::size_t i = 0; i < ch.size(); ++i)
			{
				result[i] += ch[i] * invCount;
			}
		}

		return result;
	}

	/// @brief ADSRエンベロープを適用する
	/// @param samples 入力サンプル列
	/// @param attack アタック時間（秒）
	/// @param decay ディケイ時間（秒）
	/// @param sustain サステインレベル（0〜1）
	/// @param release リリース時間（秒）
	/// @param sampleRate サンプルレート（Hz）
	/// @return エンベロープ適用後のサンプル列
	[[nodiscard]] static std::vector<float> applyEnvelope(
		const std::vector<float>& samples,
		float attack,
		float decay,
		float sustain,
		float release,
		uint32_t sampleRate = 44100)
	{
		if (samples.empty()) return {};

		const auto totalSamples = samples.size();
		std::vector<float> result(totalSamples);

		const auto attackSamples = static_cast<std::size_t>(attack * static_cast<float>(sampleRate));
		const auto decaySamples = static_cast<std::size_t>(decay * static_cast<float>(sampleRate));
		const auto releaseSamples = static_cast<std::size_t>(release * static_cast<float>(sampleRate));

		// リリース開始位置
		const std::size_t releaseStart =
			(totalSamples > releaseSamples) ? (totalSamples - releaseSamples) : 0;

		for (std::size_t i = 0; i < totalSamples; ++i)
		{
			float env = sustain;

			if (i < attackSamples)
			{
				// アタック: 0 → 1
				env = static_cast<float>(i) / static_cast<float>(attackSamples);
			}
			else if (i < attackSamples + decaySamples)
			{
				// ディケイ: 1 → sustain
				const float t = static_cast<float>(i - attackSamples) / static_cast<float>(decaySamples);
				env = 1.0f + (sustain - 1.0f) * t;
			}

			// リリース: env → 0
			if (i >= releaseStart)
			{
				const float releaseT = static_cast<float>(i - releaseStart) / static_cast<float>(releaseSamples);
				env *= (1.0f - releaseT);
			}

			result[i] = samples[i] * env;
		}

		return result;
	}

	/// @brief LFO（低周波発振）を適用する
	/// @param samples 入力サンプル列
	/// @param lfoFreq LFO周波数（Hz）
	/// @param lfoDepth LFO深度（0〜1、振幅変調の深さ）
	/// @param sampleRate サンプルレート（Hz）
	/// @return LFO適用後のサンプル列
	[[nodiscard]] static std::vector<float> applyLFO(
		const std::vector<float>& samples,
		float lfoFreq,
		float lfoDepth,
		uint32_t sampleRate = 44100)
	{
		if (samples.empty()) return {};

		std::vector<float> result(samples.size());
		const float twoPi = 2.0f * std::numbers::pi_v<float>;

		for (std::size_t i = 0; i < samples.size(); ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
			const float lfo = 1.0f - lfoDepth * (0.5f + 0.5f * std::sin(twoPi * lfoFreq * t));
			result[i] = samples[i] * lfo;
		}

		return result;
	}

private:
	/// @brief 正弦波の内部実装
	[[nodiscard]] static std::vector<float> generateSineImpl(
		float frequency, float amplitude, float phase,
		float duration, uint32_t sampleRate)
	{
		const auto numSamples = static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
		std::vector<float> result(numSamples);
		const float twoPi = 2.0f * std::numbers::pi_v<float>;

		for (std::size_t i = 0; i < numSamples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
			result[i] = amplitude * std::sin(twoPi * frequency * t + phase);
		}
		return result;
	}

	/// @brief 矩形波の内部実装
	[[nodiscard]] static std::vector<float> generateSquareImpl(
		float frequency, float amplitude, float phase,
		float duration, uint32_t sampleRate)
	{
		const auto numSamples = static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
		std::vector<float> result(numSamples);
		const float twoPi = 2.0f * std::numbers::pi_v<float>;

		for (std::size_t i = 0; i < numSamples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
			const float sinVal = std::sin(twoPi * frequency * t + phase);
			result[i] = (sinVal >= 0.0f) ? amplitude : -amplitude;
		}
		return result;
	}

	/// @brief 三角波の内部実装
	[[nodiscard]] static std::vector<float> generateTriangleImpl(
		float frequency, float amplitude, float phase,
		float duration, uint32_t sampleRate)
	{
		const auto numSamples = static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
		std::vector<float> result(numSamples);

		for (std::size_t i = 0; i < numSamples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
			// 位相を含む周期位置 [0, 1)
			float p = frequency * t + phase / (2.0f * std::numbers::pi_v<float>);
			p = p - std::floor(p); // [0, 1)に正規化

			// 三角波: 0→1→0→-1→0
			float value = 0.0f;
			if (p < 0.25f)
			{
				value = 4.0f * p;
			}
			else if (p < 0.75f)
			{
				value = 2.0f - 4.0f * p;
			}
			else
			{
				value = -4.0f + 4.0f * p;
			}
			result[i] = amplitude * value;
		}
		return result;
	}

	/// @brief ノコギリ波の内部実装
	[[nodiscard]] static std::vector<float> generateSawtoothImpl(
		float frequency, float amplitude, float phase,
		float duration, uint32_t sampleRate)
	{
		const auto numSamples = static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
		std::vector<float> result(numSamples);

		for (std::size_t i = 0; i < numSamples; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
			float p = frequency * t + phase / (2.0f * std::numbers::pi_v<float>);
			p = p - std::floor(p); // [0, 1)に正規化
			// ノコギリ波: -1 → +1 の線形
			result[i] = amplitude * (2.0f * p - 1.0f);
		}
		return result;
	}

	/// @brief ノイズの内部実装（LCG疑似乱数）
	[[nodiscard]] static std::vector<float> generateNoiseImpl(
		float amplitude, float duration,
		uint32_t sampleRate, uint32_t seed)
	{
		const auto numSamples = static_cast<std::size_t>(duration * static_cast<float>(sampleRate));
		std::vector<float> result(numSamples);

		uint32_t state = seed;
		for (std::size_t i = 0; i < numSamples; ++i)
		{
			// LCG: 線形合同法
			state = state * 1664525u + 1013904223u;
			// [0, 1)に変換して[-1, 1)にマッピング
			const float normalized = static_cast<float>(state) / static_cast<float>(0xFFFFFFFFu);
			result[i] = amplitude * (2.0f * normalized - 1.0f);
		}
		return result;
	}
};

} // namespace sgc::audio
