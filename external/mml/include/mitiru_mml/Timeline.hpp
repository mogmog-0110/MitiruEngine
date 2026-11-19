#pragma once
/// @file Timeline.hpp
/// @brief チップに依存しないイベントと、整数だけで進む時間軸
/// @details 同じ tick のイベントの順序を (tick, トラック番号, トラック内の通し番号) で一意に決める。
///          tick からサンプル数への変換は 64bit の整数で行い、浮動小数点の丸めを使わない。

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mitiru_mml
{

/// @brief チップに依存しないイベントの種別
enum class SeqEventType : std::uint8_t
{
	NoteOn,    ///< channel、a = MIDI ノート番号、b = 音量 0〜15
	NoteOff,   ///< channel
	Pitch,     ///< channel、a = MIDI ノート番号 (発音を切らずに音程だけ変える)
	Volume,    ///< channel、a = 音量 0〜15
	Pan,       ///< channel、a = 0 左 / 1 中央 / 2 右
	Voice,     ///< channel、a = 音色番号
	Param,     ///< channel、a = パラメーター id、b と c = 値 (ChipDriver::setParam の value と value2)
	Tempo,     ///< a = BPM (全チップ共通)
	LoopPoint, ///< ループの戻り先 (全チップ共通)
};

/// @brief タイムライン上のイベント 1 件
struct SeqEvent
{
	std::int64_t tick = 0;       ///< 発生する tick (48 tick = 四分音符)
	std::uint32_t track = 0;     ///< 作られたトラックの番号
	std::uint32_t order = 0;     ///< トラック内の通し番号
	std::uint32_t chip = 0;      ///< 宛先のチップ番号
	SeqEventType type = SeqEventType::NoteOff;
	int channel = 0;
	int a = 0;
	int b = 0;
	int c = 0;
};

/// @brief 全順序: tick、トラック番号、通し番号の順に比べる
/// @details (track, order) が一意なので同順位が無く、どの標準ライブラリの sort でも並びが同じになる。
[[nodiscard]] inline bool seqEventBefore(const SeqEvent& lhs, const SeqEvent& rhs) noexcept
{
	if (lhs.tick != rhs.tick) return lhs.tick < rhs.tick;
	if (lhs.track != rhs.track) return lhs.track < rhs.track;
	return lhs.order < rhs.order;
}

/// @brief イベント列を全順序で並べる
inline void sortSeqEvents(std::vector<SeqEvent>& events)
{
	std::sort(events.begin(), events.end(), seqEventBefore);
}

/// @brief tick を進めるごとに「その tick が何サンプルか」を整数で返す時計
/// @details 1 tick = 60 / (BPM * 48) 秒。サンプル数 = レート * 60 / (BPM * 48)。
///          割り切れない端数の扱いは 2 通り。
///          TruncatePerTick は tick ごとに切り捨てる。従来の OpnaSequencer と同じ長さになる
///          (チップのレート 998400 Hz、BPM 20〜300 では従来の float 計算と同じ値)。
///          CarryRemainder は切り捨てた端数を次の tick に持ち越し、曲全体でずれない。
///          チップごとにレートが違う複数チップの曲は、こちらを使う。
class TickClock
{
public:
	/// @brief 四分音符あたりの tick 数
	static constexpr std::int64_t TICKS_PER_QUARTER = 48;

	/// @brief 端数の扱い
	enum class Rounding
	{
		TruncatePerTick,
		CarryRemainder,
	};

	/// @param rate サンプルレート (Hz)
	TickClock(std::uint32_t rate, Rounding rounding, int bpm = 120) noexcept
		: m_rate(rate)
		, m_rounding(rounding)
		, m_bpm(clampBpm(bpm))
	{
	}

	/// @brief テンポを変える。tick の境界で呼ぶこと
	void setTempo(int bpm) noexcept
	{
		const std::int64_t oldDen = denominator();
		m_bpm = clampBpm(bpm);
		const std::int64_t newDen = denominator();
		// 持ち越し中の端数は分数なので、新しい分母に合わせて保つ
		m_remainder = m_remainder * newDen / oldDen;
	}

	/// @brief 現在のテンポ
	[[nodiscard]] int tempo() const noexcept
	{
		return m_bpm;
	}

	/// @brief 次の 1 tick ぶんのサンプル数を返し、時計を進める
	[[nodiscard]] std::uint32_t advance() noexcept
	{
		const std::int64_t numerator = static_cast<std::int64_t>(m_rate) * 60;
		const std::int64_t den = denominator();
		std::int64_t total = numerator;
		if (m_rounding == Rounding::CarryRemainder)
		{
			total += m_remainder;
		}
		const std::int64_t samples = total / den;
		if (m_rounding == Rounding::CarryRemainder)
		{
			m_remainder = total % den;
		}
		m_position += static_cast<std::uint64_t>(samples);
		return static_cast<std::uint32_t>(samples);
	}

	/// @brief これまでに advance() が返したサンプル数の合計
	[[nodiscard]] std::uint64_t position() const noexcept
	{
		return m_position;
	}

private:
	[[nodiscard]] static int clampBpm(int bpm) noexcept
	{
		return bpm < 1 ? 1 : bpm;
	}

	[[nodiscard]] std::int64_t denominator() const noexcept
	{
		return static_cast<std::int64_t>(m_bpm) * TICKS_PER_QUARTER;
	}

	std::uint32_t m_rate;
	Rounding m_rounding;
	int m_bpm;
	std::int64_t m_remainder = 0;  ///< 持ち越し中の端数。分母は denominator()
	std::uint64_t m_position = 0;
};

/// @brief タイムラインを tick 順に再生する
/// @param events sortSeqEvents() で並べ済みのイベント
/// @param loopCount 曲全体を何回流すか (2 回目以降は最初の LoopPoint から)
/// @param clock サンプル数を決める時計。Tempo イベントでテンポを更新する
/// @param apply Tempo と LoopPoint 以外のイベントを受ける関数 (const SeqEvent&)
/// @param renderTick その tick のサンプル数を受けて音を作る関数 (std::uint32_t)
/// @details 最後のイベントの tick まで (その tick も含めて) 進める。従来の OpnaSequencer::render と
///          同じ区切り方。
template <class ApplyEvent, class RenderTick>
void playTimeline(
	const std::vector<SeqEvent>& events,
	int loopCount,
	TickClock& clock,
	ApplyEvent&& apply,
	RenderTick&& renderTick)
{
	std::int64_t totalTicks = 0;
	std::int64_t loopPointTick = 0;
	bool foundLoopPoint = false;
	for (const auto& ev : events)
	{
		totalTicks = std::max(totalTicks, ev.tick);
		if (!foundLoopPoint && ev.type == SeqEventType::LoopPoint)
		{
			loopPointTick = ev.tick;
			foundLoopPoint = true;
		}
	}

	const int passes = std::max(1, loopCount);
	for (int pass = 0; pass < passes; ++pass)
	{
		const std::int64_t startTick = (pass == 0) ? 0 : loopPointTick;
		std::size_t index = 0;
		while (index < events.size() && events[index].tick < startTick)
		{
			++index;
		}

		for (std::int64_t tick = startTick; tick <= totalTicks; ++tick)
		{
			while (index < events.size() && events[index].tick == tick)
			{
				const SeqEvent& ev = events[index];
				if (ev.type == SeqEventType::Tempo)
				{
					clock.setTempo(ev.a);
				}
				else if (ev.type != SeqEventType::LoopPoint)
				{
					apply(ev);
				}
				++index;
			}

			const std::uint32_t samples = clock.advance();
			if (samples > 0)
			{
				renderTick(samples);
			}
		}
	}
}

/// @brief playTimeline を 1 tick ずつ進められるようにした版
/// @details 小さな塊で PCM を出すストリーミングのために、tick の途中で止めて続きから再開できる。
///          1 回の step() が playTimeline の内側のループの 1 回分で、Tempo と LoopPoint の扱い、
///          最後のイベントの tick を含めること、ループの 2 周目以降が最初の LoopPoint から始まることも同じ。
///          events は cursor より長く生きていて、変わらないこと。
class TimelineCursor
{
public:
	TimelineCursor(const std::vector<SeqEvent>& events, int loopCount)
		: m_events(&events)
		, m_passes(std::max(1, loopCount))
	{
		bool foundLoopPoint = false;
		for (const auto& ev : events)
		{
			m_totalTicks = std::max(m_totalTicks, ev.tick);
			if (!foundLoopPoint && ev.type == SeqEventType::LoopPoint)
			{
				m_loopPointTick = ev.tick;
				foundLoopPoint = true;
			}
		}
	}

	/// @brief 最後の tick まで進めたか
	[[nodiscard]] bool finished() const noexcept
	{
		return m_pass >= m_passes;
	}

	/// @brief 次の step() が処理する tick。曲の先頭からの通し番号ではなく、曲の中の位置 (ループの 2 周目は LoopPoint から)
	[[nodiscard]] std::int64_t tick() const noexcept
	{
		return m_tick;
	}

	/// @brief 次の tick を処理し、その tick のサンプル数を返す (0 のこともある)
	/// @param apply Tempo と LoopPoint 以外のイベントを受ける関数 (const SeqEvent&)
	/// @pre finished() が false
	template <class ApplyEvent>
	[[nodiscard]] std::uint32_t step(TickClock& clock, ApplyEvent&& apply)
	{
		const std::vector<SeqEvent>& events = *m_events;
		while (m_index < events.size() && events[m_index].tick == m_tick)
		{
			const SeqEvent& ev = events[m_index];
			if (ev.type == SeqEventType::Tempo)
			{
				clock.setTempo(ev.a);
			}
			else if (ev.type != SeqEventType::LoopPoint)
			{
				apply(ev);
			}
			++m_index;
		}
		const std::uint32_t samples = clock.advance();

		++m_tick;
		if (m_tick > m_totalTicks)
		{
			++m_pass;
			m_tick = m_loopPointTick;
			m_index = 0;
			while (m_index < events.size() && events[m_index].tick < m_tick)
			{
				++m_index;
			}
		}
		return samples;
	}

private:
	const std::vector<SeqEvent>* m_events;
	int m_passes;
	std::int64_t m_totalTicks = 0;
	std::int64_t m_loopPointTick = 0;
	int m_pass = 0;
	std::int64_t m_tick = 0;
	std::size_t m_index = 0;
};

} // namespace mitiru_mml
