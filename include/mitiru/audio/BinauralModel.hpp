#pragma once

/// @file BinauralModel.hpp
/// @brief ヘッドモデルから両耳の音を作る ISpatialRenderer（mix.json の binaural、ADR 0113）
/// @details 半径 8.75 cm の球として、Woodworth の式と Brown と Duda の極零フィルタを使う。
/// 両耳の時間差には Woodworth の式 ITD = a/c * (θ + sin θ) を使う。θ は正中面から横への角度で、真横では最大約 0.66 ms（48 kHz で約 31 サンプル）になる。遅れる耳だけを遅らせる。
/// 頭の影には Brown と Duda の 1 次極零フィルタ H(s) = (1 + α s/2ω0) / (1 + s/2ω0)、ω0 = c/a を使う。
/// α(θ耳) = 1.05 + 0.95 cos(1.2 θ耳) とする。θ耳 は声の向きと各耳の軸（±x）との角度で、耳の方向では高域が強まり（α = 2）、反対側では弱まる（α ≈ 0.28）。
/// ヘッドモデルだけでは前後を区別できないため、後ろの声には高域を弱める 1 次ローパスを加える。上下は区別しない。
/// 測定した HRTF を使う SteamAudioHrtfRenderer より精度は低い。スピーカーでは位置が曖昧になるため、既定では使わない。
/// 遅れには補間の 1 サンプルと、MiniaudioSpatialNode がためる 1 ブロック（256 サンプル、約 5 ms）が加わる。向きが変わる間は、ブロック内で値を直線補間する。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <mitiru/audio/SpatialRenderer.hpp>

namespace mitiru::audio
{

class BinauralModelRenderer final : public ISpatialRenderer
{
public:
	static constexpr double kHeadRadiusM = 0.0875;
	static constexpr double kSpeedOfSound = 343.0;
	static constexpr double kBackCutoffHz = 5000.0;   ///< 真後ろの声の low-pass

	explicit BinauralModelRenderer(int sampleRate)
		: m_rate(static_cast<double>(sampleRate))
		, m_line(static_cast<std::size_t>(std::ceil(m_rate * 0.0012)) + 4, 0.0f)
	{
		m_tau = kHeadRadiusM / (2.0 * kSpeedOfSound);
	}

	/// @brief 向きから決まる両耳の遅れ（サンプル）、影の係数 α、後ろの量
	struct Params
	{
		double delayL = 0.0;
		double delayR = 0.0;
		double alphaL = 1.0;
		double alphaR = 1.0;
		double back = 0.0;
	};

	[[nodiscard]] static Params paramsFor(const AudioVec3& direction, double sampleRate) noexcept
	{
		double x = direction.x, y = direction.y, z = direction.z;
		const double len = std::sqrt(x * x + y * y + z * z);
		if (len < 1e-6) { x = 0.0; y = 0.0; z = -1.0; } else { x /= len; y /= len; z /= len; }
		const double lateral = std::asin(std::clamp(x, -1.0, 1.0));
		const double itd = (kHeadRadiusM / kSpeedOfSound) * (std::fabs(lateral) + std::sin(std::fabs(lateral)));
		Params p;
		(x >= 0.0 ? p.delayL : p.delayR) = itd * sampleRate;   // 声と反対側の耳が遅れる
		auto shadow = [](double cosToEar) {
			const double theta = std::acos(std::clamp(cosToEar, -1.0, 1.0));
			return 1.05 + 0.95 * std::cos(1.2 * theta);
		};
		p.alphaR = shadow(x);
		p.alphaL = shadow(-x);
		p.back = std::clamp(z, 0.0, 1.0);
		return p;
	}

	bool process(const float* mono, float* stereo, std::size_t frames, const AudioVec3& direction, float gain) override
	{
		const Params next = paramsFor(direction, m_rate);
		if (!m_started) { m_prev = next; m_started = true; }
		const double k = 2.0 * m_rate;
		const double tk = m_tau * k;
		const double inv = 1.0 / static_cast<double>(frames);
		for (std::size_t i = 0; i < frames; ++i)
		{
			const double t = static_cast<double>(i + 1) * inv;
			push(mono[i]);
			const double dL = m_prev.delayL + (next.delayL - m_prev.delayL) * t;
			const double dR = m_prev.delayR + (next.delayR - m_prev.delayR) * t;
			const double aL = m_prev.alphaL + (next.alphaL - m_prev.alphaL) * t;
			const double aR = m_prev.alphaR + (next.alphaR - m_prev.alphaR) * t;
			const double back = m_prev.back + (next.back - m_prev.back) * t;
			const double lp = (back > 0.0) ? 1.0 - std::exp(-2.0 * 3.14159265358979323846 * backCutoff(back) / m_rate) : 1.0;
			stereo[i * 2] = static_cast<float>(ear(m_left, read(dL), aL, tk, lp) * gain);
			stereo[i * 2 + 1] = static_cast<float>(ear(m_right, read(dR), aR, tk, lp) * gain);
		}
		m_prev = next;
		return true;
	}

	[[nodiscard]] std::size_t blockFrames() const noexcept override { return 0; }
	[[nodiscard]] const char* name() const noexcept override { return "binaural-model"; }

private:
	struct EarState
	{
		double x1 = 0.0;
		double y1 = 0.0;
		double lp = 0.0;
	};

	static double backCutoff(double back) noexcept { return 20000.0 * std::pow(kBackCutoffHz / 20000.0, back); }

	void push(float v) noexcept
	{
		m_write = (m_write + 1) % m_line.size();
		m_line[m_write] = v;
	}

	/// 直前に書いた標本を遅れ 0 として、端数の遅れを 3 次のラグランジュ補間で読む。線形補間では、遅らせた耳だけ高域が弱くなる。
	/// 先の標本が必要なため、読みは 1 サンプル余計に遅れる。両耳に同じ遅れを加えるため、時間差は変わらない。
	[[nodiscard]] double read(double delay) const noexcept
	{
		const std::size_t n = m_line.size();
		const double d = std::clamp(delay + 1.0, 1.0, static_cast<double>(n - 3));
		const auto whole = static_cast<std::size_t>(d);
		const double t = d - static_cast<double>(whole);
		auto at = [&](std::size_t back) { return static_cast<double>(m_line[(m_write + n - back) % n]); };
		const double p0 = at(whole - 1), p1 = at(whole), p2 = at(whole + 1), p3 = at(whole + 2);
		return -t * (t - 1.0) * (t - 2.0) / 6.0 * p0 + (t + 1.0) * (t - 1.0) * (t - 2.0) / 2.0 * p1
		     - (t + 1.0) * t * (t - 2.0) / 2.0 * p2 + (t + 1.0) * t * (t - 1.0) / 6.0 * p3;
	}

	/// 頭の影を表す 1 次極零フィルタを双 1 次変換で通し、後ろのローパスを掛ける。
	static double ear(EarState& s, double x, double alpha, double tk, double lpCoeff) noexcept
	{
		const double inv = 1.0 / (1.0 + tk);
		const double b0 = (1.0 + alpha * tk) * inv;
		const double b1 = (1.0 - alpha * tk) * inv;
		const double a1 = (1.0 - tk) * inv;
		const double y = b0 * x + b1 * s.x1 - a1 * s.y1;
		s.x1 = x;
		s.y1 = y;
		s.lp += (y - s.lp) * lpCoeff;
		return s.lp;
	}

	double             m_rate;
	double             m_tau = 0.0;
	std::vector<float> m_line;
	std::size_t        m_write = 0;
	EarState           m_left;
	EarState           m_right;
	Params             m_prev;
	bool               m_started = false;
};

}  // namespace mitiru::audio
