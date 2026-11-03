#pragma once

/// @file SpectrumTap.hpp
/// @brief 出力の直前の波形を少しだけ溜め、帯域ごとの大きさ (スペクトラム) を返す (ABI v63、ADR 0112)。
/// @details 音の thread が push で溜め、ゲームの thread が analyze で読む。1024 点の Hann ウィンドウと radix-2 の FFT で、
///          対数に並べた帯 (約 60 Hz から 16 kHz) の大きさを dBFS に直し、-60 dB を 0、0 dB を 1 に写す。
///          読む側は描画だけ (HostView::audioSpectrum)。音の出力は PC と機器で変わるので、GameMemory には入れない。
///          リングは 2 ウィンドウ分で、読んでいる間に書き手が追い越すと一部が新しい波形に差し替わるが、絵の揺れとして見える程度に収める。

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mitiru::audio
{

class SpectrumTap
{
public:
	static constexpr int kWindow = 1024;
	static constexpr int kRing = kWindow * 2;
	static constexpr float kLowHz = 60.0f;
	static constexpr float kHighHz = 16000.0f;
	static constexpr float kFloorDb = -60.0f;
	static constexpr float kHannEnbwBins = 1.5f;

	void setSampleRate(std::uint32_t rate) noexcept { m_sampleRate = rate; }
	[[nodiscard]] std::uint32_t sampleRate() const noexcept { return m_sampleRate; }

	/// 音の thread から。interleaved の frames 分を mono (チャンネルの平均) にしてリングへ足す
	void push(const float* interleaved, std::uint32_t frames, std::uint32_t channels) noexcept
	{
		if (interleaved == nullptr || frames == 0 || channels == 0) { return; }
		std::uint64_t w = m_written.load(std::memory_order_relaxed);
		const float inv = 1.0f / static_cast<float>(channels);
		for (std::uint32_t f = 0; f < frames; ++f)
		{
			float sum = 0.0f;
			for (std::uint32_t c = 0; c < channels; ++c) { sum += interleaved[f * channels + c]; }
			m_ring[w % kRing].store(sum * inv, std::memory_order_relaxed);
			++w;
		}
		m_written.store(w, std::memory_order_release);
	}

	/// ゲームの thread から。bands に bandCount 本 (0..1)、peak に最大の振幅 (0..1) を書く。まだ 1 ウィンドウ分も無ければ false
	bool analyze(float* bands, int bandCount, float* peak) noexcept
	{
		const std::uint64_t w = m_written.load(std::memory_order_acquire);
		if (bands == nullptr || bandCount <= 0 || w < static_cast<std::uint64_t>(kWindow) || m_sampleRate == 0) { return false; }
		const std::uint64_t start = w - static_cast<std::uint64_t>(kWindow);
		float peakAbs = 0.0f;
		float windowSum = 0.0f;
		for (int i = 0; i < kWindow; ++i)
		{
			const float s = m_ring[(start + static_cast<std::uint64_t>(i)) % kRing].load(std::memory_order_relaxed);
			const float a = std::fabs(s);
			if (a > peakAbs) { peakAbs = a; }
			const float hann = 0.5f - 0.5f * std::cos(6.283185307f * static_cast<float>(i) / static_cast<float>(kWindow));
			windowSum += hann;
			m_re[i] = s * hann;
			m_im[i] = 0.0f;
		}
		fft(m_re, m_im, kWindow);
		// 片側のスペクトラムの振幅 (正弦波の振幅 A が A になる正規化)。bin 0 と Nyquist は 1 倍
		const float scale = 2.0f / windowSum;
		for (int k = 0; k <= kWindow / 2; ++k)
		{
			const float mag = std::sqrt(m_re[k] * m_re[k] + m_im[k] * m_im[k]) * ((k == 0 || k == kWindow / 2) ? 0.5f * scale : scale);
			m_mag[k] = mag;
		}
		const float binHz = static_cast<float>(m_sampleRate) / static_cast<float>(kWindow);
		const float nyquistHz = 0.5f * static_cast<float>(m_sampleRate);
		const float ratio = kHighHz / kLowHz;
		for (int b = 0; b < bandCount; ++b)
		{
			const float lo = kLowHz * std::pow(ratio, static_cast<float>(b) / static_cast<float>(bandCount));
			const float hi = kLowHz * std::pow(ratio, static_cast<float>(b + 1) / static_cast<float>(bandCount));
			if (lo >= nyquistHz) { bands[b] = 0.0f; continue; }   // 標本化周波数が低い機器では、Nyquist を超える帯は無音
			int k0 = static_cast<int>(lo / binHz);
			int k1 = static_cast<int>(hi / binHz);
			if (k1 <= k0) { k1 = k0 + 1; }
			if (k0 > kWindow / 2) { k0 = kWindow / 2; }
			if (k1 > kWindow / 2 + 1) { k1 = kWindow / 2 + 1; }
			float power = 0.0f;
			int n = 0;
			for (int k = k0; k < k1; ++k) { power += m_mag[k] * m_mag[k]; ++n; }
			// 帯の電力を Hann ウィンドウの等価雑音帯域幅で割って、正弦波の振幅に直す。帯の幅 (bin の数) に依らず、同じ振幅の音が同じ高さになる
			bands[b] = toUnit(n > 0 ? std::sqrt(power / kHannEnbwBins) : 0.0f);
		}
		if (peak != nullptr) { *peak = peakAbs > 1.0f ? 1.0f : peakAbs; }
		return true;
	}

	/// 振幅 (0..1) を dBFS に直し、kFloorDb を 0、0 dB を 1 に写す
	[[nodiscard]] static float toUnit(float amplitude) noexcept
	{
		if (!(amplitude > 1.0e-6f)) { return 0.0f; }
		const float db = 20.0f * std::log10(amplitude);
		const float u = (db - kFloorDb) / (0.0f - kFloorDb);
		return u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
	}

	/// 帯域 b の下端の周波数 (Hz)。試験と文書が同じ式を使う
	[[nodiscard]] static float bandLowHz(int b, int bandCount) noexcept
	{
		return kLowHz * std::pow(kHighHz / kLowHz, static_cast<float>(b) / static_cast<float>(bandCount));
	}

private:
	/// その場で書き換える radix-2 の FFT (n は 2 の冪)
	static void fft(float* re, float* im, int n) noexcept
	{
		for (int i = 1, j = 0; i < n; ++i)
		{
			int bit = n >> 1;
			for (; (j & bit) != 0; bit >>= 1) { j ^= bit; }
			j ^= bit;
			if (i < j)
			{
				const float tr = re[i]; re[i] = re[j]; re[j] = tr;
				const float ti = im[i]; im[i] = im[j]; im[j] = ti;
			}
		}
		for (int len = 2; len <= n; len <<= 1)
		{
			const float ang = -6.283185307f / static_cast<float>(len);
			const float wr = std::cos(ang);
			const float wi = std::sin(ang);
			for (int i = 0; i < n; i += len)
			{
				float cr = 1.0f;
				float ci = 0.0f;
				for (int k = 0; k < len / 2; ++k)
				{
					const int a = i + k;
					const int b = a + len / 2;
					const float xr = re[b] * cr - im[b] * ci;
					const float xi = re[b] * ci + im[b] * cr;
					re[b] = re[a] - xr; im[b] = im[a] - xi;
					re[a] += xr;        im[a] += xi;
					const float nr = cr * wr - ci * wi;
					ci = cr * wi + ci * wr;
					cr = nr;
				}
			}
		}
	}

	// 書き手が読み手を追い越した分は新しい波形に混ざるが、float 1 つの読み書きは atomic にして競合を避ける
	std::atomic<float> m_ring[kRing]{};
	std::atomic<std::uint64_t> m_written{0};
	std::uint32_t m_sampleRate = 0;
	float m_re[kWindow]{};
	float m_im[kWindow]{};
	float m_mag[kWindow / 2 + 1]{};
};

} // namespace mitiru::audio
