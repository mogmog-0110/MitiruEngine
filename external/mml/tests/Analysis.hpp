#pragma once
/// @file Analysis.hpp
/// @brief 聞く代わりに数字で確かめるための測定 (周波数、RMS、ピーク、重心、相関、立ち上がりと減衰の時間)

#include "PcmCompare.hpp"
#include "Sha256.hpp"

#include <mitiru_mml/StereoBuffer.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mitiru_mml_test
{

inline constexpr double TWO_PI = 6.283185307179586476925286766559;

/// @brief ステレオの片側 (0 = L、1 = R) を取り出す
[[nodiscard]] inline std::vector<float> channelOf(const mitiru_mml::StereoBuffer& b, int side)
{
	std::vector<float> out(b.frames());
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		out[i] = b.samples[i * 2 + static_cast<std::size_t>(side)];
	}
	return out;
}

/// @brief L と R の平均
[[nodiscard]] inline std::vector<float> monoOf(const mitiru_mml::StereoBuffer& b)
{
	std::vector<float> out(b.frames());
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		out[i] = 0.5f * (b.samples[i * 2] + b.samples[i * 2 + 1]);
	}
	return out;
}

[[nodiscard]] inline double rmsOf(const std::vector<float>& x, std::size_t start, std::size_t count)
{
	count = std::min(count, x.size() > start ? x.size() - start : 0);
	if (count == 0) return 0.0;
	double acc = 0.0;
	for (std::size_t i = 0; i < count; ++i)
	{
		const double v = static_cast<double>(x[start + i]);
		acc += v * v;
	}
	return std::sqrt(acc / static_cast<double>(count));
}

[[nodiscard]] inline double toDb(double linear)
{
	return linear > 1.0e-12 ? 20.0 * std::log10(linear) : -240.0;
}

[[nodiscard]] inline double rmsDbOf(const std::vector<float>& x, std::size_t start, std::size_t count)
{
	return toDb(rmsOf(x, start, count));
}

[[nodiscard]] inline double peakOf(const std::vector<float>& x)
{
	double p = 0.0;
	for (const float v : x) p = std::max(p, std::fabs(static_cast<double>(v)));
	return p;
}

[[nodiscard]] inline double peakOfStereo(const mitiru_mml::StereoBuffer& b)
{
	double p = 0.0;
	for (const float v : b.samples) p = std::max(p, std::fabs(static_cast<double>(v)));
	return p;
}

/// @brief 区間のスペクトル (Hann ウィンドウ)。点数は 2 のべき乗に切り下げる
struct Spectrum
{
	std::vector<double> mag;   ///< 0 〜 ナイキスト
	double binHz = 0.0;
};

[[nodiscard]] inline Spectrum spectrumOf(const std::vector<float>& x, std::size_t start, std::size_t count, double rate)
{
	std::size_t n = 1;
	while (n * 2 <= count) n *= 2;
	Spectrum s;
	if (start + n > x.size() || n < 8)
	{
		return s;
	}
	std::vector<std::complex<double>> a(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		const double w = 0.5 - 0.5 * std::cos(TWO_PI * static_cast<double>(i) / static_cast<double>(n));
		a[i] = {static_cast<double>(x[start + i]) * w, 0.0};
	}
	detail::fftInPlace(a);
	s.mag.resize(n / 2);
	for (std::size_t k = 0; k < s.mag.size(); ++k) s.mag[k] = std::abs(a[k]);
	s.binHz = rate / static_cast<double>(n);
	return s;
}

/// @brief 周波数 hz の成分の振幅 (Hann ウィンドウつきの DFT を、その周波数でぴったり計算する。ビンの中心に合わせなくてよい)
[[nodiscard]] inline double toneAmplitude(const std::vector<float>& x, std::size_t start, std::size_t count, double hz, double rate)
{
	count = std::min(count, x.size() > start ? x.size() - start : 0);
	double re = 0.0;
	double im = 0.0;
	double wsum = 0.0;
	for (std::size_t i = 0; i < count; ++i)
	{
		const double w = 0.5 - 0.5 * std::cos(TWO_PI * static_cast<double>(i) / static_cast<double>(count));
		const double phase = TWO_PI * hz * static_cast<double>(i) / rate;
		re += static_cast<double>(x[start + i]) * w * std::cos(phase);
		im += static_cast<double>(x[start + i]) * w * std::sin(phase);
		wsum += w;
	}
	return wsum > 0.0 ? 2.0 * std::sqrt(re * re + im * im) / wsum : 0.0;
}

/// @brief 最大の山の周波数 (放物線補間つき)。minHz より下は探さない
[[nodiscard]] inline double peakHzOf(const Spectrum& s, double minHz = 20.0)
{
	if (s.mag.size() < 8) return 0.0;
	std::size_t k0 = std::max<std::size_t>(2, static_cast<std::size_t>(minHz / s.binHz));
	std::size_t peak = k0;
	for (std::size_t k = k0; k + 1 < s.mag.size(); ++k)
	{
		if (s.mag[k] > s.mag[peak]) peak = k;
	}
	const double a = std::log(s.mag[peak - 1] + 1.0e-30);
	const double b = std::log(s.mag[peak] + 1.0e-30);
	const double c = std::log(s.mag[peak + 1] + 1.0e-30);
	const double denom = a - 2.0 * b + c;
	const double delta = (denom != 0.0) ? 0.5 * (a - c) / denom : 0.0;
	return (static_cast<double>(peak) + delta) * s.binHz;
}

[[nodiscard]] inline double centroidHzOf(const Spectrum& s)
{
	double weighted = 0.0;
	double total = 0.0;
	for (std::size_t k = 1; k < s.mag.size(); ++k)
	{
		weighted += s.mag[k] * static_cast<double>(k);
		total += s.mag[k];
	}
	return total > 0.0 ? weighted / total * s.binHz : 0.0;
}

/// @brief loHz 以上 hiHz 未満のエネルギーが全体に占める割合
[[nodiscard]] inline double bandRatio(const Spectrum& s, double loHz, double hiHz)
{
	double band = 0.0;
	double total = 0.0;
	for (std::size_t k = 1; k < s.mag.size(); ++k)
	{
		const double hz = static_cast<double>(k) * s.binHz;
		const double p = s.mag[k] * s.mag[k];
		total += p;
		if (hz >= loHz && hz < hiHz) band += p;
	}
	return total > 0.0 ? band / total : 0.0;
}

[[nodiscard]] inline double centsBetween(double measuredHz, double expectedHz)
{
	return 1200.0 * std::log2(measuredHz / expectedHz);
}

[[nodiscard]] inline double midiHz(double key)
{
	return 440.0 * std::pow(2.0, (key - 69.0) / 12.0);
}

/// @brief L と R の相関係数 (1 でモノラル、0 で無相関)
[[nodiscard]] inline double stereoCorrelation(const mitiru_mml::StereoBuffer& b, std::size_t start, std::size_t count)
{
	count = std::min(count, b.frames() > start ? b.frames() - start : 0);
	double sl = 0.0, sr = 0.0, sll = 0.0, srr = 0.0, slr = 0.0;
	for (std::size_t i = 0; i < count; ++i)
	{
		const double l = static_cast<double>(b.samples[(start + i) * 2]);
		const double r = static_cast<double>(b.samples[(start + i) * 2 + 1]);
		sl += l;
		sr += r;
		sll += l * l;
		srr += r * r;
		slr += l * r;
	}
	const double n = static_cast<double>(count);
	const double cov = slr / n - (sl / n) * (sr / n);
	const double vl = sll / n - (sl / n) * (sl / n);
	const double vr = srr / n - (sr / n) * (sr / n);
	return (vl > 0.0 && vr > 0.0) ? cov / std::sqrt(vl * vr) : 0.0;
}

/// @brief ウィンドウごとの RMS (dB) の列。立ち上がりと減衰の形を見る
[[nodiscard]] inline std::vector<double> windowedRmsDb(const std::vector<float>& x, std::size_t window)
{
	std::vector<double> out;
	for (std::size_t s = 0; s + window <= x.size(); s += window)
	{
		out.push_back(rmsDbOf(x, s, window));
	}
	return out;
}

/// @brief 最初に |x| が threshold を超えるサンプル位置 (無ければ SIZE_MAX)
[[nodiscard]] inline std::size_t firstAbove(const std::vector<float>& x, double threshold, std::size_t from = 0)
{
	for (std::size_t i = from; i < x.size(); ++i)
	{
		if (std::fabs(static_cast<double>(x[i])) > threshold) return i;
	}
	return static_cast<std::size_t>(-1);
}

/// @brief float ステレオの内容をそのまま (IEEE754 の float を並べて) ハッシュする
[[nodiscard]] inline std::string hashFloats(const std::vector<float>& v)
{
	std::vector<std::uint8_t> bytes(v.size() * sizeof(float));
	if (!v.empty()) std::memcpy(bytes.data(), v.data(), bytes.size());
	return Sha256::hex(bytes.data(), bytes.size());
}

/// @brief 16bit に直した PCM をハッシュする (リトルエンディアン)
[[nodiscard]] inline std::string hashPcm16(const std::vector<std::int16_t>& pcm)
{
	std::vector<std::uint8_t> bytes;
	bytes.reserve(pcm.size() * 2);
	for (const auto s : pcm)
	{
		bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(s) & 0xFF));
		bytes.push_back(static_cast<std::uint8_t>((static_cast<std::uint16_t>(s) >> 8) & 0xFF));
	}
	return Sha256::hex(bytes.data(), bytes.size());
}

} // namespace mitiru_mml_test
