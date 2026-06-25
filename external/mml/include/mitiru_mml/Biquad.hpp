#pragma once
/// @file Biquad.hpp
/// @brief 双2次（biquad）フィルタ — レゾナンス付き LP/HP/BP やシェルフ/ピーキング EQ の基本素子。
/// @details RBJ Audio EQ Cookbook の係数。FM/PSG の硬い倍音をローパスで削って太く温かい音に
///          する（減算合成）／マスター EQ に使う。状態を持つ逐次処理なので、1 ノート/1 ストリームに
///          つき 1 インスタンスを直列に process() する。別ノートの頭では reset() でクリアする。
///
/// @code
/// mitiru_mml::Biquad lp;
/// lp.set(mitiru_mml::BiquadType::LowPass, 44100.0f, 2000.0f, 4.0f); // 2kHz, Q=4 共鳴
/// for (auto& s : samples) s = lp.process(s);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru_mml
{

/// @brief biquad フィルタ種別
enum class BiquadType : std::uint8_t
{
	LowPass,    ///< レゾナンス付きローパス
	HighPass,   ///< レゾナンス付きハイパス
	BandPass,   ///< バンドパス（一定 0dB ピーク）
	Notch,      ///< ノッチ（帯域除去）
	Peaking,    ///< ピーキング（gainDb で増減）
	LowShelf,   ///< ローシェルフ（gainDb）
	HighShelf,  ///< ハイシェルフ（gainDb）
};

/// @brief 双2次フィルタ（モノラル、Direct Form I の逐次処理）
class Biquad
{
public:
	Biquad() = default;

	/// @brief 係数を設計する。
	/// @param type フィルタ種別
	/// @param sampleRate サンプルレート(Hz)
	/// @param freq カットオフ / 中心周波数(Hz)
	/// @param q レゾナンス / Q。大きいほど鋭い（LP で 0.707=フラット、>1 で共鳴ピーク）
	/// @param gainDb Peaking / Shelf のゲイン(dB)。他種別では無視。
	void set(BiquadType type, float sampleRate, float freq, float q, float gainDb = 0.0f)
	{
		if (sampleRate <= 0.0f) { sampleRate = 44100.0f; }
		const float nyq = sampleRate * 0.5f;
		freq = std::clamp(freq, 1.0f, nyq - 1.0f);
		if (q < 0.0001f) { q = 0.0001f; }

		const float w0    = 2.0f * kPi * freq / sampleRate;
		const float cw    = std::cos(w0);
		const float sw    = std::sin(w0);
		const float alpha = sw / (2.0f * q);
		const float A     = std::pow(10.0f, gainDb / 40.0f);

		float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;
		switch (type)
		{
		case BiquadType::LowPass:
			b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw; b2 = (1.0f - cw) * 0.5f;
			a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
			break;
		case BiquadType::HighPass:
			b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = (1.0f + cw) * 0.5f;
			a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
			break;
		case BiquadType::BandPass:
			b0 = alpha; b1 = 0.0f; b2 = -alpha;
			a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
			break;
		case BiquadType::Notch:
			b0 = 1.0f; b1 = -2.0f * cw; b2 = 1.0f;
			a0 = 1.0f + alpha; a1 = -2.0f * cw; a2 = 1.0f - alpha;
			break;
		case BiquadType::Peaking:
			b0 = 1.0f + alpha * A; b1 = -2.0f * cw; b2 = 1.0f - alpha * A;
			a0 = 1.0f + alpha / A; a1 = -2.0f * cw; a2 = 1.0f - alpha / A;
			break;
		case BiquadType::LowShelf:
		{
			const float s = 2.0f * std::sqrt(A) * alpha;
			b0 = A * ((A + 1.0f) - (A - 1.0f) * cw + s);
			b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
			b2 = A * ((A + 1.0f) - (A - 1.0f) * cw - s);
			a0 = (A + 1.0f) + (A - 1.0f) * cw + s;
			a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
			a2 = (A + 1.0f) + (A - 1.0f) * cw - s;
			break;
		}
		case BiquadType::HighShelf:
		{
			const float s = 2.0f * std::sqrt(A) * alpha;
			b0 = A * ((A + 1.0f) + (A - 1.0f) * cw + s);
			b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
			b2 = A * ((A + 1.0f) + (A - 1.0f) * cw - s);
			a0 = (A + 1.0f) - (A - 1.0f) * cw + s;
			a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
			a2 = (A + 1.0f) - (A - 1.0f) * cw - s;
			break;
		}
		}

		const float inv = 1.0f / a0;
		m_b0 = b0 * inv; m_b1 = b1 * inv; m_b2 = b2 * inv;
		m_a1 = a1 * inv; m_a2 = a2 * inv;
	}

	/// @brief 1 サンプル処理する（直列に呼ぶ）。
	[[nodiscard]] float process(float x) noexcept
	{
		const float y = m_b0 * x + m_b1 * m_x1 + m_b2 * m_x2 - m_a1 * m_y1 - m_a2 * m_y2;
		m_x2 = m_x1; m_x1 = x;
		m_y2 = m_y1; m_y1 = y;
		return y;
	}

	/// @brief 内部状態をクリアする（別ノート/ストリームの先頭で呼ぶ）。
	void reset() noexcept { m_x1 = m_x2 = m_y1 = m_y2 = 0.0f; }

private:
	static constexpr float kPi = 3.14159265358979323846f;
	float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f, m_a1 = 0.0f, m_a2 = 0.0f;  ///< 正規化済み係数
	float m_x1 = 0.0f, m_x2 = 0.0f, m_y1 = 0.0f, m_y2 = 0.0f;                ///< 遅延状態
};

} // namespace mitiru_mml
