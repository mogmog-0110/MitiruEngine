#pragma once

/// @file TimelineSpans.hpp
/// @brief ゲームが毎フレーム送るカットシーンの区間 (FrameIntents::timelineMarkers、ABI v50) を、Rewind 窓のバーに出す
///        「何フレーム前から何フレーム前まで」の帯にする。
/// @details 再生中のフレームで「長さ d の中の t の所」と分かれば、始まりは t 秒前、終わりは d - t 秒後になる。同じ名前でも
///          途中で途切れたら別の帯にする (同じカットシーンをもう一度見た時)。帯は host の中だけの表示で、録画には乗らない。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/BoundaryTypes.hpp>

namespace mitiru::observe
{

class TimelineSpans
{
public:
	static constexpr std::size_t kMaxSpans = 16;

	/// @brief 記録したフレーム (frame は 1 ずつ増える通し番号) に送られた区間を取り込む。区間の無いフレームも呼ぶ
	void record(std::int64_t frame, float dt, const module::TimelineMarker* markers, int count)
	{
		m_newest = frame;
		for (int i = 0; i < count && markers != nullptr; ++i) { take(frame, dt, markers[i]); }
	}

	/// @brief バーの帯。o0 / o1 は始まりと終わりが最新から何フレーム前か (終わりが先なら負)。capacity より古い帯は出さない
	[[nodiscard]] nlohmann::json toJson(std::int64_t capacity) const
	{
		nlohmann::json out = nlohmann::json::array();
		for (const Span& s : m_spans)
		{
			if (m_newest - s.end >= capacity) { continue; }
			out.push_back({{"label", s.name}, {"o0", m_newest - s.start}, {"o1", m_newest - s.end}});
		}
		return out;
	}

	void clear() noexcept
	{
		m_spans.clear();
		m_newest = 0;
	}

private:
	struct Span
	{
		std::string  name;
		std::int64_t start = 0;
		std::int64_t end = 0;
		std::int64_t lastSeen = 0;
	};

	void take(std::int64_t frame, float dt, const module::TimelineMarker& m)
	{
		std::size_t n = 0;
		while (n < sizeof(m.name) && m.name[n] != '\0') { ++n; }
		const std::string name(m.name, n);
		const float step = dt > 0.0f ? dt : 1.0f / 60.0f;
		const auto start = frame - static_cast<std::int64_t>(std::lround(std::max(m.timeSec, 0.0f) / step));
		const auto end = frame + static_cast<std::int64_t>(std::lround(std::max(m.durationSec - m.timeSec, 0.0f) / step));
		for (Span& s : m_spans)
		{
			// 同じフレームに同じ名前が 2 つ来たら別のカットシーンとして帯を分ける
			if (s.name != name || s.lastSeen + 1 != frame) { continue; }
			s.start = start;
			s.end = end;
			s.lastSeen = frame;
			return;
		}
		if (m_spans.size() >= kMaxSpans) { m_spans.erase(m_spans.begin()); }
		m_spans.push_back({name, start, end, frame});
	}

	std::vector<Span> m_spans;
	std::int64_t      m_newest = 0;
};

}  // namespace mitiru::observe
