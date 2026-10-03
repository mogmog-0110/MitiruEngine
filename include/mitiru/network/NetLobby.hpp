#pragma once

/// @file NetLobby.hpp
/// @brief オンライン協力プレイの相手探し。host が席を配り、全員の準備ができたら対戦の始まりを配る
///
/// host は自分の席 (0 番) を持ち、参加者は host の住所 (か参加コード) に Hello を送って席をもらう。
/// Hello には GameFingerprint (ABI の番号、GameMemory の大きさと初期値の checksum、窓口の checksum) を載せ、
/// host と違えば席を渡さずに断る。同じゲームでも DLL のビルドが違うと、遊び始めてから食い違うため。
/// 全員の席が埋まって準備ができたら、host は Start (人数・席・入力遅延・seed・論理解像度・全員の住所) を配る。
/// Start が落ちても、参加者は ping を送り続けるので、host はその返事に Start をもう一度載せる。
///
/// ping は待合室の間だけ測る (遊び始めた後は GekkoNet が測る)。時計は呼ぶ側が渡すミリ秒。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <mitiru/network/DatagramEndpoint.hpp>
#include <mitiru/network/NetAddress.hpp>

namespace mitiru::network
{

/// @brief 同じゲームかを確かめる値。全員が同じでないと遊べない
struct GameFingerprint
{
	std::uint32_t abi = 0;            ///< module::kWireApiVersion
	std::uint32_t memorySize = 0;
	std::uint32_t memoryChecksum = 0; ///< on_init 直後の GameMemory
	std::uint32_t sideChecksum = 0;   ///< on_init 直後の窓口の image (無ければ 0)
	friend bool operator==(const GameFingerprint&, const GameFingerprint&) = default;
};

struct LobbySeat
{
	bool occupied = false;
	bool ready = false;
	std::uint16_t pingMs = 0;         ///< host から見た往復の時間
	NetAddress addr{};                ///< host から見た住所 (host の席は 0)
	std::uint64_t lastSeenMs = 0;
};

/// @brief 対戦の始まり。host が決め、全員が同じ値で GekkoNet の session を作る
struct LobbyStart
{
	int players = 2;
	int seat = 0;                     ///< 受け取った人の番号
	int inputDelay = 2;
	int predictionWindow = 8;
	std::uint64_t rngSeed = 1;
	std::uint16_t logicalW = 1280;
	std::uint16_t logicalH = 720;
	std::array<NetAddress, 4> addrs{};   ///< 席ごとの住所 (受け取った側で host の席は host へ送った住所に直す)
};

enum class LobbyPhase : std::uint8_t
{
	Connecting,   ///< 参加者が host の返事を待っている
	Waiting,      ///< 待合室
	Started,
	Failed,
};

class NetLobby
{
public:
	static constexpr int kMaxSeats = 4;
	static constexpr std::uint64_t kPingEveryMs = 250;
	static constexpr std::uint64_t kTimeoutMs = 5000;
	static constexpr std::uint64_t kConnectTimeoutMs = 10000;

	/// @param params 席・住所以外の対戦の決まり (人数・遅延・seed・解像度)
	void host(DatagramEndpoint& net, const GameFingerprint& game, const LobbyStart& params, std::uint64_t nowMs,
		bool ready = true)
	{
		this->operator=(NetLobby{});
		m_ready = ready;
		m_net = &net;
		m_hosting = true;
		m_game = game;
		m_start = params;
		m_players = params.players;
		m_phase = LobbyPhase::Waiting;
		m_seats[0].occupied = true;
		m_seats[0].ready = ready;
		m_lastPing = nowMs;
	}

	void join(DatagramEndpoint& net, NetAddress hostAddr, const GameFingerprint& game, std::uint64_t nowMs, bool ready = true)
	{
		this->operator=(NetLobby{});
		m_ready = ready;
		m_net = &net;
		m_hostAddr = hostAddr;
		m_game = game;
		m_phase = LobbyPhase::Connecting;
		m_now = nowMs;
		m_began = nowMs;
		m_lastHeard = nowMs;
		m_lastPing = nowMs;
		sendHello();
	}

	/// @brief 自分の準備。host は全員の準備ができた時に始める
	void setReady(bool ready)
	{
		m_ready = ready;
		if (m_hosting) m_seats[0].ready = ready;
		else if (m_phase == LobbyPhase::Waiting) sendPing();
	}

	/// @brief 届いた packet を処理し、決まった間隔の送信と時間切れを見る。endpoint.pump の後に呼ぶ
	void update(std::uint64_t nowMs)
	{
		if (m_net == nullptr) return;
		m_now = nowMs;
		auto& box = m_net->inbox(kChannelLobby);
		for (const Datagram& d : box)
		{
			if (m_hosting) hostReceive(d);
			else joinReceive(d);
		}
		box.clear();
		if (m_phase == LobbyPhase::Failed) return;
		if (m_hosting) hostTick();
		else joinTick();
	}

	[[nodiscard]] LobbyPhase phase() const noexcept { return m_phase; }
	[[nodiscard]] bool hosting() const noexcept { return m_hosting; }
	[[nodiscard]] bool ready() const noexcept { return m_ready; }
	[[nodiscard]] int players() const noexcept { return m_players; }
	[[nodiscard]] int localSeat() const noexcept { return m_hosting ? 0 : m_seat; }
	[[nodiscard]] const LobbySeat& seat(int i) const noexcept { return m_seats[static_cast<std::size_t>(i)]; }
	[[nodiscard]] std::uint16_t pingToHostMs() const noexcept { return m_pingToHost; }
	[[nodiscard]] const LobbyStart& start() const noexcept { return m_start; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
	enum Msg : std::uint8_t { kHello = 1, kWelcome, kRefuse, kPing, kPong, kRoster, kStart };
	enum Refusal : std::uint8_t { kFull = 1, kGameMismatch, kAlreadyStarted, kProtocol };
	static constexpr std::uint32_t kMagic = 0x314E544Du;   // "MTN1"

	struct Writer
	{
		std::vector<std::uint8_t> bytes;
		Writer& put(std::uint64_t v, int n)
		{
			for (int i = 0; i < n; ++i) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
			return *this;
		}
	};

	struct Reader
	{
		const std::vector<std::uint8_t>& bytes;
		std::size_t at = 0;
		bool ok = true;
		std::uint64_t get(int n)
		{
			if (at + static_cast<std::size_t>(n) > bytes.size()) { ok = false; return 0; }
			std::uint64_t v = 0;
			for (int i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
			at += static_cast<std::size_t>(n);
			return v;
		}
	};

	[[nodiscard]] LobbySeat& seatAt(int i) noexcept { return m_seats[static_cast<std::size_t>(i)]; }
	void send(NetAddress to, const Writer& w) { m_net->send(kChannelLobby, to, w.bytes.data(), w.bytes.size()); }

	void sendHello()
	{
		Writer w;
		w.put(kHello, 1).put(kMagic, 4).put(m_game.abi, 4).put(m_game.memorySize, 4).put(m_game.memoryChecksum, 4)
			.put(m_game.sideChecksum, 4).put(m_ready ? 1 : 0, 1);
		send(m_hostAddr, w);
	}

	void sendPing() { send(m_hostAddr, Writer{}.put(kPing, 1).put(m_now, 8).put(m_ready ? 1 : 0, 1)); }

	[[nodiscard]] static std::uint16_t elapsedMs(std::uint64_t now, std::uint64_t sent) noexcept
	{
		return static_cast<std::uint16_t>(std::min<std::uint64_t>(now - sent, 65535));
	}

	// ── host の側 ───────────────────────────────────────────────────────

	[[nodiscard]] int seatOf(NetAddress a) noexcept
	{
		for (int i = 1; i < m_players; ++i)
		{
			if (seatAt(i).occupied && seatAt(i).addr == a) return i;
		}
		return -1;
	}

	void hostReceive(const Datagram& d)
	{
		Reader r{d.bytes};
		const auto type = static_cast<std::uint8_t>(r.get(1));
		const int s = seatOf(d.from);
		if (s > 0) seatAt(s).lastSeenMs = m_now;
		if (type == kHello) { hostHello(d.from, r, s); return; }
		if (s < 0) return;
		if (type == kPing) hostPing(d.from, r, s);
		else if (type == kPong)
		{
			const std::uint64_t sent = r.get(8);
			if (r.ok && sent <= m_now) seatAt(s).pingMs = elapsedMs(m_now, sent);
		}
	}

	void hostPing(NetAddress from, Reader& r, int s)
	{
		const std::uint64_t sent = r.get(8);
		const bool ready = r.get(1) != 0;
		if (!r.ok) return;
		if (m_phase != LobbyPhase::Started) seatAt(s).ready = ready;
		send(from, Writer{}.put(kPong, 1).put(sent, 8));
		if (m_phase == LobbyPhase::Started) sendStart(s);   // Start が落ちた参加者は待合室の ping を送り続ける
	}

	void hostHello(NetAddress from, Reader& r, int known)
	{
		const auto magic = static_cast<std::uint32_t>(r.get(4));
		GameFingerprint g;
		g.abi = static_cast<std::uint32_t>(r.get(4));
		g.memorySize = static_cast<std::uint32_t>(r.get(4));
		g.memoryChecksum = static_cast<std::uint32_t>(r.get(4));
		g.sideChecksum = static_cast<std::uint32_t>(r.get(4));
		const bool ready = r.get(1) != 0;
		if (!r.ok || magic != kMagic) { refuse(from, kProtocol); return; }
		if (known > 0) { welcome(known); return; }   // Welcome が落ちた参加者がもう一度送ってきた
		if (m_phase == LobbyPhase::Started) { refuse(from, kAlreadyStarted); return; }
		if (!(g == m_game)) { refuse(from, kGameMismatch); return; }
		for (int i = 1; i < m_players; ++i)
		{
			if (seatAt(i).occupied) continue;
			seatAt(i) = LobbySeat{true, ready, 0, from, m_now};
			welcome(i);
			return;
		}
		refuse(from, kFull);
	}

	void welcome(int s)
	{
		send(seatAt(s).addr, Writer{}.put(kWelcome, 1).put(static_cast<std::uint64_t>(s), 1).put(static_cast<std::uint64_t>(m_players), 1));
	}

	void refuse(NetAddress to, Refusal why) { send(to, Writer{}.put(kRefuse, 1).put(why, 1)); }

	void hostTick()
	{
		if (m_phase == LobbyPhase::Started) return;
		for (int i = 1; i < m_players; ++i)
		{
			if (seatAt(i).occupied && m_now > seatAt(i).lastSeenMs + kTimeoutMs) seatAt(i) = LobbySeat{};
		}
		if (m_now >= m_lastPing + kPingEveryMs)
		{
			m_lastPing = m_now;
			const Writer roster = rosterMessage();
			for (int i = 1; i < m_players; ++i)
			{
				if (!seatAt(i).occupied) continue;
				send(seatAt(i).addr, Writer{}.put(kPing, 1).put(m_now, 8).put(1, 1));
				send(seatAt(i).addr, roster);
			}
		}
		if (everyoneReady()) beginMatch();
	}

	[[nodiscard]] bool everyoneReady() noexcept
	{
		for (int i = 0; i < m_players; ++i)
		{
			if (!seatAt(i).occupied || !seatAt(i).ready) return false;
		}
		return true;
	}

	[[nodiscard]] Writer rosterMessage()
	{
		Writer w;
		w.put(kRoster, 1).put(static_cast<std::uint64_t>(m_players), 1);
		for (int i = 0; i < m_players; ++i)
		{
			const LobbySeat& seat = seatAt(i);
			w.put(seat.occupied ? 1 : 0, 1).put(seat.ready ? 1 : 0, 1).put(seat.pingMs, 2).put(seat.addr.ip, 4).put(seat.addr.port, 2);
		}
		return w;
	}

	void beginMatch()
	{
		m_phase = LobbyPhase::Started;
		m_start.players = m_players;
		m_start.seat = 0;
		for (int i = 0; i < m_players; ++i) m_start.addrs[static_cast<std::size_t>(i)] = seatAt(i).addr;
		for (int i = 1; i < m_players; ++i) sendStart(i);
	}

	void sendStart(int s)
	{
		Writer w;
		w.put(kStart, 1).put(static_cast<std::uint64_t>(m_players), 1).put(static_cast<std::uint64_t>(s), 1)
			.put(static_cast<std::uint64_t>(m_start.inputDelay), 1).put(static_cast<std::uint64_t>(m_start.predictionWindow), 1)
			.put(m_start.rngSeed, 8).put(m_start.logicalW, 2).put(m_start.logicalH, 2);
		for (int i = 0; i < m_players; ++i) w.put(m_start.addrs[static_cast<std::size_t>(i)].ip, 4).put(m_start.addrs[static_cast<std::size_t>(i)].port, 2);
		send(seatAt(s).addr, w);
	}

	// ── 参加者の側 ──────────────────────────────────────────────────────

	void joinReceive(const Datagram& d)
	{
		if (!(d.from == m_hostAddr) || m_phase == LobbyPhase::Started) return;
		m_lastHeard = m_now;
		Reader r{d.bytes};
		const auto type = static_cast<std::uint8_t>(r.get(1));
		if (type == kWelcome) joinWelcome(r);
		else if (type == kRefuse) joinRefused(static_cast<std::uint8_t>(r.get(1)));
		else if (type == kPing)
		{
			const std::uint64_t sent = r.get(8);
			if (r.ok) send(m_hostAddr, Writer{}.put(kPong, 1).put(sent, 8));
		}
		else if (type == kPong)
		{
			const std::uint64_t sent = r.get(8);
			if (r.ok && sent <= m_now) m_pingToHost = elapsedMs(m_now, sent);
		}
		else if (type == kRoster) joinRoster(r);
		else if (type == kStart) joinStart(r);
	}

	void joinWelcome(Reader& r)
	{
		const int s = static_cast<int>(r.get(1));
		const int players = static_cast<int>(r.get(1));
		if (!r.ok || players < 2 || players > kMaxSeats || s <= 0 || s >= players) return;
		m_seat = s;
		m_players = players;
		if (m_phase == LobbyPhase::Connecting) m_phase = LobbyPhase::Waiting;
	}

	void joinRefused(std::uint8_t why)
	{
		m_phase = LobbyPhase::Failed;
		m_error = why == kFull ? "席が埋まっている"
		        : why == kGameMismatch ? "host とゲームが違う (DLL のビルドか GameMemory の初期値が違う)"
		        : why == kAlreadyStarted ? "もう始まっている"
		                                 : "相手が MitiruEngine の待合室でない (版が違う)";
	}

	void joinRoster(Reader& r)
	{
		const int players = static_cast<int>(r.get(1));
		if (!r.ok || players < 2 || players > kMaxSeats) return;
		std::array<LobbySeat, kMaxSeats> seats{};
		for (int i = 0; i < players; ++i)
		{
			LobbySeat& seat = seats[static_cast<std::size_t>(i)];
			seat.occupied = r.get(1) != 0;
			seat.ready = r.get(1) != 0;
			seat.pingMs = static_cast<std::uint16_t>(r.get(2));
			seat.addr.ip = static_cast<std::uint32_t>(r.get(4));
			seat.addr.port = static_cast<std::uint16_t>(r.get(2));
		}
		if (!r.ok) return;
		m_seats = seats;
		m_players = players;
	}

	void joinStart(Reader& r)
	{
		LobbyStart s;
		s.players = static_cast<int>(r.get(1));
		s.seat = static_cast<int>(r.get(1));
		s.inputDelay = static_cast<int>(r.get(1));
		s.predictionWindow = static_cast<int>(r.get(1));
		s.rngSeed = r.get(8);
		s.logicalW = static_cast<std::uint16_t>(r.get(2));
		s.logicalH = static_cast<std::uint16_t>(r.get(2));
		if (!r.ok || s.players < 2 || s.players > kMaxSeats || s.seat <= 0 || s.seat >= s.players) return;
		for (int i = 0; i < s.players; ++i)
		{
			s.addrs[static_cast<std::size_t>(i)].ip = static_cast<std::uint32_t>(r.get(4));
			s.addrs[static_cast<std::size_t>(i)].port = static_cast<std::uint16_t>(r.get(2));
		}
		if (!r.ok) return;
		s.addrs[0] = m_hostAddr;   // host は自分の住所を知らない。こちらが送った先がその住所
		m_start = s;
		m_seat = s.seat;
		m_players = s.players;
		m_phase = LobbyPhase::Started;
	}

	void joinTick()
	{
		if (m_phase == LobbyPhase::Started) return;
		if (m_phase == LobbyPhase::Connecting && m_now > m_began + kConnectTimeoutMs)
		{
			m_phase = LobbyPhase::Failed;
			m_error = "host から返事が無い (住所・port・ファイアウォールを確かめる)";
			return;
		}
		if (m_phase == LobbyPhase::Waiting && m_now > m_lastHeard + kTimeoutMs)
		{
			m_phase = LobbyPhase::Failed;
			m_error = "host との接続が切れた";
			return;
		}
		if (m_now < m_lastPing + kPingEveryMs) return;
		m_lastPing = m_now;
		if (m_phase == LobbyPhase::Connecting) sendHello();
		else sendPing();
	}

	DatagramEndpoint* m_net = nullptr;
	bool m_hosting = false;
	bool m_ready = true;
	LobbyPhase m_phase = LobbyPhase::Connecting;
	GameFingerprint m_game{};
	LobbyStart m_start{};
	int m_players = 2;
	int m_seat = -1;
	NetAddress m_hostAddr{};
	std::array<LobbySeat, kMaxSeats> m_seats{};
	std::uint16_t m_pingToHost = 0;
	std::uint64_t m_now = 0;
	std::uint64_t m_began = 0;
	std::uint64_t m_lastHeard = 0;
	std::uint64_t m_lastPing = 0;
	std::string m_error;
};

} // namespace mitiru::network
