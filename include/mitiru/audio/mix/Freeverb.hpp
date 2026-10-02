#pragma once

/// @file Freeverb.hpp
/// @brief Jezar の Freeverb によるステレオ残響。並列コムフィルタ 8 本と直列オールパスフィルタ 4 本を使う。public domain。
/// @details dry を含まない残響だけを返す。送る量は接続側の MiniaudioSfxBus の送り音量で決める。遅延時間は原典の 44.1 kHz の値をサンプルレートに合わせて伸ばす。
///          バッファは init で確保し、process では確保しない。減衰後の値が非正規化数になると CPU 時間が急増するため、0 にする。

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mitiru::audio::mix
{

class Freeverb
{
public:
	void init(std::uint32_t sampleRate)
	{
		const float scale = static_cast<float>(sampleRate) / 44100.0f;
		for (std::size_t i = 0; i < kCombs; ++i)
		{
			m_combL[i].resize(len(kCombTuning[i], scale));
			m_combR[i].resize(len(kCombTuning[i] + kStereoSpread, scale));
		}
		for (std::size_t i = 0; i < kAllpasses; ++i)
		{
			m_allL[i].resize(len(kAllpassTuning[i], scale));
			m_allR[i].resize(len(kAllpassTuning[i] + kStereoSpread, scale));
		}
		setRoomSize(m_room);
		setDamping(m_damp);
	}

	/// @param room 0 以上 1 以下。大きいほど長く響く
	void setRoomSize(float room) noexcept
	{
		m_room = clamp01(room);
		m_feedback = m_room * 0.28f + 0.7f;
	}

	/// @param damping 0 以上 1 以下。大きいほど高域が早く減る
	void setDamping(float damping) noexcept
	{
		m_damp = clamp01(damping);
		m_damp1 = m_damp * 0.4f;
	}

	[[nodiscard]] float roomSize() const noexcept { return m_room; }
	[[nodiscard]] float damping() const noexcept { return m_damp; }

	/// @brief インターリーブされたステレオの in から、残響だけを out に書く。in と out は同じ領域でもよい
	void process(const float* in, float* out, std::size_t frames) noexcept
	{
		for (std::size_t f = 0; f < frames; ++f)
		{
			const float input = (in[f * 2] + in[f * 2 + 1]) * kFixedGain;
			float l = 0.0f;
			float r = 0.0f;
			for (std::size_t i = 0; i < kCombs; ++i)
			{
				l += m_combL[i].process(input, m_feedback, m_damp1);
				r += m_combR[i].process(input, m_feedback, m_damp1);
			}
			for (std::size_t i = 0; i < kAllpasses; ++i)
			{
				l = m_allL[i].process(l);
				r = m_allR[i].process(r);
			}
			out[f * 2] = l * kWetScale;
			out[f * 2 + 1] = r * kWetScale;
		}
	}

private:
	static constexpr std::size_t kCombs = 8;
	static constexpr std::size_t kAllpasses = 4;
	static constexpr int kStereoSpread = 23;
	static constexpr std::array<int, kCombs> kCombTuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
	static constexpr std::array<int, kAllpasses> kAllpassTuning{556, 441, 341, 225};
	static constexpr float kFixedGain = 0.015f;
	static constexpr float kWetScale = 3.0f;

	static float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
	static std::size_t len(int samples44k, float scale)
	{
		return static_cast<std::size_t>(std::lround(static_cast<float>(samples44k) * scale)) + 1;
	}
	static float flush(float v) noexcept { return (std::fabs(v) < 1e-20f) ? 0.0f : v; }

	struct Comb
	{
		std::vector<float> buf;
		std::size_t        idx = 0;
		float              store = 0.0f;

		void resize(std::size_t n) { buf.assign(n, 0.0f); idx = 0; store = 0.0f; }
		float process(float input, float feedback, float damp) noexcept
		{
			const float out = buf[idx];
			store = flush(out * (1.0f - damp) + store * damp);
			buf[idx] = flush(input + store * feedback);
			if (++idx >= buf.size()) { idx = 0; }
			return out;
		}
	};

	struct Allpass
	{
		std::vector<float> buf;
		std::size_t        idx = 0;

		void resize(std::size_t n) { buf.assign(n, 0.0f); idx = 0; }
		float process(float input) noexcept
		{
			const float delayed = buf[idx];
			buf[idx] = flush(input + delayed * 0.5f);
			if (++idx >= buf.size()) { idx = 0; }
			return delayed - input;
		}
	};

	std::array<Comb, kCombs>        m_combL{};
	std::array<Comb, kCombs>        m_combR{};
	std::array<Allpass, kAllpasses> m_allL{};
	std::array<Allpass, kAllpasses> m_allR{};
	float m_room = 0.5f;
	float m_damp = 0.5f;
	float m_feedback = 0.84f;
	float m_damp1 = 0.2f;
};

}  // namespace mitiru::audio::mix
