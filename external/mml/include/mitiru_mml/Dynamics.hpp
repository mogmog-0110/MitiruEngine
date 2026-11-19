#pragma once
/// @file Dynamics.hpp
/// @brief 現代的なミックスの仕上げ: サイドチェーンのコンプレッサー、イベント駆動のダッキング、リミッター、ステレオ幅、ディザ
/// @details すべて float ステレオ (StereoBuffer) を全体まとめて処理するオフライン用。先読みが要る処理を
///          因果的な遅延なしに作れる。既存の int16 モノラルの Compressor には触れず、サイドチェーン入力を
///          持つ float 版をここに新しく作った。乱数 (ディザ) は ModernRng だけを使う。

#include <mitiru_mml/ModernDsp.hpp>
#include <mitiru_mml/ModernRng.hpp>
#include <mitiru_mml/StereoBuffer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace mitiru_mml
{

[[nodiscard]] inline float dbToLinear(float db) noexcept
{
	return static_cast<float>(std::pow(10.0, static_cast<double>(db) / 20.0));
}

[[nodiscard]] inline float linearToDb(float linear) noexcept
{
	return linear > 1.0e-9f ? static_cast<float>(20.0 * std::log10(static_cast<double>(linear))) : -180.0f;
}

/// @brief リミッターの設定
struct LimiterSettings
{
	float ceilingDb = -1.0f;     ///< 出力のピークの上限
	float lookaheadMs = 3.0f;    ///< ゲインを下げ始めてから、ピークに届くまでの時間
	float releaseMs = 80.0f;     ///< ゲインが戻る時間 (90% 戻るまで)
};

/// @brief 先読みのピークリミッター。出力のピークは ceilingDb を超えない
/// @details L と R の大きい方に合わせて、両側に同じゲインをかける。ゲインは「先読みのウィンドウの最小値」を
///          取り、戻りを遅くしてからウィンドウの長さで平均するので、ピークの手前から直線的に下がり、
///          どのサンプルでも (入力 × ゲイン) が上限以下になる。
/// @return 最大のゲインリダクション (dB。0 以下)
inline float applyLimiter(StereoBuffer& buffer, const LimiterSettings& settings)
{
	const std::size_t frames = buffer.frames();
	if (frames == 0)
	{
		return 0.0f;
	}
	const double ceiling = static_cast<double>(dbToLinear(settings.ceilingDb));
	const double rate = static_cast<double>(buffer.sampleRate);
	const auto lookahead = static_cast<std::size_t>(std::max(1.0, std::round(static_cast<double>(settings.lookaheadMs) * 0.001 * rate)));

	std::vector<double> need(frames, 1.0);
	for (std::size_t i = 0; i < frames; ++i)
	{
		const double peak = std::max(std::fabs(static_cast<double>(buffer.samples[i * 2])),
			std::fabs(static_cast<double>(buffer.samples[i * 2 + 1])));
		if (peak > ceiling)
		{
			need[i] = ceiling / peak;
		}
	}

	// m[i] = min(need[i .. i + lookahead])
	std::vector<double> windowMin(frames, 1.0);
	std::deque<std::size_t> dq;
	for (std::size_t j = 0, i = 0; j < frames + lookahead; ++j)
	{
		if (j < frames)
		{
			while (!dq.empty() && need[dq.back()] >= need[j])
			{
				dq.pop_back();
			}
			dq.push_back(j);
		}
		// ウィンドウ [i, i + lookahead] がそろったら i を確定する
		while (i < frames && i + lookahead <= j)
		{
			while (!dq.empty() && dq.front() < i)
			{
				dq.pop_front();
			}
			windowMin[i] = dq.empty() ? 1.0 : need[dq.front()];
			++i;
		}
	}

	// 戻りを遅くする。下がるのは即座に、上がるのは指数的に
	const double relCoef = std::exp(-2.302585092994046 / (std::max(static_cast<double>(settings.releaseMs), 0.1) * 0.001 * rate));
	std::vector<double> held(frames, 1.0);
	double previous = 1.0;
	for (std::size_t i = 0; i < frames; ++i)
	{
		const double recovered = 1.0 - (1.0 - previous) * relCoef;
		previous = std::min(windowMin[i], recovered);
		held[i] = previous;
	}

	// ウィンドウの長さで平均して、角を丸める。ウィンドウは [i - lookahead, i] で、どの要素も need[i] 以下
	double sum = 0.0;
	const std::size_t window = lookahead + 1;
	for (std::size_t j = 0; j < window; ++j)
	{
		sum += held[0]; // i = 0 の前は held[0] で埋める
	}
	double worst = 1.0;
	for (std::size_t i = 0; i < frames; ++i)
	{
		if (i > 0)
		{
			const std::size_t leaving = (i > window) ? i - window : 0;
			sum += held[i] - held[leaving];
		}
		const double gain = sum / static_cast<double>(window);
		worst = std::min(worst, gain);
		buffer.samples[i * 2] = static_cast<float>(static_cast<double>(buffer.samples[i * 2]) * gain);
		buffer.samples[i * 2 + 1] = static_cast<float>(static_cast<double>(buffer.samples[i * 2 + 1]) * gain);
	}
	return linearToDb(static_cast<float>(worst));
}

/// @brief サイドチェーンのコンプレッサーの設定
struct SidechainSettings
{
	float thresholdDb = -30.0f;
	float ratio = 8.0f;
	float attackMs = 2.0f;
	float releaseMs = 180.0f;
	float makeupDb = 0.0f;
	float keyLowPassHz = 0.0f;   ///< 0 より大きいと、検出に使う信号を低域だけにする (キックだけに反応させる)
};

/// @brief key の音量に応じて target の音量を下げる (サイドチェーン)
/// @return 最大のゲインリダクション (dB。0 以下)
inline float applySidechainCompressor(StereoBuffer& target, const StereoBuffer& key, const SidechainSettings& s)
{
	const std::size_t frames = target.frames();
	const double rate = static_cast<double>(target.sampleRate);
	const double aCoef = std::exp(-1.0 / (std::max(static_cast<double>(s.attackMs), 0.01) * 0.001 * rate));
	const double rCoef = std::exp(-1.0 / (std::max(static_cast<double>(s.releaseMs), 0.01) * 0.001 * rate));
	const double slope = 1.0 - 1.0 / static_cast<double>(std::max(s.ratio, 1.0f));
	const float makeup = dbToLinear(s.makeupDb);

	modern_dsp::StateVariableFilter lowL;
	modern_dsp::StateVariableFilter lowR;
	if (s.keyLowPassHz > 0.0f)
	{
		lowL.setup(static_cast<double>(s.keyLowPassHz), 0.0f, rate);
		lowR.setup(static_cast<double>(s.keyLowPassHz), 0.0f, rate);
	}

	double envDb = -180.0;  // 検出のピーク (dB)
	double reduction = 0.0; // ゲインリダクション (dB。0 以上)
	double worst = 0.0;
	for (std::size_t i = 0; i < frames; ++i)
	{
		float kl = (i < key.frames()) ? key.samples[i * 2] : 0.0f;
		float kr = (i < key.frames()) ? key.samples[i * 2 + 1] : 0.0f;
		if (s.keyLowPassHz > 0.0f)
		{
			kl = lowL.process(kl, modern_dsp::StateVariableFilter::Type::LowPass);
			kr = lowR.process(kr, modern_dsp::StateVariableFilter::Type::LowPass);
		}
		const double level = static_cast<double>(std::max(std::fabs(kl), std::fabs(kr)));
		const double levelDb = (level > 1.0e-9) ? 20.0 * std::log10(level) : -180.0;
		// ピークの検出: 上がるときは速く、下がるときは release で
		const double c = (levelDb > envDb) ? aCoef : rCoef;
		envDb = c * envDb + (1.0 - c) * levelDb;
		const double want = std::max(0.0, (envDb - static_cast<double>(s.thresholdDb)) * slope);
		const double g = (want > reduction) ? aCoef : rCoef;
		reduction = g * reduction + (1.0 - g) * want;
		worst = std::max(worst, reduction);
		const float gain = static_cast<float>(std::pow(10.0, -reduction / 20.0)) * makeup;
		target.samples[i * 2] *= gain;
		target.samples[i * 2 + 1] *= gain;
	}
	return static_cast<float>(-worst);
}

/// @brief イベント駆動のダッキングの設定
struct DuckSettings
{
	float depthDb = -9.0f;    ///< 最も下がるときのゲイン
	float attackMs = 4.0f;    ///< 下がりきるまでの時間 (直線)
	float holdMs = 0.0f;      ///< 下がりきったまま保つ時間
	float releaseMs = 180.0f; ///< 元の音量の 90% (dB で見て depth の 10% まで) に戻るまでの時間
};

/// @brief 合図 (サンプル位置) から、ダッキングのゲインの曲線を作る
/// @details 検出が要らず、キックのタイミングは楽譜から決まるので、結果は曲の長さと合図だけで決まる。
[[nodiscard]] inline std::vector<float> buildDuckCurve(
	std::size_t frames, const std::vector<std::uint64_t>& triggers, std::uint32_t sampleRate, const DuckSettings& s)
{
	std::vector<float> curve(frames, 1.0f);
	const double rate = static_cast<double>(sampleRate);
	const double floorGain = static_cast<double>(dbToLinear(s.depthDb));
	const auto attack = static_cast<std::size_t>(std::max(1.0, std::round(static_cast<double>(s.attackMs) * 0.001 * rate)));
	const auto hold = static_cast<std::size_t>(std::max(0.0, std::round(static_cast<double>(s.holdMs) * 0.001 * rate)));
	// dB で見て 90% 戻る = depth の 10% に達するまで releaseMs。時定数は release / ln(10)
	const double tau = std::max(static_cast<double>(s.releaseMs), 0.1) * 0.001 * rate / 2.302585092994046;

	enum class Stage { Idle, Attack, Hold, Release };
	Stage stage = Stage::Idle;
	double gain = 1.0;
	double attackStep = 0.0;
	std::size_t holdLeft = 0;
	double releaseDb = 0.0; // リリースの途中の dB (負)
	std::size_t next = 0;
	for (std::size_t i = 0; i < frames; ++i)
	{
		while (next < triggers.size() && triggers[next] <= i)
		{
			if (triggers[next] == i)
			{
				stage = Stage::Attack;
				attackStep = (floorGain - gain) / static_cast<double>(attack);
				holdLeft = hold;
			}
			++next;
		}
		switch (stage)
		{
		case Stage::Attack:
			gain += attackStep;
			if (attackStep >= 0.0 || gain <= floorGain + 1.0e-9)
			{
				gain = floorGain;
				stage = (holdLeft > 0) ? Stage::Hold : Stage::Release;
				releaseDb = static_cast<double>(s.depthDb);
			}
			break;
		case Stage::Hold:
			if (holdLeft-- <= 1)
			{
				stage = Stage::Release;
				releaseDb = static_cast<double>(s.depthDb);
			}
			break;
		case Stage::Release:
			releaseDb *= std::exp(-1.0 / tau);
			gain = std::pow(10.0, releaseDb / 20.0);
			if (releaseDb > -0.01)
			{
				gain = 1.0;
				stage = Stage::Idle;
			}
			break;
		case Stage::Idle:
		default:
			gain = 1.0;
			break;
		}
		curve[i] = static_cast<float>(gain);
	}
	return curve;
}

/// @brief 曲線を掛ける
inline void applyGainCurve(StereoBuffer& buffer, const std::vector<float>& curve)
{
	const std::size_t frames = std::min(buffer.frames(), curve.size());
	for (std::size_t i = 0; i < frames; ++i)
	{
		buffer.samples[i * 2] *= curve[i];
		buffer.samples[i * 2 + 1] *= curve[i];
	}
}

/// @brief ステレオ幅。1 で変化なし、0 でモノラル、1 より大きいと広がる
inline void applyStereoWidth(StereoBuffer& buffer, float width)
{
	for (std::size_t i = 0; i < buffer.frames(); ++i)
	{
		const float l = buffer.samples[i * 2];
		const float r = buffer.samples[i * 2 + 1];
		const float mid = 0.5f * (l + r);
		const float side = 0.5f * (l - r) * width;
		buffer.samples[i * 2] = mid + side;
		buffer.samples[i * 2 + 1] = mid - side;
	}
}

/// @brief 16bit のステレオ PCM に直す。dither が true なら TPDF のディザを掛ける
/// @details ディザのシードは固定 (引数)。同じ入力から同じ PCM になる。
[[nodiscard]] inline PcmBuffer toPcm16StereoDithered(const StereoBuffer& buffer, bool dither, std::uint32_t seed = 0x2545F491u)
{
	if (!dither)
	{
		return buffer.toPcm16Stereo();
	}
	PcmBuffer out(buffer.samples.size());
	ModernRng rng(seed);
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		const float noise = 0.5f * (rng.nextSigned() + rng.nextSigned()); // 三角分布で ±1 LSB
		const float scaled = std::clamp(buffer.samples[i], -1.0f, 1.0f) * 32767.0f + noise;
		out[i] = static_cast<std::int16_t>(std::clamp(std::lrint(scaled), -32768L, 32767L));
	}
	return out;
}

} // namespace mitiru_mml
