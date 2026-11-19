#pragma once
/// @file PcmCompare.hpp
/// @brief 2 本の 16bit PCM の差を数字にする (最大差、RMS、帯域ごとのスペクトルの差)

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mitiru_mml_test
{

/// @brief PCM の差の測定結果
struct PcmDiff
{
	static constexpr int BAND_COUNT = 10;

	std::size_t lengthA = 0;      ///< 基準の長さ
	std::size_t lengthB = 0;      ///< 比べる側の長さ
	int maxAbsDiff = 0;           ///< サンプルの差の最大 (LSB)
	double rmsDiff = 0.0;         ///< 差の RMS (LSB)
	double rmsRef = 0.0;          ///< 基準の RMS (LSB)
	double diffToSignalDb = -300.0; ///< 20*log10(rmsDiff / rmsRef)。小さいほど一致
	double diffFullScaleDb = -300.0; ///< 20*log10(rmsDiff / 32768)
	/// @brief 帯域ごと (オクターブ) の「差のパワー / 基準のパワー」(dB)。基準にパワーが無い帯域は -300
	std::array<double, BAND_COUNT> bandDiffToSignalDb{};
	double worstBandDb = -300.0;  ///< 基準のパワーが全体の 1e-6 以上ある帯域のうち、最悪の値
	bool identical = false;       ///< 長さもサンプルも完全に同じ
};

/// @brief オクターブ帯域の上端 (Hz)。最後の帯域は ナイキストまで
inline constexpr std::array<double, PcmDiff::BAND_COUNT> PCM_BAND_EDGES_HZ = {
	125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0, 16000.0, 1.0e9};

namespace detail
{

inline void fftInPlace(std::vector<std::complex<double>>& a)
{
	const std::size_t n = a.size();
	for (std::size_t i = 1, j = 0; i < n; ++i)
	{
		std::size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
		{
			j ^= bit;
		}
		j ^= bit;
		if (i < j)
		{
			std::swap(a[i], a[j]);
		}
	}
	for (std::size_t len = 2; len <= n; len <<= 1)
	{
		const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
		const std::complex<double> wl(std::cos(ang), std::sin(ang));
		for (std::size_t i = 0; i < n; i += len)
		{
			std::complex<double> w(1.0, 0.0);
			for (std::size_t k = 0; k < len / 2; ++k)
			{
				const auto u = a[i + k];
				const auto v = a[i + k + len / 2] * w;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

} // namespace detail

/// @brief 基準 a と比べる側 b の差を測る
/// @param sampleRate 帯域の境界を Hz から bin に直すのに使う
[[nodiscard]] inline PcmDiff comparePcm(
	const std::vector<std::int16_t>& a,
	const std::vector<std::int16_t>& b,
	double sampleRate = 44100.0)
{
	PcmDiff d;
	d.lengthA = a.size();
	d.lengthB = b.size();
	d.identical = (a == b);
	d.bandDiffToSignalDb.fill(-300.0);

	const std::size_t n = std::min(a.size(), b.size());
	if (n == 0)
	{
		return d;
	}

	double sumSqDiff = 0.0;
	double sumSqRef = 0.0;
	for (std::size_t i = 0; i < n; ++i)
	{
		const int diff = static_cast<int>(b[i]) - static_cast<int>(a[i]);
		d.maxAbsDiff = std::max(d.maxAbsDiff, std::abs(diff));
		sumSqDiff += static_cast<double>(diff) * diff;
		sumSqRef += static_cast<double>(a[i]) * a[i];
	}
	d.rmsDiff = std::sqrt(sumSqDiff / static_cast<double>(n));
	d.rmsRef = std::sqrt(sumSqRef / static_cast<double>(n));
	if (d.rmsDiff > 0.0 && d.rmsRef > 0.0)
	{
		d.diffToSignalDb = 20.0 * std::log10(d.rmsDiff / d.rmsRef);
	}
	if (d.rmsDiff > 0.0)
	{
		d.diffFullScaleDb = 20.0 * std::log10(d.rmsDiff / 32768.0);
	}
	if (d.identical)
	{
		return d;
	}

	// 2048 点の Hann ウィンドウで、全フレームのパワースペクトルを平均する
	constexpr std::size_t FRAME = 2048;
	const std::size_t hop = FRAME / 2;
	std::vector<double> window(FRAME);
	for (std::size_t i = 0; i < FRAME; ++i)
	{
		window[i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i)
			/ static_cast<double>(FRAME));
	}

	std::array<double, PcmDiff::BAND_COUNT> refPower{};
	std::array<double, PcmDiff::BAND_COUNT> diffPower{};
	std::vector<std::complex<double>> specRef(FRAME);
	std::vector<std::complex<double>> specDiff(FRAME);

	for (std::size_t start = 0; start + FRAME <= n; start += hop)
	{
		for (std::size_t i = 0; i < FRAME; ++i)
		{
			const double r = static_cast<double>(a[start + i]);
			const double e = static_cast<double>(b[start + i]) - r;
			specRef[i] = {r * window[i], 0.0};
			specDiff[i] = {e * window[i], 0.0};
		}
		detail::fftInPlace(specRef);
		detail::fftInPlace(specDiff);
		for (std::size_t k = 1; k < FRAME / 2; ++k)
		{
			const double hz = static_cast<double>(k) * sampleRate / static_cast<double>(FRAME);
			std::size_t band = 0;
			while (band + 1 < PcmDiff::BAND_COUNT && hz >= PCM_BAND_EDGES_HZ[band])
			{
				++band;
			}
			refPower[band] += std::norm(specRef[k]);
			diffPower[band] += std::norm(specDiff[k]);
		}
	}

	double totalRef = 0.0;
	for (const double p : refPower)
	{
		totalRef += p;
	}
	for (int band = 0; band < PcmDiff::BAND_COUNT; ++band)
	{
		if (refPower[band] > 0.0 && diffPower[band] > 0.0)
		{
			d.bandDiffToSignalDb[band] = 10.0 * std::log10(diffPower[band] / refPower[band]);
		}
		if (totalRef > 0.0 && refPower[band] >= totalRef * 1.0e-6)
		{
			d.worstBandDb = std::max(d.worstBandDb, d.bandDiffToSignalDb[band]);
		}
	}
	return d;
}

} // namespace mitiru_mml_test
