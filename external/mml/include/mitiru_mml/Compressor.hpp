#pragma once
/// @file Compressor.hpp
/// @brief フィードフォワード型コンプレッサー。突出した音量を抑え、混ぜたとき音が前に出る。
/// @details ピーク追従のエンベロープに対し、しきい値超過分を ratio で圧縮し、makeup で底上げする。
///          マスター段に薄くかけると複数トラックが団子にならず「曲」としてまとまる。
///          16bit モノラル PcmBuffer を入力し、適用済みを返す（入力は変更しない）。
///
/// @code
/// mitiru_mml::Compressor comp;
/// comp.setThresholdDb(-18.0f).setRatio(4.0f).setMakeupDb(3.0f);
/// auto glued = comp.process(pcm, 44100);
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief ピーク追従コンプレッサー（モノラル）
class Compressor
{
public:
	Compressor& setThresholdDb(float db) { m_thresholdDb = db; return *this; }
	Compressor& setRatio(float r)        { m_ratio = (r >= 1.0f) ? r : 1.0f; return *this; }   ///< 1=無圧縮
	Compressor& setAttackMs(float ms)    { m_attackMs = (ms > 0.0f) ? ms : 0.0f; return *this; }
	Compressor& setReleaseMs(float ms)   { m_releaseMs = (ms > 0.0f) ? ms : 0.0f; return *this; }
	Compressor& setMakeupDb(float db)    { m_makeupDb = db; return *this; }

	/// @brief PCM にコンプレッションを適用して返す（入力は変更しない）。
	[[nodiscard]] PcmBuffer process(const PcmBuffer& in, std::uint32_t sampleRate) const
	{
		if (in.empty()) { return in; }
		const float sr = static_cast<float>(sampleRate);

		// アタック/リリースの 1 極平滑化係数（時定数 → 係数）。
		const float aCoef = coef(m_attackMs, sr);
		const float rCoef = coef(m_releaseMs, sr);
		const float makeup = std::pow(10.0f, m_makeupDb / 20.0f);

		float env = 0.0f;  // 振幅エンベロープ（線形）
		PcmBuffer out(in.size());
		for (std::size_t n = 0; n < in.size(); ++n)
		{
			const float x   = static_cast<float>(in[n]) / 32768.0f;
			const float rect = std::fabs(x);

			// ピーク追従: 立ち上がりは aCoef、減衰は rCoef で平滑化。
			const float c = (rect > env) ? aCoef : rCoef;
			env = c * env + (1.0f - c) * rect;

			// しきい値超過分を dB で圧縮し、線形ゲインに戻す。
			float gain = 1.0f;
			if (env > 1e-6f)
			{
				const float envDb = 20.0f * std::log10(env);
				if (envDb > m_thresholdDb)
				{
					const float overDb     = envDb - m_thresholdDb;
					const float reducedDb  = overDb * (1.0f / m_ratio - 1.0f);  // 負値 = 減衰
					gain = std::pow(10.0f, reducedDb / 20.0f);
				}
			}

			const float y = x * gain * makeup;
			out[n] = static_cast<std::int16_t>(std::clamp(y, -1.0f, 1.0f) * 32767.0f);
		}
		return out;
	}

private:
	/// @brief 時定数(ms) → 1 極平滑化係数。ms=0 は即時追従(0)。
	[[nodiscard]] static float coef(float ms, float sr) noexcept
	{
		if (ms <= 0.0f) { return 0.0f; }
		return std::exp(-1.0f / (ms * 0.001f * sr));
	}

	float m_thresholdDb = -18.0f;
	float m_ratio       = 4.0f;
	float m_attackMs    = 10.0f;
	float m_releaseMs   = 120.0f;
	float m_makeupDb    = 0.0f;
};

} // namespace mitiru_mml
