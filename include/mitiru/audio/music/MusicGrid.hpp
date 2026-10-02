#pragma once

/// @file MusicGrid.hpp
/// @brief 区間の頭からのサンプル数と、拍、小節、マーカーの位置を相互に変換する
/// @details k 拍目の位置は round(k * 60 * rate / bpm) で求め、拍ごとの端数を足し込まない。120.5 BPM のように 1 拍が整数サンプルにならなくても、何小節進めても位置がずれない。

#include <cmath>
#include <cstdint>

#include <mitiru/audio/music/MusicManifest.hpp>

namespace mitiru::audio::music
{

struct MusicGrid
{
	double        bpm = 120.0;
	int           beatsPerBar = 4;
	std::uint32_t sampleRate = 48000;

	[[nodiscard]] static MusicGrid of(const MusicSegment& s, std::uint32_t rate) noexcept
	{
		return MusicGrid{s.bpm, s.beatsPerBar, rate};
	}

	[[nodiscard]] std::int64_t beatSample(std::int64_t beat) const noexcept
	{
		return std::llround(static_cast<double>(beat) * 60.0 * static_cast<double>(sampleRate) / bpm);
	}

	/// @brief rel 以降で最初の拍。rel が拍の頭ならその拍
	[[nodiscard]] std::int64_t beatAtOrAfter(std::int64_t rel) const noexcept
	{
		if (rel <= 0) { return 0; }
		const double beats = static_cast<double>(rel) * bpm / (60.0 * static_cast<double>(sampleRate));
		auto b = static_cast<std::int64_t>(std::ceil(beats));
		while (b > 0 && beatSample(b - 1) >= rel) { --b; }
		while (beatSample(b) < rel) { ++b; }
		return b;
	}

	/// @brief rel が含まれる拍。rel が拍の頭ならその拍
	[[nodiscard]] std::int64_t beatAt(std::int64_t rel) const noexcept
	{
		const std::int64_t b = beatAtOrAfter(rel);
		return (beatSample(b) == rel) ? b : b - 1;
	}

	[[nodiscard]] std::int64_t barAtOrAfter(std::int64_t rel) const noexcept
	{
		const std::int64_t b = beatAtOrAfter(rel);
		return (b + beatsPerBar - 1) / beatsPerBar;
	}
};

/// @brief 区間の長さをサンプル数で返す。終わりの位置は次の区間の頭
[[nodiscard]] inline std::int64_t segmentLength(const MusicSegment& s, std::uint32_t rate) noexcept
{
	return MusicGrid::of(s, rate).beatSample(static_cast<std::int64_t>(s.bars) * s.beatsPerBar);
}

/// @brief 区間の頭から rel の位置で sync を待ったときの切り替え位置。区間の終わりは越えない
[[nodiscard]] inline std::int64_t syncPoint(const MusicSegment& s, std::uint32_t rate, MusicSync sync,
	std::int64_t rel) noexcept
{
	const MusicGrid g = MusicGrid::of(s, rate);
	const std::int64_t end = segmentLength(s, rate);
	std::int64_t at = end;
	switch (sync)
	{
		case MusicSync::Immediate: at = rel; break;
		case MusicSync::Beat:      at = g.beatSample(g.beatAtOrAfter(rel)); break;
		case MusicSync::Bar:       at = g.beatSample(g.barAtOrAfter(rel) * g.beatsPerBar); break;
		case MusicSync::Marker:
			for (const int m : s.markerBeats)
			{
				if (g.beatSample(m) >= rel) { at = g.beatSample(m); break; }
			}
			break;
		case MusicSync::Exit:      break;
	}
	return (at < end) ? at : end;
}

}  // namespace mitiru::audio::music
