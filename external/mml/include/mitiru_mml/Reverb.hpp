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

	/// @brief dry PCM を入力し、リバーブ適用済み PCM を返す（入力は変更しない）。
	/// @param in 16bit モノラル PCM
	/// @param sampleRate サンプルレート（遅延長のスケールに使う）
	[[nodiscard]] PcmBuffer process(const PcmBuffer& in, std::uint32_t sampleRate) const
	{
		if (in.empty()) return in;
		const float scale = static_cast<float>(sampleRate) / 44100.0f;

		// Freeverb 標準の遅延長（44100Hz 基準）をサンプルレートに合わせる。
		static constexpr int COMB[8]   = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
		static constexpr int ALLP[4]   = {556, 441, 341, 225};

		std::array<std::vector<float>, 8> comb;
		std::array<float, 8> combStore{};   // damping 用 1-pole 状態
		std::array<std::size_t, 8> ci{};
		for (int i = 0; i < 8; ++i) comb[i].assign(scaleLen(COMB[i], scale), 0.0f);

		std::array<std::vector<float>, 4> allp;
		std::array<std::size_t, 4> ai{};
		for (int i = 0; i < 4; ++i) allp[i].assign(scaleLen(ALLP[i], scale), 0.0f);

		const float feedback = m_roomSize * 0.28f + 0.7f; // roomSize→帰還量
		const float damp1 = m_damping;
		const float damp2 = 1.0f - m_damping;
		constexpr float kInputGain = 0.015f;               // Freeverb の固定入力ゲイン相当

		PcmBuffer out;
		out.resize(in.size());
		for (std::size_t n = 0; n < in.size(); ++n)
		{
			const float input = static_cast<float>(in[n]) / 32768.0f * kInputGain;
			float wetSum = 0.0f;

			// 並列コムフィルタ（各々 damping 付き帰還）
			for (int c = 0; c < 8; ++c)
			{
				auto& buf = comb[c];
				const float y = buf[ci[c]];
				combStore[c] = y * damp2 + combStore[c] * damp1;     // ローパス
				buf[ci[c]] = input + combStore[c] * feedback;
				if (++ci[c] >= buf.size()) ci[c] = 0;
				wetSum += y;
			}

			// 直列オールパス
			float s = wetSum;
			for (int a = 0; a < 4; ++a)
			{
				auto& buf = allp[a];
				const float bufed = buf[ai[a]];
				const float y = -s + bufed;
				buf[ai[a]] = s + bufed * 0.5f;
				if (++ai[a] >= buf.size()) ai[a] = 0;
				s = y;
			}

			const float dryF = static_cast<float>(in[n]) / 32768.0f;
			const float mixed = dryF * m_dry + s * m_wet * 3.0f; // wet を聴感上同程度に
			out[n] = static_cast<std::int16_t>(
				std::clamp(mixed, -1.0f, 1.0f) * 32767.0f);
		}
		return out;
	}

private:
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
