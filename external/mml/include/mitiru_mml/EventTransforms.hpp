#pragma once
/// @file EventTransforms.hpp
/// @brief イベント列に掛ける変換 (スウィング)
/// @details 変換はチップの Driver より手前、イベントの tick だけを動かすので、どのチップにも同じように効く。
///          tick は四分音符 48 なので、スウィングの細かさは 1 tick (120 BPM で約 10 ミリ秒) の単位になる。

#include <mitiru_mml/Timeline.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief スウィングで tick を動かす
/// @param grid 4 = 4 分音符、8 = 8 分音符、16 = 16 分音符。grid 2 つぶんを 1 周期として、裏拍を後ろへずらす
/// @param ratio 周期のうち、表拍が占める割合。0.5 でずらさない、0.667 で 3 連符の跳ね
[[nodiscard]] inline std::int64_t swingTick(std::int64_t tick, int grid, float ratio) noexcept
{
	if (grid != 4 && grid != 8 && grid != 16)
	{
		return tick;
	}
	const std::int64_t unit = (TickClock::TICKS_PER_QUARTER * 4) / grid; // grid 8 なら 24 tick
	const std::int64_t period = unit * 2;
	const std::int64_t clamped = static_cast<std::int64_t>(std::lround(
		static_cast<double>(std::clamp(ratio, 0.4f, 0.8f)) * static_cast<double>(period)));
	const std::int64_t base = tick - (tick % period);
	const std::int64_t pos = tick - base;
	std::int64_t mapped = 0;
	if (pos < unit)
	{
		mapped = (pos * clamped + unit / 2) / unit;
	}
	else
	{
		mapped = clamped + ((pos - unit) * (period - clamped) + unit / 2) / unit;
	}
	return base + mapped;
}

/// @brief 全イベントにスウィングを掛ける (Tempo と LoopPoint は動かさない)
/// @details 変換は単調なので、並びは変わらない。変換のあとで sortSeqEvents() を呼ぶこと。
inline void applySwing(std::vector<SeqEvent>& events, int grid, float ratio)
{
	if (std::fabs(ratio - 0.5f) < 1.0e-6f)
	{
		return;
	}
	for (auto& ev : events)
	{
		if (ev.type == SeqEventType::Tempo || ev.type == SeqEventType::LoopPoint)
		{
			continue;
		}
		ev.tick = swingTick(ev.tick, grid, ratio);
	}
}

/// @brief MML の文字列の [ ... ]n を、文字列のまま n 回に展開し、; から行末のコメントを捨てる
/// @details リズム MML の読み取りは [ ] を知らないので、読む前にこれで展開する。入れ子も展開する。n を省くと 2 回
[[nodiscard]] inline std::string expandTextLoops(std::string_view text)
{
	std::string out;
	std::vector<std::size_t> starts;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (c == ';')
		{
			while (i < text.size() && text[i] != '\n')
			{
				++i;
			}
			out += '\n';
		}
		else if (c == '[')
		{
			starts.push_back(out.size());
		}
		else if (c == ']' && !starts.empty())
		{
			int count = 0;
			bool hasCount = false;
			while (i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)
			{
				count = count * 10 + (text[i + 1] - '0');
				hasCount = true;
				++i;
			}
			if (!hasCount)
			{
				count = 2;
			}
			const std::string body = out.substr(starts.back());
			starts.pop_back();
			for (int r = 1; r < std::max(1, count); ++r)
			{
				out += body;
			}
		}
		else
		{
			out += c;
		}
	}
	return out;
}

} // namespace mitiru_mml
