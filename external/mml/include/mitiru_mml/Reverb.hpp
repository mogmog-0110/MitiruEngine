#pragma once
/// @file Reverb.hpp
/// @brief マスターリバーブ（Freeverb 系: 並列コムフィルタ + 直列オールパス）。
/// @details FM/PSG/OPNA の硬い音に空間の広がりと残響を付け、スーファミ/同人ゲーム級の
///          質感に寄せる(#9)。16bit モノラル PcmBuffer をその場で（または複製して）処理する。
///
/// @code
/// mitiru_mml::Reverb rv;                 // 既定: 中程度の部屋
/// rv.setRoomSize(0.7f).setWet(0.3f);
/// auto wet = rv.process(dryPcm, 44100);  // dry はそのまま、wet を返す
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief Freeverb 系マスターリバーブ（モノラル）
class Reverb
{
public:
	Reverb& setRoomSize(float v) { m_roomSize = std::clamp(v, 0.0f, 0.98f); return *this; }
	Reverb& setDamping(float v)  { m_damping = std::clamp(v, 0.0f, 1.0f); return *this; }
	Reverb& setWet(float v)      { m_wet = std::clamp(v, 0.0f, 1.0f); return *this; }
	Reverb& setDry(float v)      { m_dry = std::clamp(v, 0.0f, 1.0f); return *this; }

	/// @brief 1 サンプルずつ進めるリバーブの状態。process() と ModernSong の送りバスが同じ式を使う
	/// @details 入力は int16 を 32768 で割った float。戻り値は wet の成分だけ (dry は含まない)。
	class Stream
	{
	public:
		Stream(const Reverb& settings, std::uint32_t sampleRate)
			: m_wet(settings.m_wet)
		{
			const float scale = static_cast<float>(sampleRate) / 44100.0f;
			for (int i = 0; i < 8; ++i) m_comb[i].assign(scaleLen(COMB[i], scale), 0.0f);
			for (int i = 0; i < 4; ++i) m_allp[i].assign(scaleLen(ALLP[i], scale), 0.0f);
			m_feedback = settings.m_roomSize * 0.28f + 0.7f; // roomSize→帰還量
			m_damp1 = settings.m_damping;
			m_damp2 = 1.0f - settings.m_damping;
		}

		[[nodiscard]] float wet(float x)
		{
			const float input = x * kInputGain;
			float wetSum = 0.0f;

			// 並列コムフィルタ（各々 damping 付き帰還）
			for (int c = 0; c < 8; ++c)
			{
				auto& buf = m_comb[c];
				const float y = buf[m_ci[c]];
				m_combStore[c] = y * m_damp2 + m_combStore[c] * m_damp1;     // ローパス
				buf[m_ci[c]] = input + m_combStore[c] * m_feedback;
				if (++m_ci[c] >= buf.size()) m_ci[c] = 0;
				wetSum += y;
			}

			// 直列オールパス
			float s = wetSum;
			for (int a = 0; a < 4; ++a)
			{
				auto& buf = m_allp[a];
				const float bufed = buf[m_ai[a]];
				const float y = -s + bufed;
				buf[m_ai[a]] = s + bufed * 0.5f;
				if (++m_ai[a] >= buf.size()) m_ai[a] = 0;
				s = y;
			}
			return s * m_wet * 3.0f; // wet を聴感上同程度に
		}

	private:
		static constexpr float kInputGain = 0.015f; ///< Freeverb の固定入力ゲイン相当

		std::array<std::vector<float>, 8> m_comb;
		std::array<float, 8> m_combStore{};   // damping 用 1-pole 状態
		std::array<std::size_t, 8> m_ci{};
		std::array<std::vector<float>, 4> m_allp;
		std::array<std::size_t, 4> m_ai{};
		float m_feedback = 0.0f;
		float m_damp1 = 0.0f;
		float m_damp2 = 0.0f;
		float m_wet = 0.0f;
	};

	/// @brief dry PCM を入力し、リバーブ適用済み PCM を返す（入力は変更しない）。
	/// @param in 16bit モノラル PCM
	/// @param sampleRate サンプルレート（遅延長のスケールに使う）
	[[nodiscard]] PcmBuffer process(const PcmBuffer& in, std::uint32_t sampleRate) const
	{
		if (in.empty()) return in;
		Stream stream(*this, sampleRate);

		PcmBuffer out;
		out.resize(in.size());
		for (std::size_t n = 0; n < in.size(); ++n)
		{
			const float dryF = static_cast<float>(in[n]) / 32768.0f;
			const float wetPart = stream.wet(dryF);
			const float mixed = dryF * m_dry + wetPart;
			out[n] = static_cast<std::int16_t>(
				std::clamp(mixed, -1.0f, 1.0f) * 32767.0f);
		}
		return out;
	}

private:
	// Freeverb 標準の遅延長（44100Hz 基準）。サンプルレートに合わせて伸縮する。
	static constexpr int COMB[8]   = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
	static constexpr int ALLP[4]   = {556, 441, 341, 225};

	[[nodiscard]] static std::size_t scaleLen(int base, float scale)
	{
		const auto v = static_cast<std::size_t>(static_cast<float>(base) * scale);
		return v < 1 ? 1 : v;
	}

	float m_roomSize = 0.6f;
	float m_damping = 0.5f;
	float m_wet = 0.28f;
	float m_dry = 0.9f;
};

} // namespace mitiru_mml
