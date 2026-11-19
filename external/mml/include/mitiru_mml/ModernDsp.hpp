#pragma once
/// @file ModernDsp.hpp
/// @brief 現代的な音源 (シンセ、ドラムシンセ、サンプラー) が共有する DSP の部品
/// @details 外部のライブラリは使わない。乱数は ModernRng だけを使い、状態はすべて部品の中に置く
///          (static の可変状態を持たないので、チップごとに別スレッドで描いても結果が変わらない)。

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru_mml::modern_dsp
{

inline constexpr double PI = 3.14159265358979323846;

/// @brief 音を作る制御の間隔 (サンプル)。フィルターの係数などをこの間隔で更新する
inline constexpr int CONTROL_BLOCK = 16;

/// @brief MIDI ノート番号 (小数可) → 周波数 (A4 = 69 = 440 Hz)
[[nodiscard]] inline double midiToHz(double key) noexcept
{
	return 440.0 * std::exp2((key - 69.0) / 12.0);
}

/// @brief セント → 周波数の倍率
[[nodiscard]] inline double centsToRatio(double cents) noexcept
{
	return std::exp2(cents / 1200.0);
}

/// @brief MML の V (0〜15) の振幅。2 乗の曲線 (V12 は約 -3.9 dB)
[[nodiscard]] inline float volumeCurve15(int v) noexcept
{
	const float x = static_cast<float>(std::clamp(v, 0, 15)) / 15.0f;
	return x * x;
}

/// @brief 0〜127 の音量の振幅。2 乗の曲線
[[nodiscard]] inline float volumeCurve127(int v) noexcept
{
	const float x = static_cast<float>(std::clamp(v, 0, 127)) / 127.0f;
	return x * x;
}

/// @brief V (0〜15) を 0〜127 に直す
[[nodiscard]] inline int volume15To127(int v) noexcept
{
	return (std::clamp(v, 0, 15) * 127 + 7) / 15;
}

/// @brief 定位 (0〜127、64 が中央) の左右の係数。等パワー
inline void panGains(float pan127, float& left, float& right) noexcept
{
	// 64 がちょうど中央 (左右が同じ) になるよう、64 の上下で傾きを分ける
	const float p = std::clamp(pan127, 0.0f, 127.0f);
	const float t = (p <= 64.0f) ? p * (0.5f / 64.0f) : 0.5f + (p - 64.0f) * (0.5f / 63.0f);
	const double angle = static_cast<double>(t) * (PI * 0.5);
	left = static_cast<float>(std::cos(angle));
	right = static_cast<float>(std::sin(angle));
}

/// @brief 目標へ指数的に近づく値 (パラメーターの急な変化によるクリックを避ける)
struct Smoothed
{
	float current = 0.0f;
	float target = 0.0f;

	void snap(float v) noexcept
	{
		current = v;
		target = v;
	}

	void step(float coef) noexcept
	{
		current += (target - current) * coef;
	}
};

/// @brief 時定数 tau 秒で blockSamples ぶん進めるときの 1 次の係数
[[nodiscard]] inline float smoothCoef(int blockSamples, double tauSeconds, double rate) noexcept
{
	if (tauSeconds <= 0.0)
	{
		return 1.0f;
	}
	return static_cast<float>(1.0 - std::exp(-static_cast<double>(blockSamples) / (tauSeconds * rate)));
}

/// @brief PolyBLEP の補正項。位相 t の不連続点の前後だけ値を持つ
[[nodiscard]] inline float polyBlep(double t, double dt) noexcept
{
	if (t < dt)
	{
		const double x = t / dt;
		return static_cast<float>(x + x - x * x - 1.0);
	}
	if (t > 1.0 - dt)
	{
		const double x = (t - 1.0) / dt;
		return static_cast<float>(x * x + x + x + 1.0);
	}
	return 0.0f;
}

/// @brief 帯域制限したノコギリ波 (位相 0〜1。-1 から 1 へ上がる)
[[nodiscard]] inline float sawBlep(double phase, double dt) noexcept
{
	return static_cast<float>(2.0 * phase - 1.0) - polyBlep(phase, dt);
}

/// @brief 帯域制限した矩形波 (pw はパルス幅 0〜1)
[[nodiscard]] inline float squareBlep(double phase, double dt, double pw) noexcept
{
	float v = (phase < pw) ? 1.0f : -1.0f;
	v += polyBlep(phase, dt);
	double shifted = phase - pw;
	if (shifted < 0.0)
	{
		shifted += 1.0;
	}
	v -= polyBlep(shifted, dt);
	return v;
}

/// @brief 三角波 (高調波が 1/n^2 で減るので、帯域制限なしでも折り返しは小さい)
[[nodiscard]] inline float triangleNaive(double phase) noexcept
{
	return static_cast<float>(4.0 * std::fabs(phase - 0.5) - 1.0);
}

/// @brief 位相を 0〜1 に保つ
inline void wrapPhase(double& phase) noexcept
{
	phase -= std::floor(phase);
}

/// @brief 状態変数フィルター (TPT 形式)。カットオフを速く動かしても安定する
class StateVariableFilter
{
public:
	enum class Type : std::uint8_t
	{
		LowPass,
		HighPass,
		BandPass,
	};

	/// @param cutoffHz カットオフ (サンプルレートの 0.45 倍までに丸める)
	/// @param resonance 0〜1。1 に近いと自己発振に近い
	void setup(double cutoffHz, float resonance, double rate) noexcept
	{
		const double fc = std::clamp(cutoffHz, 10.0, rate * 0.45);
		const double g = std::tan(PI * fc / rate);
		const double k = 2.0 - 1.96 * static_cast<double>(std::clamp(resonance, 0.0f, 1.0f));
		m_k = static_cast<float>(k);
		m_a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
		m_a2 = static_cast<float>(g) * m_a1;
		m_a3 = static_cast<float>(g) * m_a2;
	}

	[[nodiscard]] float process(float x, Type type) noexcept
	{
		const float v3 = x - m_ic2;
		const float v1 = m_a1 * m_ic1 + m_a2 * v3;
		const float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3;
		m_ic1 = 2.0f * v1 - m_ic1;
		m_ic2 = 2.0f * v2 - m_ic2;
		switch (type)
		{
		case Type::HighPass: return x - m_k * v1 - v2;
		case Type::BandPass: return v1 * m_k;
		case Type::LowPass:
		default: return v2;
		}
	}

	void reset() noexcept
	{
		m_ic1 = 0.0f;
		m_ic2 = 0.0f;
	}

private:
	float m_k = 2.0f;
	float m_a1 = 0.0f;
	float m_a2 = 0.0f;
	float m_a3 = 0.0f;
	float m_ic1 = 0.0f;
	float m_ic2 = 0.0f;
};

/// @brief ADSR エンベロープ。アタックは直線、ディケイとリリースは指数
/// @details 時間は「60 dB 下がるまでの時間」で指定する。0 の時間は次の段へ即座に移る
class Adsr
{
public:
	enum class Stage : std::uint8_t
	{
		Idle,
		Attack,
		Decay,
		Sustain,
		Release,
	};

	void configure(double attackSec, double decaySec, float sustain, double releaseSec, double rate) noexcept
	{
		m_attackStep = (attackSec > 0.0) ? static_cast<float>(1.0 / (attackSec * rate)) : 1.0f;
		m_decayCoef = coefFor(decaySec, rate);
		m_releaseCoef = coefFor(releaseSec, rate);
		m_sustain = std::clamp(sustain, 0.0f, 1.0f);
	}

	void trigger() noexcept
	{
		m_stage = Stage::Attack;
	}

	/// @brief リリースの長さ (60 dB 下がるまでの秒) を変えて、すぐにリリースへ入る
	void releaseWithin(double sec60, double rate) noexcept
	{
		m_releaseCoef = coefFor(sec60, rate);
		release();
	}

	void release() noexcept
	{
		if (m_stage != Stage::Idle)
		{
			m_stage = Stage::Release;
		}
	}

	/// @brief 1 サンプル進めて、その値 (0〜1) を返す
	[[nodiscard]] float next() noexcept
	{
		switch (m_stage)
		{
		case Stage::Attack:
			m_level += m_attackStep;
			if (m_level >= 1.0f)
			{
				m_level = 1.0f;
				m_stage = Stage::Decay;
			}
			break;
		case Stage::Decay:
			m_level = m_sustain + (m_level - m_sustain) * m_decayCoef;
			if (m_level - m_sustain < 1.0e-4f)
			{
				m_level = m_sustain;
				m_stage = Stage::Sustain;
			}
			break;
		case Stage::Sustain:
			m_level = m_sustain;
			break;
		case Stage::Release:
			m_level *= m_releaseCoef;
			if (m_level < 1.0e-4f)
			{
				m_level = 0.0f;
				m_stage = Stage::Idle;
			}
			break;
		case Stage::Idle:
		default:
			m_level = 0.0f;
			break;
		}
		if (m_stage == Stage::Sustain && m_sustain <= 0.0f)
		{
			m_level = 0.0f;
			m_stage = Stage::Idle;
		}
		return m_level;
	}

	[[nodiscard]] bool active() const noexcept
	{
		return m_stage != Stage::Idle;
	}

	[[nodiscard]] Stage stage() const noexcept
	{
		return m_stage;
	}

	[[nodiscard]] float level() const noexcept
	{
		return m_level;
	}

	void reset() noexcept
	{
		m_stage = Stage::Idle;
		m_level = 0.0f;
	}

private:
	[[nodiscard]] static float coefFor(double sec60, double rate) noexcept
	{
		if (sec60 <= 0.0)
		{
			return 0.0f;
		}
		// 60 dB (1/1000) 下がるまでに sec60 秒かかる係数
		return static_cast<float>(std::exp(-6.907755278982137 / (sec60 * rate)));
	}

	Stage m_stage = Stage::Idle;
	float m_level = 0.0f;
	float m_attackStep = 1.0f;
	float m_decayCoef = 0.0f;
	float m_releaseCoef = 0.0f;
	float m_sustain = 1.0f;
};

} // namespace mitiru_mml::modern_dsp
