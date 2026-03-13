#pragma once

/// @file BgmManager.hpp
/// @brief GraphWalker手続き的BGM管理（データモデル）
///
/// ゾーンごとに異なるBGMパラメータを管理し、
/// sgc::audio::WaveGeneratorを用いて波形データを生成する。
///
/// @code
/// mitiru::gw::BgmManager bgm;
/// bgm.setZone(mitiru::gw::ZoneId::Tutorial);
/// bgm.update(dt);
/// auto samples = bgm.generateBgmSamples(1.0f, 44100);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "mitiru/graphwalker/Common.hpp"
#include "sgc/audio/WaveGenerator.hpp"

namespace mitiru::gw
{

/// @brief BGMパラメータ構造体
struct BgmParams
{
	float baseTempo = 120.0f;     ///< ベーステンポ（BPM）
	float bassFreq = 55.0f;       ///< ベース周波数（Hz）
	float melodyScale = 1.0f;     ///< メロディスケール倍率
	float harmonicRatio = 0.5f;   ///< ハーモニクス比率
	float lfoRate = 2.0f;         ///< LFO周波数（Hz）
	float lfoDepth = 0.3f;        ///< LFO深度（0〜1）
};

/// @brief 手続き的BGM管理クラス
///
/// ゾーンごとのBGMパラメータを保持し、ゾーン切替時にクロスフェードを行う。
/// 実際の波形データはgenerateBgmSamples()で生成する。
class BgmManager
{
public:
	/// @brief 現在のゾーンBGMを設定する
	/// @param zone ゾーンID
	void setZone(ZoneId zone)
	{
		if (zone == m_currentZone && !m_firstSet)
		{
			return;
		}

		m_previousParams = m_currentParams;
		m_currentParams = getZonePreset(zone);
		m_currentZone = zone;
		m_crossfadeTimer = m_crossfadeDuration;
		m_isCrossfading = true;
		m_firstSet = false;
	}

	/// @brief フレーム更新
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (m_isCrossfading)
		{
			m_crossfadeTimer = std::max(0.0f, m_crossfadeTimer - dt);
			if (m_crossfadeTimer <= 0.0f)
			{
				m_isCrossfading = false;
			}
		}
	}

	/// @brief 現在のBGMパラメータを取得する
	/// @return 現在のパラメータ（クロスフェード中は補間値）
	[[nodiscard]] BgmParams getBgmParams() const
	{
		if (!m_isCrossfading)
		{
			return m_currentParams;
		}

		// クロスフェード中は線形補間
		const float t = getCrossfadeProgress();
		BgmParams result;
		result.baseTempo = lerp(m_previousParams.baseTempo, m_currentParams.baseTempo, t);
		result.bassFreq = lerp(m_previousParams.bassFreq, m_currentParams.bassFreq, t);
		result.melodyScale = lerp(m_previousParams.melodyScale, m_currentParams.melodyScale, t);
		result.harmonicRatio = lerp(m_previousParams.harmonicRatio, m_currentParams.harmonicRatio, t);
		result.lfoRate = lerp(m_previousParams.lfoRate, m_currentParams.lfoRate, t);
		result.lfoDepth = lerp(m_previousParams.lfoDepth, m_currentParams.lfoDepth, t);
		return result;
	}

	/// @brief クロスフェードの進行度を取得する
	/// @return 進行度（0=開始, 1=完了）
	[[nodiscard]] float getCrossfadeProgress() const
	{
		if (m_crossfadeDuration <= 0.0f || !m_isCrossfading)
		{
			return 1.0f;
		}
		return 1.0f - (m_crossfadeTimer / m_crossfadeDuration);
	}

	/// @brief クロスフェード中か判定する
	/// @return クロスフェード中ならtrue
	[[nodiscard]] bool isCrossfading() const { return m_isCrossfading; }

	/// @brief 音量を設定する
	/// @param volume 音量（0〜1）
	void setVolume(float volume) { m_volume = std::clamp(volume, 0.0f, 1.0f); }

	/// @brief 音量を取得する
	/// @return 現在の音量
	[[nodiscard]] float getVolume() const { return m_volume; }

	/// @brief 現在のゾーンIDを取得する
	/// @return ゾーンID
	[[nodiscard]] ZoneId getCurrentZone() const { return m_currentZone; }

	/// @brief BGMサンプルデータを生成する
	/// @param duration 生成時間（秒）
	/// @param sampleRate サンプルレート（Hz）
	/// @return 波形サンプル列（-1〜1にクランプ済み）
	[[nodiscard]] std::vector<float> generateBgmSamples(float duration, uint32_t sampleRate = 44100) const
	{
		const auto params = getBgmParams();
		sgc::audio::WaveGenerator gen;

		// ベースライン（正弦波）
		auto bass = gen.generateSine(params.bassFreq, 0.4f * m_volume, duration, sampleRate);

		// メロディ（三角波）— ベース周波数のmelodyScale倍
		const float melodyFreq = params.bassFreq * params.melodyScale * 2.0f;
		auto melody = gen.generateTriangle(melodyFreq, 0.25f * m_volume, duration, sampleRate);

		// ハーモニクス（矩形波）— ベース周波数のharmonicRatio倍
		const float harmFreq = params.bassFreq * (1.0f + params.harmonicRatio);
		auto harmonics = gen.generateSquare(harmFreq, 0.15f * m_volume, duration, sampleRate);

		// ミックス
		auto mixed = sgc::audio::WaveGenerator::mix({bass, melody, harmonics});

		// LFO適用
		if (params.lfoDepth > 0.0f)
		{
			mixed = sgc::audio::WaveGenerator::applyLFO(mixed, params.lfoRate, params.lfoDepth, sampleRate);
		}

		// クランプ
		for (auto& s : mixed)
		{
			s = std::clamp(s, -1.0f, 1.0f);
		}

		return mixed;
	}

private:
	/// @brief 線形補間
	[[nodiscard]] static float lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	/// @brief ゾーンごとのBGMプリセットを取得する
	/// @param zone ゾーンID
	/// @return BGMパラメータ
	[[nodiscard]] static BgmParams getZonePreset(ZoneId zone)
	{
		switch (zone)
		{
		case ZoneId::Tutorial:
			// 穏やかなアンビエント（低テンポ）
			return { 80.0f, 55.0f, 1.0f, 0.3f, 1.0f, 0.2f };
		case ZoneId::Linear:
			// 安定したビート
			return { 110.0f, 65.0f, 1.5f, 0.5f, 2.0f, 0.3f };
		case ZoneId::AbsValue:
			// 対称的なリズム
			return { 100.0f, 73.0f, 1.2f, 0.4f, 1.5f, 0.25f };
		case ZoneId::Trig:
			// リズミカルなパルス
			return { 130.0f, 82.0f, 2.0f, 0.6f, 3.0f, 0.4f };
		case ZoneId::Parabola:
			// 上昇するメロディ
			return { 120.0f, 98.0f, 1.8f, 0.55f, 2.5f, 0.35f };
		case ZoneId::Exponential:
			// 加速するテンポ
			return { 140.0f, 110.0f, 2.5f, 0.7f, 4.0f, 0.5f };
		case ZoneId::Logarithm:
			// 減速する雰囲気
			return { 90.0f, 48.0f, 0.8f, 0.35f, 1.2f, 0.15f };
		case ZoneId::Chaos:
			// 不協和なレイヤー
			return { 150.0f, 41.0f, 3.0f, 0.9f, 5.0f, 0.6f };
		default:
			return {};
		}
	}

	BgmParams m_currentParams{};       ///< 現在のBGMパラメータ
	BgmParams m_previousParams{};      ///< 前回のBGMパラメータ（クロスフェード用）
	ZoneId m_currentZone = ZoneId::Tutorial; ///< 現在のゾーン
	float m_volume = 1.0f;             ///< 音量（0〜1）
	float m_crossfadeTimer = 0.0f;     ///< クロスフェード残り時間
	float m_crossfadeDuration = 1.0f;  ///< クロスフェード持続時間
	bool m_isCrossfading = false;      ///< クロスフェード中フラグ
	bool m_firstSet = true;            ///< 初回設定フラグ
};

} // namespace mitiru::gw
