#pragma once

/// @file MusicClock.hpp
/// @brief 区間の頭からのサンプル数を、ゲームへ渡す拍の位置 (小節・拍・拍の中の位置・秒) にする

#include <algorithm>
#include <cstdint>

#include <mitiru/audio/AudioEngine.hpp>
#include <mitiru/audio/music/MusicGrid.hpp>

namespace mitiru::audio::music
{

[[nodiscard]] inline MusicClockInfo musicClockOf(const MusicSegment& s, int segment, std::int64_t rel,
                                                 std::uint32_t rate) noexcept
{
	MusicClockInfo c;
	const MusicGrid g = MusicGrid::of(s, rate);
	rel = std::max<std::int64_t>(rel, 0);
	const std::int64_t beat = g.beatAt(rel);
	const std::int64_t beatStart = g.beatSample(beat);
	const std::int64_t beatLen = std::max<std::int64_t>(g.beatSample(beat + 1) - beatStart, 1);
	const int perBar = std::max(s.beatsPerBar, 1);
	c.segment = segment;
	c.bar = static_cast<int>(beat / perBar);
	c.beatInBar = static_cast<int>(beat % perBar);
	c.beatsPerBar = perBar;
	c.beatPhase = static_cast<float>(static_cast<double>(rel - beatStart) / static_cast<double>(beatLen));
	c.sec = (rate > 0) ? static_cast<double>(rel) / static_cast<double>(rate) : 0.0;
	return c;
}

}  // namespace mitiru::audio::music
