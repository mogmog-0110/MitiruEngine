#pragma once

/// @file BusEq.hpp
/// @brief バスに掛けるパラメトリック EQ。RBJ Audio EQ Cookbook の biquad を最大 8 本、直列に並べる
/// @details 係数は miniaudio の ma_peak2 / ma_loshelf2 / ma_hishelf2 / ma_hpf2 / ma_bpf2 / ma_notch2 と同じ式だが、
///          miniaudio の node は ma_biquad_reinit を音声の thread と同時に呼ぶことになり (MiniaudioSmoothLowPass.hpp と同じ理由)、
///          場所の切り替えで係数を動かすと雑音が出るため、係数の更新を 64 フレームごとに音声の thread の中で行う自前の処理にした。
///          帯域の種類が変わる所は、掛かり具合 (wet) をいったん 0 へ下げてから入れ替えて上げ直す。同じ種類なら周波数・ゲイン・Q だけを滑らかに動かす。
///          帯域が全部 Off で wet が 0 なら、入力をそのまま返す。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mitiru::audio::mix
{

enum class EqBandType : std::uint8_t
{
	Off = 0, Peaking, LowShelf, HighShelf, HighPass, LowPass, BandPass, Notch,
};

struct EqBand
{
	EqBandType type = EqBandType::Off;
	float      hz = 1000.0f;
	float      gainDb = 0.0f;    ///< Peaking / LowShelf / HighShelf だけが使う
	float      q = 0.70710678f;

	[[nodiscard]] bool operator==(const EqBand& o) const noexcept
	{
		return type == o.type && hz == o.hz && gainDb == o.gainDb && q == o.q;
	}
};

inline constexpr std::size_t kEqMaxBands = 8;
using EqBands = std::array<EqBand, kEqMaxBands>;

inline constexpr float kEqMinHz = 10.0f;
inline constexpr float kEqMaxHz = 20000.0f;
inline constexpr float kEqMinQ = 0.1f;
inline constexpr float kEqMaxQ = 20.0f;
inline constexpr float kEqMaxGainDb = 24.0f;

[[nodiscard]] inline bool eqBandUsesGain(EqBandType t) noexcept
{
	return t == EqBandType::Peaking || t == EqBandType::LowShelf || t == EqBandType::HighShelf;
}

[[nodiscard]] inline bool eqTypeFromName(std::string_view name, EqBandType& out) noexcept
{
	struct Entry { const char* name; EqBandType type; };
	static constexpr Entry kNames[] = {
		{"peaking", EqBandType::Peaking}, {"lowShelf", EqBandType::LowShelf}, {"highShelf", EqBandType::HighShelf},
		{"highPass", EqBandType::HighPass}, {"lowPass", EqBandType::LowPass}, {"bandPass", EqBandType::BandPass},
		{"notch", EqBandType::Notch}};
	for (const Entry& e : kNames)
	{
		if (name == e.name) { out = e.type; return true; }
	}
	return false;
}

/// @return 範囲を外れていれば理由。空なら使える
[[nodiscard]] inline std::string validateEqBand(const EqBand& b, float sampleRate = 48000.0f)
{
	const float maxHz = std::min(kEqMaxHz, sampleRate * 0.45f);
	if (!(b.hz >= kEqMinHz && b.hz <= maxHz)) { return "hz は " + std::to_string(static_cast<int>(kEqMinHz)) + " 以上、" + std::to_string(static_cast<int>(maxHz)) + " 以下"; }
	if (!(b.q >= kEqMinQ && b.q <= kEqMaxQ)) { return "q は 0.1 以上、20 以下"; }
	if (!(std::fabs(b.gainDb) <= kEqMaxGainDb)) { return "gainDb は -24 以上、24 以下"; }
	return {};
}

struct BiquadCoeffs
{
	double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;   ///< a0 で割った後
};

/// @brief RBJ Cookbook の式。Off は素通し (係数が 1, 0, 0, 0, 0)
[[nodiscard]] inline BiquadCoeffs eqBiquad(const EqBand& band, double sampleRate) noexcept
{
	if (band.type == EqBandType::Off) { return {}; }
	constexpr double kPi = 3.14159265358979323846;
	const double hz = std::clamp(static_cast<double>(band.hz), static_cast<double>(kEqMinHz), sampleRate * 0.45);
	const double q = std::clamp(static_cast<double>(band.q), static_cast<double>(kEqMinQ), static_cast<double>(kEqMaxQ));
	const double w0 = 2.0 * kPi * hz / sampleRate;
	const double s = std::sin(w0);
	const double c = std::cos(w0);
	const double alpha = s / (2.0 * q);
	const double A = std::pow(10.0, static_cast<double>(band.gainDb) / 40.0);
	const double sqA2alpha = 2.0 * std::sqrt(A) * alpha;
	double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
	switch (band.type)
	{
	case EqBandType::Peaking:
		b0 = 1 + alpha * A; b1 = -2 * c; b2 = 1 - alpha * A;
		a0 = 1 + alpha / A; a1 = -2 * c; a2 = 1 - alpha / A;
		break;
	case EqBandType::LowShelf:
		b0 = A * ((A + 1) - (A - 1) * c + sqA2alpha);
		b1 = 2 * A * ((A - 1) - (A + 1) * c);
		b2 = A * ((A + 1) - (A - 1) * c - sqA2alpha);
		a0 = (A + 1) + (A - 1) * c + sqA2alpha;
		a1 = -2 * ((A - 1) + (A + 1) * c);
		a2 = (A + 1) + (A - 1) * c - sqA2alpha;
		break;
	case EqBandType::HighShelf:
		b0 = A * ((A + 1) + (A - 1) * c + sqA2alpha);
		b1 = -2 * A * ((A - 1) + (A + 1) * c);
		b2 = A * ((A + 1) + (A - 1) * c - sqA2alpha);
		a0 = (A + 1) - (A - 1) * c + sqA2alpha;
		a1 = 2 * ((A - 1) - (A + 1) * c);
		a2 = (A + 1) - (A - 1) * c - sqA2alpha;
		break;
	case EqBandType::HighPass:
		b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2;
		a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
		break;
	case EqBandType::LowPass:
		b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2;
		a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
		break;
	case EqBandType::BandPass:   // 中心のゲインが 0 dB
		b0 = alpha; b1 = 0; b2 = -alpha;
		a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
		break;
	case EqBandType::Notch:
		b0 = 1; b1 = -2 * c; b2 = 1;
		a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
		break;
	case EqBandType::Off: break;
	}
	return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

/// @brief 係数から求めた周波数応答の大きさ (dB)。試験と mitiru verify の確かめ用
[[nodiscard]] inline double eqResponseDb(const BiquadCoeffs& k, double hz, double sampleRate) noexcept
{
	constexpr double kPi = 3.14159265358979323846;
	const double w = 2.0 * kPi * hz / sampleRate;
	const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
	const double nr = k.b0 + k.b1 * c1 + k.b2 * c2, ni = -(k.b1 * s1 + k.b2 * s2);
	const double dr = 1 + k.a1 * c1 + k.a2 * c2, di = -(k.a1 * s1 + k.a2 * s2);
	return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

/// @brief 帯域を直列に通す処理。音声の thread だけが触る。目標は setTarget で渡す
class EqProcessor
{
public:
	static constexpr std::uint32_t kBlockFrames = 64;
	static constexpr std::uint32_t kMaxChannels = 2;

	/// @param fadeSec 場所を切り替えるときに、新しい設定へ移りきるまでの秒。0 なら次のブロックで移る
	void init(float sampleRate, std::uint32_t channels, float fadeSec) noexcept
	{
		m_rate = static_cast<double>(sampleRate);
		m_channels = std::min(channels, kMaxChannels);
		const double fade = std::max(static_cast<double>(fadeSec), 0.0);
		const double blockSec = static_cast<double>(kBlockFrames) / m_rate;
		m_smooth = (fade > 0.0) ? 1.0 - std::exp(-blockSec / (fade * 0.25)) : 1.0;
		m_wetStep = (fade > 0.0) ? blockSec / (fade * 0.5) : 1.0;
		for (Slot& s : m_slots) { s = Slot{}; }
	}

	/// @param immediate true なら滑らかに動かさず、すぐその設定にする。最初の設定に使う
	void setTarget(const EqBands& bands, bool immediate = false) noexcept
	{
		for (std::size_t i = 0; i < kEqMaxBands; ++i)
		{
			Slot& s = m_slots[i];
			s.target = bands[i];
			if (immediate)
			{
				s.cur = toState(bands[i]);
				s.wet = s.wetFrom = (bands[i].type == EqBandType::Off) ? 0.0f : 1.0f;
				s.dirty = true;
				clearHistory(s);
			}
		}
	}

	/// @brief frames フレームを処理する。in と out は同じ配列でもよい (インターリーブ)
	void process(const float* in, float* out, std::uint32_t frames) noexcept
	{
		if (in != out) { std::copy_n(in, static_cast<std::size_t>(frames) * m_channels, out); }
		for (std::uint32_t done = 0; done < frames; done += kBlockFrames)
		{
			const std::uint32_t n = std::min(kBlockFrames, frames - done);
			float* block = out + static_cast<std::size_t>(done) * m_channels;
			for (Slot& s : m_slots) { advance(s, n); runSlot(s, block, n); }
		}
	}

	/// @brief 全部 Off で、掛かり具合も 0 のとき true (入力をそのまま返す)
	[[nodiscard]] bool idle() const noexcept
	{
		for (const Slot& s : m_slots)
		{
			if (s.cur.type != EqBandType::Off || s.target.type != EqBandType::Off || s.wet != 0.0f) { return false; }
		}
		return true;
	}

private:
	struct State
	{
		EqBandType type = EqBandType::Off;
		float      log2Hz = 10.0f;
		float      gainDb = 0.0f;
		float      q = 0.70710678f;
	};

	struct Slot
	{
		State        cur;
		EqBand       target;
		float        wet = 0.0f;          ///< 0 = 素通し、1 = 帯域を通した音。ブロックの終わりの値
		float        wetFrom = 0.0f;      ///< ブロックの頭の値。標本ごとに wet まで直線で動かす
		bool         dirty = true;        ///< 係数を作り直す
		BiquadCoeffs k;
		double       z1[kMaxChannels]{};
		double       z2[kMaxChannels]{};
	};

	[[nodiscard]] static State toState(const EqBand& b) noexcept
	{
		return {b.type, std::log2(std::max(b.hz, kEqMinHz)), b.gainDb, b.q};
	}

	static void clearHistory(Slot& s) noexcept
	{
		for (std::uint32_t c = 0; c < kMaxChannels; ++c) { s.z1[c] = 0.0; s.z2[c] = 0.0; }
	}

	/// ブロックの頭で、目標へ向けて n フレーム分だけ進める。端数のブロックでも、動く速さは秒で決まる
	void advance(Slot& s, std::uint32_t n) noexcept
	{
		const double share = static_cast<double>(n) / static_cast<double>(kBlockFrames);
		float wetGoal = 0.0f;
		if (s.target.type == s.cur.type)
		{
			if (s.cur.type != EqBandType::Off)
			{
				wetGoal = 1.0f;
				const State t = toState(s.target);
				const float k = static_cast<float>(1.0 - std::pow(1.0 - m_smooth, share));
				const State before = s.cur;
				s.cur.log2Hz = settle(s.cur.log2Hz, t.log2Hz, k, 1e-4f);
				s.cur.gainDb = settle(s.cur.gainDb, t.gainDb, k, 1e-3f);
				s.cur.q = settle(s.cur.q, t.q, k, 1e-4f);
				if (s.cur.log2Hz != before.log2Hz || s.cur.gainDb != before.gainDb || s.cur.q != before.q) { s.dirty = true; }
			}
		}
		else if (s.wet <= 0.0f)
		{
			s.cur = toState(s.target);   // 種類の入れ替えは、素通しになっている間に行う
			s.dirty = true;
			clearHistory(s);
			wetGoal = (s.cur.type == EqBandType::Off) ? 0.0f : 1.0f;
		}
		const float step = static_cast<float>(m_wetStep * share);
		s.wetFrom = s.wet;
		s.wet = (std::fabs(wetGoal - s.wet) <= step) ? wetGoal : s.wet + std::copysign(step, wetGoal - s.wet);
	}

	[[nodiscard]] static float settle(float cur, float goal, float k, float eps) noexcept
	{
		const float next = cur + (goal - cur) * k;
		return (std::fabs(goal - next) < eps) ? goal : next;
	}

	void runSlot(Slot& s, float* block, std::uint32_t n) noexcept
	{
		if (s.cur.type == EqBandType::Off && s.wet == 0.0f && s.wetFrom == 0.0f) { return; }
		if (s.dirty)
		{
			s.k = eqBiquad({s.cur.type, std::exp2(s.cur.log2Hz), s.cur.gainDb, s.cur.q}, m_rate);
			s.dirty = false;
		}
		const float from = s.wetFrom;
		const float to = s.wet;
		for (std::uint32_t i = 0; i < n; ++i)
		{
			const float w = from + (to - from) * static_cast<float>(i + 1) / static_cast<float>(n);
			for (std::uint32_t c = 0; c < m_channels; ++c)
			{
				const std::size_t at = static_cast<std::size_t>(i) * m_channels + c;
				const double x = block[at];
				const double y = s.k.b0 * x + s.z1[c];
				s.z1[c] = s.k.b1 * x - s.k.a1 * y + s.z2[c];
				s.z2[c] = s.k.b2 * x - s.k.a2 * y;
				block[at] = static_cast<float>(x + (y - x) * static_cast<double>(w));
			}
		}
	}

	double m_rate = 48000.0;
	double m_smooth = 0.1;
	double m_wetStep = 0.1;
	std::uint32_t m_channels = 2;
	std::array<Slot, kEqMaxBands> m_slots{};
};

}  // namespace mitiru::audio::mix
