#pragma once

/// @file AudioTransportClock.hpp
/// @brief デバイスが実際に出力したフレーム数を積算する連続再生クロック
/// @details ma_engine 自身のグローバル時刻 (ma_engine_get_time_in_pcm_frames) は
///          鳴っている音が無い区間で進まないことが実機計測で確認されている
///          (oscar-rythm: wall 6.3s 経過時にエンジン時刻は 0.38s しか進んでいなかった)。
///          本クロックは device data callback が要求したフレーム数だけを積算するため、
///          無音でも単調増加し、device が止まれば呼ばれなくなって自然に止まる。
///          real backend からもテストの dummy 呼び出しからも同じ addFrames() 経路で駆動できる。

#include <atomic>
#include <cstdint>

namespace mitiru::audio
{

/// @brief 出力フレーム数の積算だけを行うロックフリーなクロック
class AudioTransportClock
{
public:
	/// @brief 出力したフレーム数を積算する
	void addFrames(std::uint64_t frameCount) noexcept
	{
		m_frames.fetch_add(frameCount, std::memory_order_relaxed);
	}

	/// @brief 積算フレーム数を 0 に戻す
	void reset() noexcept { m_frames.store(0, std::memory_order_relaxed); }

	/// @brief 積算フレーム数を取得する
	[[nodiscard]] std::uint64_t frames() const noexcept
	{
		return m_frames.load(std::memory_order_relaxed);
	}

	/// @brief サンプルレートで割った経過秒を返す。sampleRate == 0 なら 0
	[[nodiscard]] double seconds(std::uint32_t sampleRate) const noexcept
	{
		if (sampleRate == 0) { return 0.0; }
		return static_cast<double>(frames()) / static_cast<double>(sampleRate);
	}

private:
	std::atomic<std::uint64_t> m_frames{0};
};

} // namespace mitiru::audio
