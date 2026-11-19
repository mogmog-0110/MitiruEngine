#pragma once
/// @file ChipPitch.hpp
/// @brief 任意のチップの Driver に 1 音を鳴らさせ、基音の周波数を測る (どのチップにも共通)
/// @details Device をネイティブレートで動かし、約 25 kHz になるように箱型で平均してからウィンドウをかけて FFT し、
///          最大の山の周りを細かく刻んだ直接の DFT で山の頂点を求める。FFT の格子 (約 1.5 Hz) より
///          ずっと細かい値が出るので、±1 セントの検査の測定誤差が小さい。

#include "PcmCompare.hpp"

#include <mitiru_mml/ChipDriver.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace mitiru_mml_test
{

/// @brief 440 Hz を基準にした、MIDI ノート番号の周波数
[[nodiscard]] inline double keyHz(int key)
{
	return 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
}

/// @brief 期待値に対する測定値のずれ (セント)
[[nodiscard]] inline double centsBetween(double measuredHz, double expectedHz)
{
	return 1200.0 * std::log2(measuredHz / expectedHz);
}

/// @brief 1 音の測定結果
struct DriverTone
{
	double peakHz = 0.0;    ///< 基音の周波数
	double rmsDb = -300.0;  ///< 測定区間の RMS (dBFS、L と R の平均)
	double peakDb = -300.0; ///< 測定区間の絶対値の最大 (dBFS)
};

/// @brief ウィンドウ付きの DFT の振幅を、周波数 hz で直接求める
[[nodiscard]] inline double dftMagnitude(const std::vector<double>& x, const std::vector<double>& window,
	double hz, double rate)
{
	std::complex<double> acc(0.0, 0.0);
	const double step = -2.0 * 3.14159265358979323846 * hz / rate;
	// 位相は 1 サンプルずつ回す。サンプル数が 1.6 万なので、誤差は無視できる
	std::complex<double> rot(1.0, 0.0);
	const std::complex<double> delta(std::cos(step), std::sin(step));
	for (std::size_t i = 0; i < x.size(); ++i)
	{
		acc += rot * (x[i] * window[i]);
		rot *= delta;
		if ((i & 1023u) == 1023u)
		{
			rot /= std::abs(rot);
		}
	}
	return std::abs(acc);
}

/// @brief モノラルの波形から基音の周波数と音量を求める
/// @param x 先頭の 16384 点を使う (直流は取り除く)
/// @param rate x のサンプルレート (Hz)
[[nodiscard]] inline DriverTone analyzeMono(std::vector<double> x, double rate)
{
	constexpr std::size_t POINTS = 16384;
	x.resize(POINTS, 0.0);
	double mean = 0.0;
	for (const double v : x)
	{
		mean += v;
	}
	mean /= static_cast<double>(POINTS);
	double sumSq = 0.0;
	double peakAbs = 0.0;
	std::vector<double> window(POINTS);
	std::vector<std::complex<double>> spec(POINTS);
	for (std::size_t i = 0; i < POINTS; ++i)
	{
		x[i] -= mean;
		sumSq += x[i] * x[i];
		peakAbs = std::max(peakAbs, std::abs(x[i]));
		window[i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i)
			/ static_cast<double>(POINTS));
		spec[i] = {x[i] * window[i], 0.0};
	}
	detail::fftInPlace(spec);

	std::size_t peak = 3;
	double best = 0.0;
	for (std::size_t k = 3; k + 1 < POINTS / 2; ++k)
	{
		const double m = std::abs(spec[k]);
		if (m > best)
		{
			best = m;
			peak = k;
		}
	}

	// 山の頂点を黄金分割探索で求める (±1 格子の範囲)
	const double bin = rate / static_cast<double>(POINTS);
	double lo = (static_cast<double>(peak) - 1.0) * bin;
	double hi = (static_cast<double>(peak) + 1.0) * bin;
	const double phi = 0.6180339887498949;
	double a = hi - phi * (hi - lo);
	double b = lo + phi * (hi - lo);
	double fa = dftMagnitude(x, window, a, rate);
	double fb = dftMagnitude(x, window, b, rate);
	for (int iter = 0; iter < 40; ++iter)
	{
		if (fa < fb)
		{
			lo = a;
			a = b;
			fa = fb;
			b = lo + phi * (hi - lo);
			fb = dftMagnitude(x, window, b, rate);
		}
		else
		{
			hi = b;
			b = a;
			fb = fa;
			a = hi - phi * (hi - lo);
			fa = dftMagnitude(x, window, a, rate);
		}
	}

	DriverTone tone;
	tone.peakHz = 0.5 * (lo + hi);
	const double rms = std::sqrt(sumSq / static_cast<double>(POINTS));
	tone.rmsDb = (rms > 0.0) ? 20.0 * std::log10(rms) : -300.0;
	tone.peakDb = (peakAbs > 0.0) ? 20.0 * std::log10(peakAbs) : -300.0;
	return tone;
}

/// @brief すでに発音している driver のチャンネルの、基音の周波数と音量を測る
/// @param skipSeconds 発音の頭を捨てる時間 (秒)
/// @details noteOn は呼び出し側で済ませておく。Device の状態を進めるので 1 回きりの測定。
[[nodiscard]] inline DriverTone measureSounding(mitiru_mml::ChipDriver& driver, double skipSeconds = 0.1)
{
	constexpr std::size_t POINTS = 16384;
	constexpr double TARGET_RATE = 25000.0;
	mitiru_mml::ChipDevice& device = driver.device();
	const std::uint32_t native = device.nativeRate();
	const std::uint32_t factor = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(native / TARGET_RATE));
	const double rate = static_cast<double>(native) / static_cast<double>(factor);
	const auto skip = static_cast<std::size_t>(rate * skipSeconds);

	std::vector<double> mono;
	mono.reserve(skip + POINTS);
	constexpr std::uint32_t CHUNK = 512; // 出力の点で 512 個ぶん
	std::vector<float> block(static_cast<std::size_t>(CHUNK) * factor * 2);
	while (mono.size() < skip + POINTS)
	{
		device.generate(block.data(), CHUNK * factor);
		for (std::uint32_t i = 0; i < CHUNK; ++i)
		{
			double acc = 0.0;
			for (std::uint32_t k = 0; k < factor; ++k)
			{
				const std::size_t at = (static_cast<std::size_t>(i) * factor + k) * 2;
				acc += 0.5 * (static_cast<double>(block[at]) + static_cast<double>(block[at + 1]));
			}
			mono.push_back(acc / factor);
		}
	}
	std::vector<double> x(mono.begin() + static_cast<std::ptrdiff_t>(skip),
		mono.begin() + static_cast<std::ptrdiff_t>(skip + POINTS));
	return analyzeMono(std::move(x), rate);
}

/// @brief チャンネルを初期化して key を鳴らし、測る
/// @param voice 音色番号。負なら setVoice を呼ばない
[[nodiscard]] inline DriverTone measureNote(mitiru_mml::ChipDriver& driver, int channel, int key,
	int voice, int volume = 15)
{
	driver.reset();
	if (voice >= 0)
	{
		driver.setVoice(channel, voice);
	}
	driver.setVolume(channel, volume);
	driver.noteOn(channel, key, volume);
	return measureSounding(driver);
}

} // namespace mitiru_mml_test
