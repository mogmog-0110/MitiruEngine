#pragma once

/// @file ClipWatch.hpp
/// @brief 出力のミックスが 0 dBFS を超えたサンプルを数え、初めて超えたときに端末へ 1 回だけ知らせる。
/// @details 音は変えない。master に limiter を既定で掛けると、記録した WAV と音の確かめ (tools/audio_report.py の
///          ピーク・帯域・鳴り始め) の値が変わり、大きすぎる素材も隠れる。Godot も master の limiter は既定で入れず、
///          足すよう勧める形を取っている。数えるのは device の thread、知らせるのはメインスレッド。

#include <atomic>
#include <cmath>
#include <cstdint>

#include <mitiru/debug/WarnOnce.hpp>

namespace mitiru::audio
{

class ClipWatch
{
public:
	static constexpr const char* kWarnKey = "audio.clip";

	/// @brief ミックスの n サンプル (インターリーブ) のうち、絶対値が 1 を超えたものを足す。どの thread からでもよい
	void count(const float* samples, std::uint64_t n) noexcept
	{
		std::uint64_t over = 0;
		for (std::uint64_t i = 0; i < n; ++i) { over += std::fabs(samples[i]) > 1.0f ? 1u : 0u; }
		if (over > 0) { m_clipped.fetch_add(over, std::memory_order_relaxed); }
	}

	[[nodiscard]] std::uint64_t clippedSamples() const noexcept { return m_clipped.load(std::memory_order_relaxed); }

	/// @brief メインスレッドで毎フレーム呼ぶ。割れていれば最初の 1 回だけ知らせる
	void warnOnceIfClipped()
	{
		if (m_warned || clippedSamples() == 0) { return; }
		m_warned = true;
		debug::warnOnce(kWarnKey,
		                "音が割れている (同時に鳴っている音を足した大きさが上限を超えた)。重なる音の音量を "
		                "hud.play / hud.music の引数か assets/audio/sounds.json の volumeDb で下げる。");
	}

private:
	std::atomic<std::uint64_t> m_clipped{0};
	bool m_warned = false;
};

}  // namespace mitiru::audio
