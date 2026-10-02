#pragma once

/// @file PodQueue.hpp
/// @brief POD 固定長メッセージキュー `MsgQueue<Msg,N>`。
/// `core::EventBus` (`publish` / `publishDeferred`) は型ごとの放送で宛先を持たない
/// (`hedgehog_study/mitiru_vs_he2.md` 1-4)。ここでは GameMemory の 1 フィールドとして持てる「宛先付き
/// メッセージ」の書き方を示す。中身は `FixedVec` と同じ固定長配列なので、巻き戻し互換
/// (flat POD) は保たれる。push した側と drain する側でフレームがずれてもおかしくならないよう、
/// drain が保証するのは、今溜まっている分を全部消費して空にするところまでに限る。
/// `pushDeferred` (Godot の `CONNECT_DEFERRED` 相当) は残り待ちフレーム数 (`deferWords`) を GameMemory 側に
/// 持たせるので、「次フレームへ持ち越し中」の状態がそのまま巻き戻し・録画の対象になる。
/// `CONNECT_ONE_SHOT` 相当は型を増やさず、drain 側が最初の 1 件で `break` する運用で書ける。

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace mitiru
{

namespace detail
{
template <class T, class = void>
struct MsgHasLayer : std::false_type {};
template <class T>
struct MsgHasLayer<T, std::void_t<decltype(std::declval<const T&>().layer)>> : std::true_type {};
}  // namespace detail

/// @brief 固定容量 N の POD メッセージキュー (Msg は trivially_copyable)。
template <typename Msg, std::size_t N>
struct MsgQueue
{
	static_assert(std::is_trivially_copyable_v<Msg>,
	              "MsgQueue<Msg,N>: Msg も flat POD (trivially_copyable) であること");

	Msg           items[N]{};
	// 要素 i の残り待ちフレーム数 (0 = 今フレーム drain 対象 / n>0 なら n フレーム待つ、CONNECT_DEFERRED)。
	// 1 byte ずつ並べると count の前に暗黙の padding が入り、その未初期化 byte のせいで録画の bit-exact 比較が
	// 合わなくなる。そのため 4 個ずつ 32bit 語に詰める (reflection の消費も N ではなく N/4 個で済む)。
	std::uint32_t deferWords[(N + 3) / 4]{};
	std::uint32_t count = 0;

	[[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
	[[nodiscard]] constexpr bool        empty() const noexcept { return count == 0; }
	[[nodiscard]] constexpr bool        full() const noexcept { return count >= N; }

	/// @brief 末尾に積む。容量を超えたときは何も知らせずに捨てることはせず、false を返す (戻り値を見て分岐させるため
	/// 戻り値に [[nodiscard]] を付けている)。
	[[nodiscard]] constexpr bool push(const Msg& m) noexcept
	{
		if (count >= N) { return false; }
		items[count] = m;
		setDeferFrames(count, 0);
		++count;
		return true;
	}

	/// @brief `frames` フレーム後まで drain 対象にしない (Godot `CONNECT_DEFERRED` 相当)。
	/// 0 を渡すと `push` と同じ (今フレームから drain 可)。
	[[nodiscard]] constexpr bool pushDeferred(const Msg& m, std::uint8_t frames = 1) noexcept
	{
		if (count >= N) { return false; }
		items[count] = m;
		setDeferFrames(count, frames);
		++count;
		return true;
	}

	/// @brief `deferFrames == 0` の分だけ宣言順に `fn(const Msg&)` へ渡して取り除く。
	/// まだ待ち時間が残るメッセージは配列に残したまま待ち時間を 1 減らし、次回の drain に回す
	/// (宣言順は保たれる)。全件が今フレーム対象なら、従来どおりキューは空になる。
	template <class Fn>
	void drain(Fn&& fn)
	{
		std::uint32_t writeIdx = 0;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const std::uint8_t remain = deferFrames(i);
			if (remain == 0)
			{
				fn(items[i]);
				continue;
			}
			if (writeIdx != i) { items[writeIdx] = items[i]; }
			setDeferFrames(writeIdx, static_cast<std::uint8_t>(remain - 1));
			++writeIdx;
		}
		count = writeIdx;
	}

	/// @brief `Msg::layer` (0..7) を持つ型だけの版。`layerMask` の bit i が立った layer 宛てだけ `fn` へ渡し、
	/// 残りは待ちフレームを減らさずに持ち越す (HE2 の GameObjectLayer::messageMask 相当)。こうすると、止めた layer
	/// (object ポーズ、`dtByLayer[i] == 0`) 宛ての当たり判定などが、再開した瞬間に古い分まで処理されることがない。
	/// 持ち越しが溜まると容量が減って `push` が false を返すので、止めている間は積む側も抑える。
	template <class Fn>
	void drain(Fn&& fn, std::uint8_t layerMask)
	{
		static_assert(detail::MsgHasLayer<Msg>::value,
		              "MsgQueue::drain(fn, layerMask): Msg に `std::uint8_t layer` が要る (無い型は mask 無しの drain を使う)");
		std::uint32_t writeIdx = 0;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const bool         wanted = ((layerMask >> (items[i].layer & 7u)) & 1u) != 0u;
			const std::uint8_t remain = deferFrames(i);
			if (wanted && remain == 0)
			{
				fn(items[i]);
				continue;
			}
			if (writeIdx != i) { items[writeIdx] = items[i]; }
			setDeferFrames(writeIdx, (wanted && remain > 0) ? static_cast<std::uint8_t>(remain - 1) : remain);
			++writeIdx;
		}
		count = writeIdx;
	}

	[[nodiscard]] constexpr std::uint8_t deferFrames(std::uint32_t i) const noexcept
	{
		return static_cast<std::uint8_t>(deferWords[i / 4] >> ((i % 4) * 8));
	}

	constexpr void setDeferFrames(std::uint32_t i, std::uint8_t frames) noexcept
	{
		const std::uint32_t shift = (i % 4) * 8;
		deferWords[i / 4] = (deferWords[i / 4] & ~(0xFFu << shift)) | (std::uint32_t{frames} << shift);
	}
};

}  // namespace mitiru
