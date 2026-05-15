#pragma once

/// @file AudioEffectChain.hpp
/// @brief オーディオエフェクトチェーン（リバーブ/コーラス/ディレイ）
/// @details DSPエフェクトのチェーン処理。各エフェクトはIAudioEffectインターフェースを実装。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace mitiru::audio
{

/// @brief オーディオエフェクト基底クラス
class IAudioEffect
{
public:
	virtual ~IAudioEffect() = default;
	virtual void process(float* samples, int numSamples, int sampleRate) = 0;
	[[nodiscard]] virtual const char* name() const noexcept = 0;
	virtual void setWetDry(float wet) { m_wet = std::clamp(wet, 0.0f, 1.0f); }
	[[nodiscard]] float wetDry() const noexcept { return m_wet; }
	void setEnabled(bool e) noexcept { m_enabled = e; }
	[[nodiscard]] bool isEnabled() const noexcept { return m_enabled; }
protected:
	float m_wet = 0.5f;
	bool m_enabled = true;
};

/// @brief シンプルリバーブ（Schroederモデル簡易版）
class ReverbEffect : public IAudioEffect
{
public:
	[[nodiscard]] const char* name() const noexcept override { return "Reverb"; }
	void setDecay(float d) noexcept { m_decay = std::clamp(d, 0.0f, 0.99f); }
	void setRoomSize(float s) noexcept { m_roomSize = std::clamp(s, 0.0f, 1.0f); }

	void process(float* samples, int numSamples, [[maybe_unused]] int sampleRate) override
	{
		if (!m_enabled) return;
		if (m_buffer.empty()) { m_buffer.resize(kBufSize, 0.0f); }
		for (int i = 0; i < numSamples; ++i)
		{
			const float in = samples[i];
			const float delayed = m_buffer[m_pos];
			m_buffer[m_pos] = in + delayed * m_decay * m_roomSize;
			m_pos = (m_pos + 1) % kBufSize;
			samples[i] = in * (1.0f - m_wet) + delayed * m_wet;
		}
	}
private:
	static constexpr int kBufSize = 44100; // 1秒@44.1kHz
	std::vector<float> m_buffer;
	int m_pos = 0;
	float m_decay = 0.7f;
	float m_roomSize = 0.5f;
};

/// @brief コーラスエフェクト
class ChorusEffect : public IAudioEffect
{
public:
	[[nodiscard]] const char* name() const noexcept override { return "Chorus"; }
	void setRate(float hz) noexcept { m_rate = hz; }
	void setDepth(float ms) noexcept { m_depth = ms; }

	void process(float* samples, int numSamples, int sampleRate) override
	{
		if (!m_enabled) return;
		const int maxDelay = sampleRate / 10; // 100ms max
		if (static_cast<int>(m_buffer.size()) < maxDelay) { m_buffer.resize(static_cast<size_t>(maxDelay), 0.0f); }
		for (int i = 0; i < numSamples; ++i)
		{
			m_buffer[static_cast<size_t>(m_writePos)] = samples[i];
			const float lfo = std::sin(m_phase) * m_depth * 0.001f * static_cast<float>(sampleRate);
			const float delayF = 10.0f + lfo; // base 10 samples + modulation
			const int delaySamples = std::clamp(static_cast<int>(delayF), 1, maxDelay - 1);
			int readPos = m_writePos - delaySamples;
			if (readPos < 0) readPos += maxDelay;
			const float delayed = m_buffer[static_cast<size_t>(readPos)];
			samples[i] = samples[i] * (1.0f - m_wet) + delayed * m_wet;
			m_writePos = (m_writePos + 1) % maxDelay;
			m_phase += 2.0f * 3.14159265f * m_rate / static_cast<float>(sampleRate);
			if (m_phase > 6.28318f) m_phase -= 6.28318f;
		}
	}
private:
	std::vector<float> m_buffer;
	int m_writePos = 0;
	float m_phase = 0.0f;
	float m_rate = 1.5f;  // LFO Hz
	float m_depth = 5.0f; // ms
};

/// @brief ディレイエフェクト
class DelayEffect : public IAudioEffect
{
public:
	[[nodiscard]] const char* name() const noexcept override { return "Delay"; }
	void setDelayMs(float ms) noexcept { m_delayMs = std::clamp(ms, 1.0f, 2000.0f); }
	void setFeedback(float fb) noexcept { m_feedback = std::clamp(fb, 0.0f, 0.95f); }

	void process(float* samples, int numSamples, int sampleRate) override
	{
		if (!m_enabled) return;
		const int delaySamples = static_cast<int>(m_delayMs * 0.001f * static_cast<float>(sampleRate));
		const int bufSize = std::max(delaySamples + 1, 1);
		if (static_cast<int>(m_buffer.size()) < bufSize) { m_buffer.resize(static_cast<size_t>(bufSize), 0.0f); }
		for (int i = 0; i < numSamples; ++i)
		{
			int readPos = m_writePos - delaySamples;
			if (readPos < 0) readPos += bufSize;
			const float delayed = m_buffer[static_cast<size_t>(readPos)];
			m_buffer[static_cast<size_t>(m_writePos)] = samples[i] + delayed * m_feedback;
			samples[i] = samples[i] * (1.0f - m_wet) + delayed * m_wet;
			m_writePos = (m_writePos + 1) % bufSize;
		}
	}
private:
	std::vector<float> m_buffer;
	int m_writePos = 0;
	float m_delayMs = 300.0f;
	float m_feedback = 0.4f;
};

/// @brief エフェクトチェーン
class AudioEffectChain
{
public:
	void addEffect(std::unique_ptr<IAudioEffect> effect)
	{
		m_effects.push_back(std::move(effect));
	}

	void process(float* samples, int numSamples, int sampleRate)
	{
		for (auto& fx : m_effects)
		{
			if (fx->isEnabled())
			{
				fx->process(samples, numSamples, sampleRate);
			}
		}
	}

	[[nodiscard]] size_t effectCount() const noexcept { return m_effects.size(); }
	[[nodiscard]] IAudioEffect* getEffect(size_t idx) { return idx < m_effects.size() ? m_effects[idx].get() : nullptr; }

	/// @brief デフォルトチェーンを生成する(リバーブ+コーラス+ディレイ)
	static AudioEffectChain createDefault()
	{
		AudioEffectChain chain;
		chain.addEffect(std::make_unique<ReverbEffect>());
		chain.addEffect(std::make_unique<ChorusEffect>());
		chain.addEffect(std::make_unique<DelayEffect>());
		return chain;
	}

private:
	std::vector<std::unique_ptr<IAudioEffect>> m_effects;
};

} // namespace mitiru::audio
