#pragma once

/// @file SimpleReliableUdp.hpp
/// @brief GameNetworkingSockets (GNS) 無し環境向けの最小 selective-repeat reliable UDP
/// @details `ReliableUDP.hpp` の `NullReliableTransport` は完全な no-op で、信頼性のある配送が
///          全く無かった (J8)。本実装は `UdpTransport` (または任意の `INetworkTransport`)
///          の上に、reliable 送信ごとの ACK + 再送タイマー + selective-repeat 受信側
///          reorder buffer を足す最小プロトコル。GNS の輻輳制御・暗号化・NAT punchthrough
///          等は持たない。LAN 内の小規模 P2P/専用サーバー用途の fallback としては十分な範囲。
///
/// ── ワイヤ形式 (payload 先頭に付与する 5 byte header) ──────────────────
///   1 byte 目: WireType (Data=0 / Ack=1 / DataUnreliable=2)。
///   続く 4 byte: seq (uint32 little-endian)。Data 系はこの packet 自身の連番、
///   Ack は ack 対象の連番を入れる。
///   Data / DataUnreliable はその後ろに実ペイロードが続く。
///
/// ── 再送方式 ────────────────────────────────────────────────────────────
///   reliable send() ごとに connection 単位の連番を振り、ACK が来るまで
///   `retransmitIntervalMs` ごとに再送する。`maxRetries` を超えたら諦めて捨てる
///   (呼び出し側への通知は無い。現状の既知の制約)。
///
/// ── 受信順序 ────────────────────────────────────────────────────────────
///   受信側は次に届けるべき連番 (`nextExpectedSeq`) を持つ。先着 (順序が先の) packet は
///   reorder buffer に保持し、gap が埋まった時点でまとめて順番に receive() へ渡す
///   (selective repeat)。重複 (既に届け済み) は ACK だけ返して破棄する。

#include <mitiru/network/INetworkTransport.hpp>
#include <mitiru/network/NetworkTypes.hpp>
#include <mitiru/network/ReliableUDP.hpp>
#include <mitiru/network/UdpTransport.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace mitiru::network::detail
{

/// @brief a が b より (mod 2^32 の巡回順で) 手前かどうか。差の絶対値が 2^31 未満である
/// 限り成立する符号付き差分比較 (RFC 1982 型のシーケンス番号比較)。単純な `a < b` だと
/// seq が uint32 の上限で 1 周したあと、実際には新しい packet の方が「小さい値」に
/// 見えて誤って既届け済み扱いになる。
[[nodiscard]] inline bool seqBefore(std::uint32_t a, std::uint32_t b) noexcept
{
	return static_cast<std::int32_t>(a - b) < 0;
}

}  // namespace mitiru::network::detail

namespace mitiru::network
{

/// @brief GNS 無し環境向けの最小 reliable UDP 実装 (`UdpTransport` の上に selective-repeat を足す)。
class SimpleReliableUdp final : public IReliableTransport
{
public:
	/// @brief 現在時刻を ms で返す関数 (テスト用に差し替え可能。既定は steady_clock)。
	using ClockFn = std::function<std::uint64_t()>;

	/// @param transport 下位トランスポート (既定: 実ソケットの UdpTransport)。
	///                  テストではパケットロスを注入できる fake INetworkTransport を渡せる。
	/// @param clock     現在時刻取得 (テストでは仮想時間を進める fake を渡せる)。
	explicit SimpleReliableUdp(
		std::unique_ptr<INetworkTransport> transport = std::make_unique<UdpTransport>(),
		ClockFn clock = &SimpleReliableUdp::steadyClockMs)
		: m_transport(std::move(transport))
		, m_clock(std::move(clock))
	{}

	// ── 接続管理 ──────────────────────────────────────────────────

	bool listen(std::uint16_t port) override
	{
		return m_transport->listen(port);
	}

	/// @brief 接続要求を出す。
	/// @return 成功時は暫定 ConnectionId (`UdpTransport` のクライアント固定 ID と同じ値: 1)。
	///         実際の Connected イベントは次の poll() で届く。失敗時は INVALID_CONNECTION。
	ConnectionId connect(std::string_view address, std::uint16_t port) override
	{
		if (!m_transport->connect(address, port)) { return INVALID_CONNECTION; }
		constexpr ConnectionId kClientConnectionId = 1;  // UdpTransport のクライアント固定 ID と同じ契約
		m_conns[kClientConnectionId].status = ConnectionStatus::Connecting;
		return kClientConnectionId;
	}

	void disconnect(ConnectionId connId) override
	{
		m_transport->disconnect(connId);
		m_conns.erase(connId);
	}

	// ── データ送受信 ──────────────────────────────────────────────

	void send(ConnectionId connId, const void* data, std::uint32_t size, bool reliable) override
	{
		const auto* bytes = static_cast<const std::uint8_t*>(data);

		if (!reliable)
		{
			auto wire = makeWire(WireType::DataUnreliable, 0, bytes, size);
			m_transport->send(connId, wire);
			return;
		}

		auto& conn = m_conns[connId];
		const std::uint32_t seq = conn.nextSendSeq++;
		auto wire = makeWire(WireType::Data, seq, bytes, size);
		m_transport->send(connId, wire);
		conn.unacked[seq] = OutstandingPacket{std::move(wire), m_clock(), 0};
	}

	[[nodiscard]] std::vector<ReceivedPacket> receive() override
	{
		auto out = std::move(m_receivedQueue);
		m_receivedQueue.clear();
		return out;
	}

	// ── ポーリング ────────────────────────────────────────────────

	void poll() override
	{
		for (auto& msg : m_transport->poll())
		{
			handleTransportMessage(msg);
		}
		retransmitDue();
	}

	// ── 状態照会 ──────────────────────────────────────────────────

	[[nodiscard]] ConnectionStatus getConnectionStatus(ConnectionId connId) const override
	{
		auto it = m_conns.find(connId);
		return (it != m_conns.end()) ? it->second.status : ConnectionStatus::Disconnected;
	}

	/// @brief 再送間隔 (既定 100ms)。テストでは短くして使う。
	void setRetransmitIntervalMs(std::uint64_t ms) noexcept { m_retransmitIntervalMs = ms; }

	/// @brief 再送を諦めるまでの最大試行回数 (既定 10)。
	void setMaxRetries(int retries) noexcept { m_maxRetries = retries; }

private:
	enum class WireType : std::uint8_t { Data = 0, Ack = 1, DataUnreliable = 2 };

	struct OutstandingPacket
	{
		std::vector<std::uint8_t> wire;
		std::uint64_t lastSentMs = 0;
		int retries = 0;
	};

	struct ConnState
	{
		ConnectionStatus status = ConnectionStatus::Connecting;
		std::uint32_t nextSendSeq = 0;
		std::uint32_t nextExpectedSeq = 0;
		std::map<std::uint32_t, OutstandingPacket> unacked;              // 送信側: 未 ACK
		std::map<std::uint32_t, std::vector<std::uint8_t>> reorderBuffer; // 受信側: gap 待ち
	};

	[[nodiscard]] static std::uint64_t steadyClockMs()
	{
		using namespace std::chrono;
		return static_cast<std::uint64_t>(
			duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
	}

	[[nodiscard]] static std::vector<std::uint8_t> makeWire(
		WireType type, std::uint32_t seq, const std::uint8_t* payload, std::uint32_t size)
	{
		std::vector<std::uint8_t> wire(5 + static_cast<std::size_t>(size));
		wire[0] = static_cast<std::uint8_t>(type);
		std::memcpy(wire.data() + 1, &seq, 4);
		if (size > 0 && payload) { std::memcpy(wire.data() + 5, payload, size); }
		return wire;
	}

	void handleTransportMessage(const NetworkMessage& msg)
	{
		const auto event = static_cast<NetworkEvent>(msg.header.type);
		switch (event)
		{
		case NetworkEvent::Connected:
		{
			m_conns[msg.sender].status = ConnectionStatus::Connected;
			fireOnConnected(msg.sender);
			return;
		}
		case NetworkEvent::Disconnected:
		{
			m_conns.erase(msg.sender);
			fireOnDisconnected(msg.sender);
			return;
		}
		case NetworkEvent::DataReceived:
			handleWirePacket(msg.sender, msg.payload);
			return;
		default:
			return;
		}
	}

	void handleWirePacket(ConnectionId sender, const std::vector<std::uint8_t>& wire)
	{
		if (wire.size() < 5) { return; }  // 壊れた packet。header すら無い

		const auto type = static_cast<WireType>(wire[0]);
		std::uint32_t seq = 0;
		std::memcpy(&seq, wire.data() + 1, 4);

		if (type == WireType::Ack)
		{
			auto it = m_conns.find(sender);
			if (it != m_conns.end()) { it->second.unacked.erase(seq); }
			return;
		}

		if (type == WireType::DataUnreliable)
		{
			ReceivedPacket pkt;
			pkt.connectionId = sender;
			pkt.data.assign(wire.begin() + 5, wire.end());
			pkt.size = static_cast<std::uint32_t>(pkt.data.size());
			pkt.reliable = false;
			m_receivedQueue.push_back(std::move(pkt));
			fireOnData(m_receivedQueue.back());
			return;
		}

		// type == Data。ACK は届いたことが分かった時点で必ず返す
		// (順序整合はこちらの都合なので、重複でも ACK 自体は再送する)。
		sendAck(sender, seq);

		auto& conn = m_conns[sender];
		if (detail::seqBefore(seq, conn.nextExpectedSeq)) { return; }  // 既に届け済み。ACK 再送のみで破棄
		if (seq != conn.nextExpectedSeq)
		{
			// gap あり。selective repeat の reorder buffer に保持する。
			conn.reorderBuffer.emplace(seq, std::vector<std::uint8_t>(wire.begin() + 5, wire.end()));
			return;
		}

		// seq == nextExpectedSeq。順番どおり届いた。deliver してから buffer 内の
		// 連続分もまとめて deliver する。
		deliverInOrder(sender, conn, std::vector<std::uint8_t>(wire.begin() + 5, wire.end()));
	}

	void deliverInOrder(ConnectionId sender, ConnState& conn, std::vector<std::uint8_t> firstPayload)
	{
		ReceivedPacket pkt;
		pkt.connectionId = sender;
		pkt.data = std::move(firstPayload);
		pkt.size = static_cast<std::uint32_t>(pkt.data.size());
		pkt.reliable = true;
		m_receivedQueue.push_back(std::move(pkt));
		fireOnData(m_receivedQueue.back());
		++conn.nextExpectedSeq;

		// buffer 済みの続き (gap が埋まった分) を連番順にまとめて deliver する。
		for (auto it = conn.reorderBuffer.find(conn.nextExpectedSeq);
		     it != conn.reorderBuffer.end();
		     it = conn.reorderBuffer.find(conn.nextExpectedSeq))
		{
			ReceivedPacket next;
			next.connectionId = sender;
			next.data = std::move(it->second);
			next.size = static_cast<std::uint32_t>(next.data.size());
			next.reliable = true;
			conn.reorderBuffer.erase(it);
			m_receivedQueue.push_back(std::move(next));
			fireOnData(m_receivedQueue.back());
			++conn.nextExpectedSeq;
		}
	}

	void sendAck(ConnectionId connId, std::uint32_t seq)
	{
		auto wire = makeWire(WireType::Ack, seq, nullptr, 0);
		m_transport->send(connId, wire);
	}

	void retransmitDue()
	{
		const std::uint64_t now = m_clock();
		for (auto& [connId, conn] : m_conns)
		{
			for (auto it = conn.unacked.begin(); it != conn.unacked.end();)
			{
				auto& pkt = it->second;
				if (now - pkt.lastSentMs < m_retransmitIntervalMs) { ++it; continue; }

				if (pkt.retries >= m_maxRetries)
				{
					// 諦めて捨てる (呼び出し側への通知は現状無い。既知の制約)。
					it = conn.unacked.erase(it);
					continue;
				}
				m_transport->send(connId, pkt.wire);
				pkt.lastSentMs = now;
				++pkt.retries;
				++it;
			}
		}
	}

	std::unique_ptr<INetworkTransport> m_transport;
	ClockFn m_clock;
	std::map<ConnectionId, ConnState> m_conns;
	std::vector<ReceivedPacket> m_receivedQueue;
	std::uint64_t m_retransmitIntervalMs = 100;
	int m_maxRetries = 10;
};

} // namespace mitiru::network
