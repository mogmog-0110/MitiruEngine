#pragma once

/// @file AuthorityHost.hpp
/// @brief host 権威の host の側: シミュレーションを 1 人で持ち、参加者の入力で進め、状態を決まった間隔で配る
///
/// 巻き戻さないので、on_update は 1 フレームに 1 回だけ呼ぶ (重い場面でも 1 人分の費用で済む)。参加者の入力は
/// 届いた順に 1 フレーム 1 つずつ使い、届いていなければ前の入力を続ける。溜まりすぎたら新しい方へ飛ばす。
/// 状態は snapshotEvery フレームごとに、参加者が最後に受け取ったと返した状態との差分で送る。同じ時に、直前の
/// 数回分の間に使った全員の入力を小さな別の packet で送る。参加者はそれで 2 つの状態の間のフレームを作る
/// (作り方は AuthorityClient.hpp)。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/SideStateHost.hpp>
#include <mitiru/network/AuthorityWire.hpp>
#include <mitiru/network/SnapshotCodec.hpp>

namespace mitiru::network::authority
{

using rollback::kMaxPlayers;
using rollback::Keymap;
using rollback::RollbackCalls;
using rollback::SnapshotBase;

struct AuthorityConfig
{
	int players = 2;
	int localPlayer = 0;                 ///< この PC の席 (host は 0)
	int snapshotEvery = 3;               ///< 何フレームごとに状態を送るか (60Hz で 3 = 20Hz)
	unsigned disconnectTimeoutMs = 5000;
	SnapshotBase snapshot{};
	std::array<Keymap, kMaxPlayers> keymaps{rollback::kKeymapPlayer1, rollback::kKeymapPlayer2, Keymap{}, Keymap{}};
	std::array<NetAddress, kMaxPlayers> addrs{};   ///< 席ごとの住所 (host は参加者の、参加者は host の住所を使う)
};

/// @brief 状態と一緒に送る入力のフレーム数。入力の packet が 2 回続けて落ちても、間のフレームを作れる
[[nodiscard]] inline int inputHistoryFrames(int snapshotEvery) noexcept
{
	return (std::min)(3 * snapshotEvery + 1, 63);
}

/// @brief 毎秒 hz 回配る時の、状態の間のフレーム数。60 の約数でなければ hz を下の約数へ丸める (0 以下は 1 回)
[[nodiscard]] inline int snapshotEveryForRate(int hz) noexcept
{
	int rate = std::clamp(hz, 1, 60);
	while (60 % rate != 0) --rate;
	return 60 / rate;
}

/// @brief 状態の image の上限 (GameMemory + 窓口)。窓口の分はロールバックの保存枠と同じく始めの 4 倍か 64 KB の大きい方
[[nodiscard]] inline std::size_t maxImageBytes(std::uint32_t memorySize, std::size_t sideImage) noexcept
{
	return memorySize + (std::max)(sideImage * 4, std::size_t{64} * 1024);
}

struct AuthorityHostStats
{
	int frame = 0;                       ///< 次に進めるフレーム (= 進めた回数)
	std::uint64_t bytesSent = 0;         ///< 状態と入力の packet の合計 (行き先の 1 byte を含む、IP と UDP の頭は含まない)
	std::uint64_t packetsSent = 0;
	std::uint64_t bytesReceived = 0;
	int snapshots = 0;                   ///< 参加者 1 人に 1 回送るごとに 1
	int fullSnapshots = 0;               ///< そのうち差分でなく丸ごと送った回
	int skippedInputs = 0;               ///< 溜まりすぎて飛ばした参加者の入力
	int heldInputs = 0;                  ///< 届いていなくて前の入力を続けたフレーム
	int disconnects = 0;
	std::uint32_t disconnectedMask = 0;
};

class AuthorityHost
{
public:
	AuthorityHost(const module::ModuleApi& api, void* memory, const AuthorityConfig& cfg, DatagramEndpoint& net,
		module::SideStateHost* sides, std::uint64_t nowMs)
		: m_update(api.on_update), m_memorySize(api.memorySize), m_memory(memory), m_cfg(cfg), m_net(&net),
		  m_sides(sides != nullptr && !sides->empty() ? sides : nullptr),
		  m_intents(std::make_unique<module::FrameIntents>()), m_snap(std::make_unique<module::InputSnapshot>())
	{
		m_intents->reset();
		if (api.on_update == nullptr || memory == nullptr || api.memorySize == 0) fail("GameMemory の大きさ (memorySize) を申告していない");
		else if (cfg.players < 2 || cfg.players > kMaxPlayers || cfg.snapshotEvery < 1 || cfg.snapshotEvery > 60) fail("人数か状態の間隔が範囲外");
		for (int p = 0; p < cfg.players; ++p) m_seats[static_cast<std::size_t>(p)].lastHeardMs = nowMs;
	}

	void setCalls(const RollbackCalls& calls) noexcept { m_calls = calls; }

	/// @brief 1 フレーム分。届いた入力を読み、決まったフレームなら状態を配り、全員の入力で 1 回進める
	void tick(std::uint64_t nowMs, const PadInput& local)
	{
		if (!m_error.empty()) return;
		m_now = nowMs;
		receive();
		checkTimeouts();
		if (m_stats.frame % m_cfg.snapshotEvery == 0) broadcast();
		if (!m_error.empty()) return;
		advance(local);
	}

	bool takeConfirmedIntents(module::FrameIntents& out)
	{
		if (!m_fresh) return false;
		m_fresh = false;
		std::memcpy(&out, m_intents.get(), sizeof(out));
		return true;
	}

	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] const AuthorityHostStats& stats() const noexcept { return m_stats; }
	[[nodiscard]] const AuthorityConfig& config() const noexcept { return m_cfg; }
	/// @brief 席 p の往復の時間 (自分と、まだ測っていない席は 0)
	[[nodiscard]] std::uint16_t pingMs(int p) const noexcept { return m_seats[static_cast<std::size_t>(p & 3)].rttMs; }
	/// @brief 最後に watch したフレームの状態の checksum (GameMemory + 窓口)。まだ無ければ 0
	[[nodiscard]] std::uint32_t watchedChecksum() const noexcept { return m_watchedSum; }
	[[nodiscard]] bool watched() const noexcept { return m_watched; }
	/// @brief frame を進める前の状態の checksum を取っておく (--net-frames の突き合わせ用)
	void watchFrame(int frame) noexcept { m_watchFrame = frame; }

private:
	struct Seat
	{
		bool present = true;
		std::uint64_t lastHeardMs = 0;
		std::array<PadInput, 64> ring{};
		std::array<std::uint32_t, 64> seqs{};
		std::array<bool, 64> filled{};
		bool any = false;
		std::uint32_t newest = 0;
		std::uint32_t next = 0;
		std::uint32_t first = 0;               ///< 最初に届いた入力の番号 (next がここなら、まだ 1 つも使っていない)
		PadInput last{};
		std::uint32_t ack = kNoFrame;          ///< 参加者が最後に組み上げた状態のフレーム
		std::uint32_t echoTime = 0;            ///< 参加者の時計 (返すと往復が測れる)
		std::uint64_t echoAt = 0;
		std::uint16_t rttMs = 0;
	};

	struct Sent
	{
		std::uint32_t frame = kNoFrame;
		std::vector<std::uint8_t> image;
	};

	static constexpr int kHistory = 16;
	static constexpr std::int32_t kMaxBacklog = 6;

	void fail(std::string why) { m_error = std::move(why); }

	[[nodiscard]] Seat& seat(int p) noexcept { return m_seats[static_cast<std::size_t>(p)]; }

	[[nodiscard]] int seatOf(NetAddress a) const noexcept
	{
		for (int p = 0; p < m_cfg.players; ++p)
		{
			if (p != m_cfg.localPlayer && m_cfg.addrs[static_cast<std::size_t>(p)] == a) return p;
		}
		return -1;
	}

	void receive()
	{
		auto& box = m_net->inbox(kChannelAuthority);
		for (const Datagram& d : box)
		{
			m_stats.bytesReceived += d.bytes.size() + 1;
			const int p = seatOf(d.from);
			if (p >= 0 && seat(p).present) readInput(seat(p), d.bytes);
		}
		box.clear();
	}

	void readInput(Seat& s, const std::vector<std::uint8_t>& bytes)
	{
		WireReader r(bytes);
		if (r.get(1) != kMsgInput) return;
		const auto newest = static_cast<std::uint32_t>(r.get(4));
		const auto ack = static_cast<std::uint32_t>(r.get(4));
		const auto clientTime = static_cast<std::uint32_t>(r.get(4));
		const auto echo = static_cast<std::uint32_t>(r.get(4));
		const auto hold = static_cast<std::uint32_t>(r.get(2));
		const int count = static_cast<int>(r.get(1));
		if (!r.ok || count < 1 || count > kInputRedundancy) return;
		s.lastHeardMs = m_now;
		if (ack != kNoFrame && (s.ack == kNoFrame || static_cast<std::int32_t>(ack - s.ack) > 0)) s.ack = ack;
		s.echoTime = clientTime;
		s.echoAt = m_now;
		const auto now32 = static_cast<std::uint32_t>(m_now);
		if (echo != 0 && now32 - echo >= hold) s.rttMs = static_cast<std::uint16_t>((std::min)(now32 - echo - hold, 65535u));
		for (int k = 0; k < count; ++k)
		{
			const PadInput in = getPadInput(r);
			if (r.ok) store(s, newest - static_cast<std::uint32_t>(k), in);
		}
	}

	static void store(Seat& s, std::uint32_t seq, const PadInput& in) noexcept
	{
		if (!s.any)
		{
			s.any = true;
			s.newest = seq;
			s.next = seq;
			s.first = seq;
		}
		if (static_cast<std::int32_t>(seq - s.next) < 0) return;   // もう使ったか飛ばした
		const std::size_t i = seq % 64;
		s.ring[i] = in;
		s.seqs[i] = seq;
		s.filled[i] = true;
		if (static_cast<std::int32_t>(seq - s.newest) > 0) s.newest = seq;
	}

	/// @brief 席の入力を 1 つ使う。無ければ前の入力を続け、溜まりすぎたら新しい方へ飛ばす
	PadInput consume(Seat& s) noexcept
	{
		if (!s.present) return PadInput{};
		if (!s.any) return s.last;
		const auto backlog = static_cast<std::int32_t>(s.newest - s.next);
		if (backlog > kMaxBacklog)
		{
			m_stats.skippedInputs += backlog - 1;
			s.next = s.newest - 1;
		}
		const std::size_t i = s.next % 64;
		if (s.filled[i] && s.seqs[i] == s.next)
		{
			s.last = s.ring[i];
			s.filled[i] = false;
			++s.next;
		}
		else ++m_stats.heldInputs;
		return s.last;
	}

	void checkTimeouts()
	{
		for (int p = 0; p < m_cfg.players; ++p)
		{
			Seat& s = seat(p);
			if (p == m_cfg.localPlayer || !s.present || m_now < s.lastHeardMs + m_cfg.disconnectTimeoutMs) continue;
			s.present = false;
			++m_stats.disconnects;
			m_stats.disconnectedMask |= 1u << p;
		}
	}

	[[nodiscard]] std::uint8_t presentMask() const noexcept
	{
		std::uint8_t mask = 0;
		for (int p = 0; p < m_cfg.players; ++p)
		{
			if (m_seats[static_cast<std::size_t>(p)].present) mask = static_cast<std::uint8_t>(mask | (1u << p));
		}
		return mask;
	}

	bool captureImage(std::vector<std::uint8_t>& image)
	{
		image.assign(static_cast<const std::uint8_t*>(m_memory), static_cast<const std::uint8_t*>(m_memory) + m_memorySize);
		if (m_sides == nullptr) return true;
		std::string why;
		if (!m_sides->capture(m_memory, true, m_side, &why))
		{
			fail("GameMemory の外に持つ状態を取れない: " + why);
			return false;
		}
		image.insert(image.end(), m_side.begin(), m_side.end());
		return true;
	}

	/// @brief 今の状態を履歴に置き、繋がっている参加者それぞれへ、その人が持つ状態との差分で送る
	void broadcast()
	{
		Sent& slot = m_history[static_cast<std::size_t>(m_stats.frame / m_cfg.snapshotEvery) % kHistory];
		if (!captureImage(slot.image)) return;
		slot.frame = static_cast<std::uint32_t>(m_stats.frame);
		buildInputs();
		for (int p = 0; p < m_cfg.players; ++p)
		{
			if (p == m_cfg.localPlayer || !seat(p).present) continue;
			// 入力は 1 つの小さな packet で先に送る。大きな状態の欠片が落ちても、参加者は入力で間のフレームを作り続けられる
			m_net->send(kChannelAuthority, m_cfg.addrs[static_cast<std::size_t>(p)], m_inputs.data(), m_inputs.size());
			m_stats.bytesSent += m_inputs.size() + 1;
			++m_stats.packetsSent;
			sendSnapshot(p, slot);
		}
	}

	[[nodiscard]] const Sent* history(std::uint32_t frame) const noexcept
	{
		for (const Sent& s : m_history)
		{
			if (s.frame == frame && frame != kNoFrame) return &s;
		}
		return nullptr;
	}

	void sendSnapshot(int p, const Sent& now)
	{
		Seat& s = seat(p);
		const Sent* base = s.ack != kNoFrame && s.ack != now.frame ? history(s.ack) : nullptr;
		m_body.clear();
		putLe(m_body, now.frame, 4);
		putLe(m_body, base != nullptr ? base->frame : kNoFrame, 4);
		putLe(m_body, static_cast<std::uint32_t>(m_now), 4);
		putLe(m_body, s.echoTime, 4);
		putLe(m_body, (std::min<std::uint64_t>)(m_now - s.echoAt, 65535), 2);
		putLe(m_body, static_cast<std::uint64_t>(m_cfg.players), 1);
		putLe(m_body, presentMask(), 1);
		m_codec.encodeImage(now.image, base != nullptr ? std::span<const std::uint8_t>(base->image) : std::span<const std::uint8_t>(), m_body);
		m_codec.pack(m_body, m_packed);
		const std::size_t bytes = sendFragments(*m_net, m_cfg.addrs[static_cast<std::size_t>(p)], now.frame, m_packed, m_scratch);
		m_stats.bytesSent += bytes;
		m_stats.packetsSent += (m_packed.size() + kFragmentBytes - 1) / kFragmentBytes;
		++m_stats.snapshots;
		if (base == nullptr) ++m_stats.fullSnapshots;
	}

	/// @brief 入力の packet: この状態の直前までに使った全員の入力 (古い方から)。同じボタンが続くので zstd で詰める
	void buildInputs()
	{
		const int count = (std::min)(inputHistoryFrames(m_cfg.snapshotEvery), m_stats.frame);
		const int first = m_stats.frame - count;
		m_body.clear();
		putLe(m_body, static_cast<std::uint32_t>(first), 4);
		putLe(m_body, static_cast<std::uint64_t>(count), 1);
		putLe(m_body, static_cast<std::uint64_t>(m_cfg.players), 1);
		for (int f = first; f < m_stats.frame; ++f)
		{
			const auto& row = m_inputLog[static_cast<std::size_t>(f) % m_inputLog.size()];
			for (int p = 0; p < m_cfg.players; ++p) putPadInput(m_body, row[static_cast<std::size_t>(p)]);
			// 席ごとに、そのフレームまでに使った入力の番号 (参加者が自分の先読みで、まだ使われていない入力を数える)
			const auto& seqs = m_seqLog[static_cast<std::size_t>(f) % m_seqLog.size()];
			for (int p = 0; p < m_cfg.players; ++p) putLe(m_body, seqs[static_cast<std::size_t>(p)], 4);
		}
		m_codec.pack(m_body, m_packed);
		m_inputs.clear();
		putLe(m_inputs, kMsgInputs, 1);
		m_inputs.insert(m_inputs.end(), m_packed.begin(), m_packed.end());
	}

	void advance(const PadInput& local)
	{
		std::array<PadInput, kMaxPlayers> now{};
		std::array<std::uint32_t, kMaxPlayers> seqs{kNoFrame, kNoFrame, kNoFrame, kNoFrame};
		for (int p = 0; p < m_cfg.players; ++p)
		{
			if (p == m_cfg.localPlayer)
			{
				now[static_cast<std::size_t>(p)] = local;
				continue;
			}
			Seat& s = seat(p);
			now[static_cast<std::size_t>(p)] = consume(s);
			if (s.present && s.any && s.next != s.first) seqs[static_cast<std::size_t>(p)] = s.next - 1;
		}
		if (m_stats.frame == m_watchFrame)
		{
			m_watchedSum = imageChecksum();
			m_watched = true;
		}
		const auto n = static_cast<std::size_t>(m_cfg.players);
		rollback::composeSnapshot(*m_snap, std::span<const PadInput>(now.data(), n), std::span<const PadInput>(m_prev.data(), n),
			m_cfg.keymaps, m_cfg.snapshot);
		m_intents->reset();
		const bool ok = m_calls.update != nullptr ? m_calls.update(m_calls.ctx, m_snap.get(), m_intents.get())
		                                          : (m_update(m_memory, m_cfg.snapshot.dt, m_snap.get(), m_intents.get()), true);
		if (!ok)
		{
			fail("on_update で game が止まった");
			return;
		}
		m_inputLog[static_cast<std::size_t>(m_stats.frame) % m_inputLog.size()] = now;
		m_seqLog[static_cast<std::size_t>(m_stats.frame) % m_seqLog.size()] = seqs;
		m_prev = now;
		m_fresh = true;
		++m_stats.frame;
	}

	[[nodiscard]] std::uint32_t imageChecksum()
	{
		if (!captureImage(m_watchImage)) return 0;
		return rollback::stateChecksum(m_watchImage.data(), m_watchImage.size());
	}

	using UpdateFn = void (*)(void*, float, const module::InputSnapshot*, module::FrameIntents*);
	UpdateFn m_update = nullptr;
	std::uint32_t m_memorySize = 0;
	void* m_memory = nullptr;
	AuthorityConfig m_cfg;
	DatagramEndpoint* m_net = nullptr;
	module::SideStateHost* m_sides = nullptr;
	RollbackCalls m_calls{};
	std::array<Seat, kMaxPlayers> m_seats{};
	std::array<Sent, kHistory> m_history{};
	std::array<std::array<PadInput, kMaxPlayers>, 64> m_inputLog{};
	std::array<std::array<std::uint32_t, kMaxPlayers>, 64> m_seqLog{};   ///< フレームごと・席ごとに、使い終えた入力の番号
	std::array<PadInput, kMaxPlayers> m_prev{};
	std::unique_ptr<module::FrameIntents> m_intents;
	std::unique_ptr<module::InputSnapshot> m_snap;
	SnapshotCodec m_codec;
	std::vector<std::uint8_t> m_side;
	std::vector<std::uint8_t> m_body;
	std::vector<std::uint8_t> m_inputs;
	std::vector<std::uint8_t> m_packed;
	std::vector<std::uint8_t> m_scratch;
	std::vector<std::uint8_t> m_watchImage;
	AuthorityHostStats m_stats;
	std::string m_error;
	std::uint64_t m_now = 0;
	bool m_fresh = false;
	int m_watchFrame = -1;
	std::uint32_t m_watchedSum = 0;
	bool m_watched = false;
};

} // namespace mitiru::network::authority
