#pragma once
/// @file LayerGains.hpp
/// @brief ゲームの状態に応じて層をミュートするゲイン操作と、小節位置の表
/// @details 層は鳴らしたまま出口のゲインだけを変えるため、ミュート中も曲の位置が進み、戻したときにほかの層とずれない。
/// ゲインは出力フレーム番号だけで決まり、実時間と乱数は使わない。
/// 同じ命令列と曲からは、命令が処理済みフレームより前を指さない限り、処理単位によらず同じ PCM を出力する。
/// MultiChipStream と ModernSongStream も参照。

#include <mitiru_mml/Timeline.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mitiru_mml
{

enum class FadeCurve : std::uint8_t
{
	Linear,      ///< 振幅が直線的に変わる
	EqualPower,  ///< 上がるときは sin、下がるときは cos。入れる層と外す層を同時に動かすと、二乗の和が一定になる
};

/// @brief 1 層のゲインをフレーム順に返す
/// @details schedule() は、startFrame から rampFrames かけて target へ変化させる。
/// 前の変化の途中なら、その時点の値から続ける。
/// 同じ startFrame の命令は追加順に処理する。
class LayerGainTrack
{
public:
	/// @param startFrame 変化を始める出力フレーム番号。処理済みなら次のフレームから始める
	/// @param target 変化後のゲイン。1 は元の音量、0 は無音
	/// @param rampFrames 変化にかけるフレーム数。0 なら startFrame で切り替える
	/// @throws std::invalid_argument target が 0 から 1 の範囲外か NaN のとき。負値は位相を反転し、NaN は以降の出力へ伝わる
	void schedule(std::uint64_t startFrame, float target, std::uint32_t rampFrames, FadeCurve curve = FadeCurve::EqualPower)
	{
		if (!(target >= 0.0f && target <= 1.0f))
		{
			throw std::invalid_argument("layer gain target must be within 0 to 1, got " + std::to_string(target));
		}
		Segment seg;
		seg.start = std::max(startFrame, m_frame);
		seg.target = static_cast<double>(target);
		seg.ramp = rampFrames;
		seg.curve = curve;
		const auto it = std::upper_bound(m_pending.begin() + static_cast<std::ptrdiff_t>(m_next), m_pending.end(), seg,
			[](const Segment& a, const Segment& b) { return a.start < b.start; });
		m_pending.insert(it, seg);
	}

	[[nodiscard]] float next()
	{
		while (m_next < m_pending.size() && m_pending[m_next].start <= m_frame)
		{
			m_active = m_pending[m_next];
			m_from = m_value;
			m_running = true;
			++m_next;
		}
		if (m_running)
		{
			const std::uint64_t elapsed = m_frame - m_active.start + 1;
			if (m_active.ramp == 0 || elapsed >= m_active.ramp)
			{
				m_value = m_active.target;
				m_running = false;
			}
			else
			{
				m_value = m_from + (m_active.target - m_from) * shape(static_cast<double>(elapsed) / static_cast<double>(m_active.ramp));
			}
		}
		if (m_next > 64 && m_next == m_pending.size())
		{
			m_pending.clear();
			m_next = 0;
		}
		++m_frame;
		return static_cast<float>(m_value);
	}

	/// @brief 次に next() が返すフレーム番号。処理済みのフレーム数でもある
	[[nodiscard]] std::uint64_t frame() const noexcept
	{
		return m_frame;
	}

	/// @brief 変化中か、開始前の命令があるか
	[[nodiscard]] bool busy() const noexcept
	{
		return m_running || m_next < m_pending.size();
	}

	/// @brief 直前に返したゲイン。まだ返していなければ 1
	[[nodiscard]] float value() const noexcept
	{
		return static_cast<float>(m_value);
	}

private:
	struct Segment
	{
		std::uint64_t start = 0;
		double target = 1.0;
		std::uint32_t ramp = 0;
		FadeCurve curve = FadeCurve::EqualPower;
	};

	[[nodiscard]] double shape(double t) const
	{
		if (m_active.curve == FadeCurve::Linear)
		{
			return t;
		}
		const double halfPi = 1.57079632679489661923;
		return (m_active.target >= m_from) ? std::sin(t * halfPi) : 1.0 - std::cos(t * halfPi);
	}

	std::vector<Segment> m_pending;
	std::size_t m_next = 0;
	Segment m_active;
	bool m_running = false;
	double m_from = 1.0;
	double m_value = 1.0;
	std::uint64_t m_frame = 0;
};

/// @brief 小節や拍の先頭に対応する出力フレーム番号の表
/// @details 出力レートに合わせて、テンポの変化とループを 1 度たどって作る。
/// 境界は、曲中の tick 番号が ticksPerStep の倍数になる位置。
/// ループの 2 周目は LoopPoint の tick から数える。
/// チップのネイティブレートで進む時計とは、整数の丸めにより 1 フレームほどずれることがある。
class TickGrid
{
public:
	static constexpr std::int64_t TICKS_PER_BEAT = TickClock::TICKS_PER_QUARTER;
	static constexpr std::int64_t TICKS_PER_BAR_4_4 = TICKS_PER_BEAT * 4;

	TickGrid() = default;

	/// @param events Tempo と LoopPoint を含む、並べ替え済みの全イベント
	TickGrid(const std::vector<SeqEvent>& events, int loopCount, std::uint32_t outputRate, int initialBpm)
	{
		TickClock clock(outputRate, TickClock::Rounding::CarryRemainder, initialBpm);
		TimelineCursor cursor(events, loopCount);
		while (!cursor.finished())
		{
			m_frames.push_back(clock.position());
			m_ticks.push_back(cursor.tick());
			static_cast<void>(cursor.step(clock, [](const SeqEvent&) {}));
		}
		m_endFrame = clock.position();
	}

	/// @brief atOrAfterFrame 以降で、tick が ticksPerStep の倍数になる最初のフレーム番号
	/// @details 曲の最後までに境界がなければ atOrAfterFrame を返す。
	[[nodiscard]] std::uint64_t nextBoundary(std::uint64_t atOrAfterFrame, std::int64_t ticksPerStep = TICKS_PER_BAR_4_4) const
	{
		const std::int64_t step = std::max<std::int64_t>(1, ticksPerStep);
		auto it = std::lower_bound(m_frames.begin(), m_frames.end(), atOrAfterFrame);
		for (std::size_t i = static_cast<std::size_t>(it - m_frames.begin()); i < m_frames.size(); ++i)
		{
			if (m_ticks[i] % step == 0)
			{
				return m_frames[i];
			}
		}
		return atOrAfterFrame;
	}

	/// @brief 最後の tick が終わるフレーム番号。チップの尾は含まない
	[[nodiscard]] std::uint64_t endFrame() const noexcept
	{
		return m_endFrame;
	}

private:
	std::vector<std::uint64_t> m_frames;
	std::vector<std::int64_t> m_ticks;
	std::uint64_t m_endFrame = 0;
};

/// @brief 層ごとのゲイン操作と小節位置の表。ストリームが持つ
class LayerControl
{
public:
	LayerControl() = default;

	LayerControl(std::size_t layerCount, TickGrid grid)
		: m_tracks(layerCount)
		, m_grid(std::move(grid))
	{
	}

	[[nodiscard]] std::size_t layerCount() const noexcept
	{
		return m_tracks.size();
	}

	/// @throws std::out_of_range layer が範囲外のとき
	void schedule(std::size_t layer, std::uint64_t startFrame, float target, std::uint32_t rampFrames, FadeCurve curve)
	{
		check(layer).schedule(startFrame, target, rampFrames, curve);
	}

	/// @brief atOrAfterFrame 以降の最初の境界から層のゲインを変える。既定の境界は 4/4 拍子の小節の先頭
	/// @return 変化を始めるフレーム番号
	std::uint64_t scheduleAtBoundary(std::size_t layer, std::uint64_t atOrAfterFrame, float target,
		std::uint32_t rampFrames, FadeCurve curve, std::int64_t ticksPerStep)
	{
		const std::uint64_t start = m_grid.nextBoundary(atOrAfterFrame, ticksPerStep);
		check(layer).schedule(start, target, rampFrames, curve);
		return start;
	}

	[[nodiscard]] std::uint64_t nextBoundary(std::uint64_t atOrAfterFrame, std::int64_t ticksPerStep) const
	{
		return m_grid.nextBoundary(atOrAfterFrame, ticksPerStep);
	}

	[[nodiscard]] const TickGrid& grid() const noexcept
	{
		return m_grid;
	}

	[[nodiscard]] std::vector<LayerGainTrack>& tracks() noexcept
	{
		return m_tracks;
	}

	/// @brief ゲインを処理したフレーム数。この番号より前の命令は遅れたものとして扱う
	[[nodiscard]] std::uint64_t processedFrames() const noexcept
	{
		return m_tracks.empty() ? 0 : m_tracks.front().frame();
	}

private:
	LayerGainTrack& check(std::size_t layer)
	{
		if (layer >= m_tracks.size())
		{
			throw std::out_of_range("layer " + std::to_string(layer) + " does not exist (layer count = "
				+ std::to_string(m_tracks.size()) + ")");
		}
		return m_tracks[layer];
	}

	std::vector<LayerGainTrack> m_tracks;
	TickGrid m_grid;
};

} // namespace mitiru_mml
