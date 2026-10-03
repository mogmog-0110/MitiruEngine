#pragma once

/// @file AuthorityWire.hpp
/// @brief host 権威の回線の決まり: 参加者の入力の packet と、状態の packet を切り分けて送り、受けて組み直す
///
/// 状態の packet は数十 KB になることがあるので、1 つを kFragmentBytes ずつの欠片にして送る。欠片が 1 つでも
/// 落ちたらその回の状態は捨てる (参加者は届いた中で一番新しい状態の番号を返し、host は次をそれとの差分にする)。
/// 入力の packet は毎フレーム、直近 kInputRedundancy フレーム分を重ねて送るので、続けて 3 個落ちても欠けない。

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <mitiru/network/DatagramEndpoint.hpp>
#include <mitiru/network/RollbackInput.hpp>
#include <mitiru/network/WireBytes.hpp>

namespace mitiru::network::authority
{

using rollback::PadInput;

inline constexpr std::uint32_t kNoFrame = 0xFFFFFFFFu;
/// 1 つの UDP packet に載せる状態の欠片。IP と UDP の頭を足しても、普通の回線の MTU (1500) を超えない
inline constexpr std::size_t kFragmentBytes = 1180;
inline constexpr int kMaxFragments = 4096;
inline constexpr int kInputRedundancy = 4;
inline constexpr std::size_t kPadInputWireBytes = 24;

enum MessageType : std::uint8_t
{
	kMsgInput = 1,     ///< 参加者 → host
	kMsgSnapshot = 2,  ///< host → 参加者 (状態の欠片)
	kMsgInputs = 3,    ///< host → 参加者 (状態の間のフレームに使った全員の入力)
};

inline void putPadInput(std::vector<std::uint8_t>& out, const PadInput& in)
{
	putLe(out, in.buttons, 4);
	putLe(out, in.padButtons, 4);
	for (const std::int8_t a : in.padAxes) putLe(out, static_cast<std::uint8_t>(a), 1);
	putLe(out, in.padConnected, 1);
	putLe(out, 0, 1);
	putLe(out, in.actions, 8);
}

[[nodiscard]] inline PadInput getPadInput(WireReader& r) noexcept
{
	PadInput in;
	in.buttons = static_cast<std::uint32_t>(r.get(4));
	in.padButtons = static_cast<std::uint32_t>(r.get(4));
	for (std::int8_t& a : in.padAxes) a = static_cast<std::int8_t>(static_cast<std::uint8_t>(r.get(1)));
	in.padConnected = static_cast<std::uint8_t>(r.get(1));
	(void)r.get(1);
	in.actions = r.get(8);
	return in;
}

/// @brief 詰めた状態の packet を欠片に分けて送る。@return 送った byte 数 (行き先の 1 byte を含む)
inline std::size_t sendFragments(DatagramEndpoint& net, NetAddress to, std::uint32_t frame, std::span<const std::uint8_t> packed,
	std::vector<std::uint8_t>& scratch)
{
	const std::size_t count = packed.empty() ? 1 : (packed.size() + kFragmentBytes - 1) / kFragmentBytes;
	if (count > static_cast<std::size_t>(kMaxFragments)) return 0;
	std::size_t sent = 0;
	for (std::size_t i = 0; i < count; ++i)
	{
		const std::size_t at = i * kFragmentBytes;
		const std::size_t len = (std::min)(kFragmentBytes, packed.size() - at);
		scratch.clear();
		putLe(scratch, kMsgSnapshot, 1);
		putLe(scratch, frame, 4);
		putLe(scratch, i, 2);
		putLe(scratch, count, 2);
		scratch.insert(scratch.end(), packed.data() + at, packed.data() + at + len);
		net.send(kChannelAuthority, to, scratch.data(), scratch.size());
		sent += scratch.size() + 1;
	}
	return sent;
}

/// @brief 欠片を組み直す。同時に組み立てるのは kSlots 回分までで、古いものから捨てる
class Reassembler
{
public:
	static constexpr int kSlots = 4;

	/// @param maxBytes 組み上がりの上限 (超える欠片の数を名乗る packet は捨てる)
	explicit Reassembler(std::size_t maxBytes = 0) noexcept : m_maxBytes(maxBytes) {}

	void setLimit(std::size_t maxBytes) noexcept { m_maxBytes = maxBytes; }

	/// @brief 欠片を 1 つ入れる。その回の全部が揃ったら out に組み上げて true
	bool add(std::uint32_t frame, int index, int count, std::span<const std::uint8_t> payload, std::vector<std::uint8_t>& out)
	{
		if (count <= 0 || count > kMaxFragments || index < 0 || index >= count || payload.size() > kFragmentBytes) return false;
		if (static_cast<std::size_t>(count - 1) * kFragmentBytes > m_maxBytes) return false;
		if (index + 1 < count && payload.size() != kFragmentBytes) return false;
		if (m_doneValid && static_cast<std::int32_t>(frame - m_done) <= 0) return false;   // 組み上げ済みより古い
		Slot& s = slotFor(frame, count);
		if (s.count != count || s.have[static_cast<std::size_t>(index)] != 0) return false;
		s.have[static_cast<std::size_t>(index)] = 1;
		std::memcpy(s.data.data() + static_cast<std::size_t>(index) * kFragmentBytes, payload.data(), payload.size());
		if (index + 1 == count) s.lastLen = payload.size();
		if (++s.received < count) return false;
		out.assign(s.data.begin(), s.data.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(count - 1) * kFragmentBytes + s.lastLen));
		m_done = frame;
		m_doneValid = true;
		s.used = false;
		return true;
	}

	/// @brief 組み立て途中で捨てた回の数 (新しい回に場所を譲ったもの)
	[[nodiscard]] int abandoned() const noexcept { return m_abandoned; }

private:
	struct Slot
	{
		bool used = false;
		std::uint32_t frame = 0;
		int count = 0;
		int received = 0;
		std::size_t lastLen = 0;
		std::uint64_t age = 0;
		std::vector<std::uint8_t> have;
		std::vector<std::uint8_t> data;
	};

	Slot& slotFor(std::uint32_t frame, int count)
	{
		Slot* oldest = &m_slots[0];
		for (Slot& s : m_slots)
		{
			if (s.used && s.frame == frame) return s;
			if (!s.used) oldest = &s;
			else if (oldest->used && s.age < oldest->age) oldest = &s;
		}
		if (oldest->used) ++m_abandoned;
		Slot& s = *oldest;
		s.used = true;
		s.frame = frame;
		s.count = count;
		s.received = 0;
		s.lastLen = 0;
		s.age = ++m_clock;
		s.have.assign(static_cast<std::size_t>(count), std::uint8_t{0});
		s.data.resize(static_cast<std::size_t>(count) * kFragmentBytes);
		return s;
	}

	std::array<Slot, kSlots> m_slots{};
	std::size_t m_maxBytes = 0;
	std::uint32_t m_done = 0;
	bool m_doneValid = false;
	std::uint64_t m_clock = 0;
	int m_abandoned = 0;
};

} // namespace mitiru::network::authority
