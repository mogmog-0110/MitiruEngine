#pragma once
/// @file Delay.hpp
/// @brief フィードバックディレイ（やまびこ）。リバーブより前に置くと音に奥行きと反復が出る。
/// @details 16bit モノラル PcmBuffer を入力し、ディレイ適用済みを返す（入力は変更しない）。
///          出力長は入力と同じ（末尾の反復を録りたいときは入力に無音を足してから process する）。
///
/// @code
/// mitiru_mml::Delay d;
/// d.setTime(0.25f).setFeedback(0.4f).setWet(0.3f); // 250ms, 40% 反復
/// auto wet = d.process(dryPcm, 44100);
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief フィードバックディレイ（モノラル）
class Delay
{
public:
	Delay& setTime(float sec)     { m_timeSec = (sec > 0.0f) ? sec : 0.0f; return *this; }
	Delay& setFeedback(float v)   { m_feedback = std::clamp(v, 0.0f, 0.95f); return *this; }  ///< 0.95 上限で発散を防ぐ
	Delay& setWet(float v)        { m_wet = std::clamp(v, 0.0f, 1.0f); return *this; }
	Delay& setDry(float v)        { m_dry = std::clamp(v, 0.0f, 1.0f); return *this; }

	/// @brief 1 サンプルずつ処理するディレイの状態。process() と ModernSong の送りバスで同じ式を使う。
	/// @details 入力は int16 を 32768 で割った float。戻り値は反復成分だけ。
	class Stream
	{
	public:
		Stream(const Delay& settings, std::uint32_t sampleRate)
			: m_line(std::max<std::size_t>(1, static_cast<std::size_t>(settings.m_timeSec * static_cast<float>(sampleRate))), 0.0f)
			, m_feedback(settings.m_feedback)
			, m_wet(settings.m_wet)
		{
		}

		[[nodiscard]] float wet(float x)
		{
			const float echo = m_line[m_idx];             // d サンプル前の遅延信号
			m_line[m_idx] = x + echo * m_feedback;        // 入力 + 帰還を書き戻す
			if (++m_idx >= m_line.size()) { m_idx = 0; }
			return echo * m_wet;
		}

	private:
		std::vector<float> m_line;
		std::size_t m_idx = 0;
		float m_feedback;
		float m_wet;
	};

	/// @brief dry PCM を入力し、ディレイ適用済み PCM を返す（入力は変更しない）。
	[[nodiscard]] PcmBuffer process(const PcmBuffer& in, std::uint32_t sampleRate) const
	{
		if (in.empty()) { return in; }
		Stream stream(*this, sampleRate);

		PcmBuffer out(in.size());
		for (std::size_t n = 0; n < in.size(); ++n)
		{
			const float x    = static_cast<float>(in[n]) / 32768.0f;
			const float y    = x * m_dry + stream.wet(x);
			out[n] = static_cast<std::int16_t>(std::clamp(y, -1.0f, 1.0f) * 32767.0f);
		}
		return out;
	}

private:
	float m_timeSec  = 0.25f;
	float m_feedback = 0.35f;
	float m_wet      = 0.3f;
	float m_dry      = 1.0f;
};

} // namespace mitiru_mml
