#pragma once

/// @file SpatialRenderer.hpp
/// @brief モノラル音源 1 本を、リスナーから見た向きに応じてステレオに配置する
/// @details 既定では等パワーのパン (PanSpatialRenderer) を使う。-DMITIRU_WITH_STEAMAUDIO=ON で
///          Steam Audio の SDK (tools/fetch_steamaudio.py) があれば、SpatialRendererFactory.hpp の
///          createSpatialRenderer は、HRTF で両耳に畳み込む SteamAudioHrtfRenderer を返す。向きは
///          SpatialAudio::toListenerSpace() で作る (+x 右、+y 上、-z 前)。

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <mitiru/audio/SpatialAudio.hpp>

namespace mitiru::audio
{

class ISpatialRenderer
{
public:
	virtual ~ISpatialRenderer() = default;

	/// @param mono frames 個のモノラル入力
	/// @param stereo frames * 2 個のインターリーブ出力 (L, R)
	/// @param direction リスナー空間の単位ベクトル
	/// @param gain 距離減衰などを掛け終えた音量
	/// @return 処理したかどうか。blockFrames() と frames が合わなければ何もしない
	virtual bool process(const float* mono, float* stereo, std::size_t frames,
	                     const AudioVec3& direction, float gain) = 0;

	/// @return 1 回の process で渡すフレーム数。0 なら任意
	[[nodiscard]] virtual std::size_t blockFrames() const noexcept = 0;

	[[nodiscard]] virtual const char* name() const noexcept = 0;
};

/// @brief 等パワーのパン。左右だけを見て、前後と上下は区別しない
class PanSpatialRenderer final : public ISpatialRenderer
{
public:
	bool process(const float* mono, float* stereo, std::size_t frames,
	             const AudioVec3& direction, float gain) override
	{
		// pan の -1..1 を 0..π/2 の角度に変換する。中央では両方が cos(π/4) になり、合計パワーは 1 のままになる
		const float pan = std::clamp(direction.x, -1.0f, 1.0f);
		const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
		const float left = std::cos(angle) * gain;
		const float right = std::sin(angle) * gain;
		for (std::size_t i = 0; i < frames; ++i)
		{
			stereo[i * 2] = mono[i] * left;
			stereo[i * 2 + 1] = mono[i] * right;
		}
		return true;
	}

	[[nodiscard]] std::size_t blockFrames() const noexcept override { return 0; }
	[[nodiscard]] const char* name() const noexcept override { return "pan"; }
};

} // namespace mitiru::audio
