#pragma once
/// @file PitchMeasure.hpp
/// @brief FM の音の高さを FFT で測る (OpnaDriver に 1 音を鳴らさせて、基音の周波数と音量と明るさを返す)

#include "PcmCompare.hpp"

#include <mitiru_mml/OpnaDriver.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace mitiru_mml_test
{

/// @brief 1 音の測定結果
struct ToneMeasure
{
	double peakHz = 0.0;      ///< スペクトルの最大の山の周波数 (放物線補間つき)
	double rmsDb = -300.0;    ///< 測定区間の RMS (dBFS)
	double centroidHz = 0.0;  ///< スペクトルの重心。大きいほど明るい音
};

/// @brief 440 Hz を基準にした、MIDI ノート番号の周波数
[[nodiscard]] inline double expectedHz(int key)
{
	return 440.0 * std::pow(2.0, (static_cast<double>(key) - 69.0) / 12.0);
}

/// @brief 期待値に対する測定値のずれ (セント)
[[nodiscard]] inline double centsOff(double measuredHz, double expected)
{
	return 1200.0 * std::log2(measuredHz / expected);
}

/// @brief 基音だけが鳴る FM 音色 (アルゴリズム 7 の OP1 だけが出力、持続音)
[[nodiscard]] inline mitiru_mml::OpnaFmVoice pureSineVoice()
{
	mitiru_mml::OpnaFmVoice voice;
	voice.algorithm = 7;
	voice.feedback = 0;
	for (int op = 0; op < 4; ++op)
	{
		auto& p = voice.ops[op];
		p.detune = 0;
		p.multiple = 1;
		p.totalLevel = (op == 0) ? 0 : 127;
		p.keyScale = 0;
		p.attackRate = 31;
		p.decayRate = 0;
		p.sustainRate = 0;
		p.sustainLevel = 0;
		p.releaseRate = 7;
	}
	return voice;
}

/// @brief FM チャンネル 0 で key を 0.1 秒ぶん鳴らしたあと、約 0.59 秒を測る
[[nodiscard]] inline ToneMeasure measureFmNote(const mitiru_mml::OpnaFmVoice& voice, int key)
{
	constexpr std::size_t POINTS = 32768;
	constexpr std::uint32_t HOLD = 18; // FM は 18 サンプルずつ同じ値で出てくる
	mitiru_mml::OpnaDriver driver;
	driver.setFmVoice(0, voice);
	driver.fmNoteOn(0, key);

	const std::uint32_t native = driver.sampleRate();
	const double rate = static_cast<double>(native) / HOLD;
	const std::size_t skip = static_cast<std::size_t>(rate * 0.1);

	std::vector<float> mono;
	std::vector<float> block(HOLD * 1024 * 2);
	while (mono.size() < skip + POINTS)
	{
		driver.device().generate(block.data(), HOLD * 1024);
		for (std::size_t i = 0; i < 1024; ++i)
		{
			double acc = 0.0;
			for (std::uint32_t k = 0; k < HOLD; ++k)
			{
				acc += static_cast<double>(block[(i * HOLD + k) * 2]);
			}
			mono.push_back(static_cast<float>(acc / HOLD));
		}
	}

	std::vector<std::complex<double>> spec(POINTS);
	double sumSq = 0.0;
	for (std::size_t i = 0; i < POINTS; ++i)
	{
		const double x = static_cast<double>(mono[skip + i]);
		sumSq += x * x;
		const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i)
			/ static_cast<double>(POINTS));
		spec[i] = {x * w, 0.0};
	}
	detail::fftInPlace(spec);

	std::vector<double> mag(POINTS / 2);
	for (std::size_t k = 0; k < mag.size(); ++k)
	{
		mag[k] = std::abs(spec[k]);
	}
	std::size_t peak = 3;
	for (std::size_t k = 3; k + 1 < mag.size(); ++k)
	{
		if (mag[k] > mag[peak]) peak = k;
	}
	const double a = std::log(mag[peak - 1] + 1.0e-30);
	const double b = std::log(mag[peak] + 1.0e-30);
	const double c = std::log(mag[peak + 1] + 1.0e-30);
	const double denom = a - 2.0 * b + c;
	const double delta = (denom != 0.0) ? 0.5 * (a - c) / denom : 0.0;

	double weighted = 0.0;
	double total = 0.0;
	for (std::size_t k = 1; k < mag.size(); ++k)
	{
		weighted += mag[k] * static_cast<double>(k);
		total += mag[k];
	}

	ToneMeasure m;
	m.peakHz = (static_cast<double>(peak) + delta) * rate / static_cast<double>(POINTS);
	const double rms = std::sqrt(sumSq / static_cast<double>(POINTS));
	m.rmsDb = (rms > 0.0) ? 20.0 * std::log10(rms) : -300.0;
	m.centroidHz = (total > 0.0) ? weighted / total * rate / static_cast<double>(POINTS) : 0.0;
	return m;
}

} // namespace mitiru_mml_test
