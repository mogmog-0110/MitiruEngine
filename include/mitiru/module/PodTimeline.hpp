#pragma once

/// @file PodTimeline.hpp
/// @brief フレーム番号で刻む合図の列 (HE2 の Dv カットシーン / DiEventManager の最小形)。
///
/// `PodHsm` / `MsgQueue` と同じ位置づけの flat POD で、GameMemory に置く。再生位置 (`fired`) も
/// GameMemory なので、巻き戻せば再生位置も戻り、進め直せば同じフレームで同じ合図がもう一度出る
/// (演出の再生は決定論の一部)。`MsgQueue::pushDeferred` の待ちフレームは uint8 (255) までで
/// 数秒〜数十秒の演出に届かないため、こちらは uint32 の相対フレームで刻む。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru
{

/// @brief 固定容量 N の合図列。`Cue` は trivially_copyable で alignof ≤ 4 (padding を作らないため)。
template <class Cue, int N>
struct PodTimeline
{
	static_assert(std::is_trivially_copyable_v<Cue>, "PodTimeline<Cue,N>: Cue も flat POD であること");
	static_assert(alignof(Cue) <= 4, "PodTimeline<Cue,N>: Cue の alignment は 4 まで (8 だと末尾に padding が入り録画の bit-exact 比較を壊す)");
	static_assert(N > 0 && N < 65535, "PodTimeline<Cue,N>: N は 1..65534");

	Cue           cue[N]{};
	std::uint32_t atFrame[N]{};     ///< start() からの相対フレーム。昇順に保つ
	std::uint32_t startFrame = 0;   ///< start() を呼んだ時の currentFrame
	std::uint16_t count      = 0;
	std::uint16_t fired      = 0;   ///< 次に発火する添字 (count に達したら再生終了)
	std::uint8_t  playing    = 0;   ///< 0 = 停止中 (advance は何もしない)
	std::uint8_t  _pad[3]{};

	/// @brief 合図を積む。同じ相対フレームは積んだ順、それ以外は昇順に挿入する。満杯なら false。
	/// 再生中に発火済みの位置より前へ積んだ分は鳴らない (発火済みの合図を鳴らし直さないため。
	/// 積む側が経過フレーム以降の at を選ぶ)。
	[[nodiscard]] bool add(std::uint32_t at, const Cue& c) noexcept
	{
		if (count >= static_cast<std::uint16_t>(N)) { return false; }
		std::uint16_t pos = count;
		while (pos > 0 && atFrame[pos - 1] > at)
		{
			atFrame[pos] = atFrame[pos - 1];
			cue[pos]     = cue[pos - 1];
			--pos;
		}
		atFrame[pos] = at;
		cue[pos]     = c;
		++count;
		if (playing != 0 && pos < fired) { ++fired; }
		return true;
	}

	void start(std::uint32_t currentFrame) noexcept
	{
		startFrame = currentFrame;
		fired      = 0;
		playing    = 1;
	}

	void stop() noexcept { playing = 0; }

	[[nodiscard]] bool finished() const noexcept { return playing != 0 && fired >= count; }

	/// @brief 経過フレームに達した合図を昇順に `fn(const Cue&, std::uint32_t atFrame)` へ渡す。
	/// 1 フレームに複数の合図があれば全部渡す。経過フレームが発火済みの合図より小さければ
	/// (巻き戻し)、発火位置をそこまで戻して同じ合図をもう一度出す。
	template <class Fn>
	void advance(std::uint32_t currentFrame, Fn&& fn)
	{
		if (playing == 0 || currentFrame < startFrame) { return; }
		const std::uint32_t elapsed = currentFrame - startFrame;
		while (fired > 0 && atFrame[fired - 1] > elapsed) { --fired; }
		while (fired < count && atFrame[fired] <= elapsed)
		{
			fn(cue[fired], atFrame[fired]);
			++fired;
		}
	}
};

}  // namespace mitiru
