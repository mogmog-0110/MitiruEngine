#pragma once
/// @file StereoBuffer.hpp
/// @brief float ステレオのバッファ (内部ミックス用)
/// @details 段ごとに int16 へ丸めると誤差がたまるので、エフェクトの手前までは float で持つ。
///          L と R を交互に並べる (interleaved)。±1.0 が 16bit の満点。

#include <mitiru_mml/MmlTypes.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief float ステレオ (interleaved L,R) のバッファ
struct StereoBuffer
{
	std::vector<float> samples;       ///< L,R,L,R,... の順。要素数は frames() * 2
	std::uint32_t sampleRate = 44100; ///< サンプルレート (Hz)

	/// @brief フレーム数 (L と R で 1 フレーム)
	[[nodiscard]] std::size_t frames() const noexcept
	{
		return samples.size() / 2;
	}

	/// @brief 空かどうか
	[[nodiscard]] bool empty() const noexcept
	{
		return samples.empty();
	}

	/// @brief 無音で frames フレームぶんに伸ばす (縮めはしない)
	void growTo(std::size_t frameCount)
	{
		if (samples.size() < frameCount * 2)
		{
			samples.resize(frameCount * 2, 0.0f);
		}
	}

	/// @brief other を gain 倍して加える。長さが足りなければ無音で伸ばす
	void addScaled(const StereoBuffer& other, float gain)
	{
		growTo(other.frames());
		for (std::size_t i = 0; i < other.samples.size(); ++i)
		{
			samples[i] += other.samples[i] * gain;
		}
	}

	/// @brief 16bit の interleaved ステレオへ変換する (±1 でクリップ、四捨五入)
	[[nodiscard]] PcmBuffer toPcm16Stereo() const
	{
		PcmBuffer out(samples.size());
		for (std::size_t i = 0; i < samples.size(); ++i)
		{
			out[i] = floatToPcm16(samples[i]);
		}
		return out;
	}

	/// @brief 16bit のモノラルへ畳む (L と R の平均)
	[[nodiscard]] PcmBuffer toPcm16Mono() const
	{
		PcmBuffer out(frames());
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			out[i] = floatToPcm16(0.5f * (samples[i * 2] + samples[i * 2 + 1]));
		}
		return out;
	}

	/// @brief float (±1.0 が満点) を 16bit に直す
	[[nodiscard]] static std::int16_t floatToPcm16(float v) noexcept
	{
		const float clamped = std::clamp(v, -1.0f, 1.0f);
		return static_cast<std::int16_t>(std::lrint(clamped * 32767.0f));
	}
};

} // namespace mitiru_mml
