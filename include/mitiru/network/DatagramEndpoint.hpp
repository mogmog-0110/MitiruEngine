#pragma once

/// @file DatagramEndpoint.hpp
/// @brief オンライン協力プレイの回線。UDP の口 1 つを、相手探し (lobby)・ロールバック (GekkoNet)・host 権威の 3 本に分けて使う
///
/// どの packet も先頭 1 byte が行き先 (kChannelLobby / kChannelRollback / kChannelAuthority)。同じ port で相手探しから対戦まで続けるので、
/// 相手に教える住所は 1 つで済む。待ち受けは既定で 127.0.0.1 だけ (ListenScope::Loopback)。外の相手を待つのは、
/// 利用者が自分で host を始めた時だけ ListenScope::Network にする (Windows のファイアウォールが確認を出す)。
///
/// LinkConditions を入れると、送る packet に遅延・揺れ・落ちを足す (試験と手元の確認用)。揺れで順番も入れ替わる。
/// 時計は呼ぶ側が渡すミリ秒で、壁時計は読まない。

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/network/NetAddress.hpp>
#include <mitiru/network/NetworkTypes.hpp>
#include <mitiru/network/SocketCompat.hpp>

namespace mitiru::network
{

inline constexpr std::uint8_t kChannelLobby = 'L';
inline constexpr std::uint8_t kChannelRollback = 'G';
inline constexpr std::uint8_t kChannelAuthority = 'A';

/// @brief 送る packet に足す回線の悪さ (試験用)
struct LinkConditions
{
	std::uint32_t latencyMs = 0;   ///< 片道の遅延
	std::uint32_t jitterMs = 0;    ///< 遅延に 0..jitterMs を足す (packet ごと)
	float lossPercent = 0.0f;      ///< この割合 (%) の packet を捨てる
	std::uint32_t seed = 1;

	[[nodiscard]] bool active() const noexcept { return latencyMs > 0 || jitterMs > 0 || lossPercent > 0.0f; }
};

/// @brief "latency,jitter,loss" (ミリ秒, ミリ秒, %) を読む。足りない項目は 0
[[nodiscard]] inline std::optional<LinkConditions> parseLinkConditions(std::string_view text)
{
	LinkConditions c;
	float values[3] = {};
	int n = 0;
	std::string token;
	for (std::size_t i = 0; i <= text.size(); ++i)
	{
		if (i < text.size() && text[i] != ',') { token.push_back(text[i]); continue; }
		if (n >= 3 || token.empty()) return std::nullopt;
		char* end = nullptr;
		values[n++] = std::strtof(token.c_str(), &end);
		if (end == nullptr || *end != '\0' || !std::isfinite(values[n - 1]) || values[n - 1] < 0.0f) return std::nullopt;
		token.clear();
	}
	if (values[0] > 60000.0f || values[1] > 60000.0f || values[2] > 100.0f) return std::nullopt;
	c.latencyMs = static_cast<std::uint32_t>(values[0]);
	c.jitterMs = static_cast<std::uint32_t>(values[1]);
	c.lossPercent = values[2];
	return c;
}

/// @brief 外へ出る口の IPv4 (参加者に伝える住所を推し量る)。分からなければ 0
/// @details UDP の connect は送り先を覚えるだけで packet を送らないので、どこへも何も届かない。
[[nodiscard]] inline std::uint32_t guessLanAddressV4() noexcept
{
	WsaGuard wsa;
	if (!wsa.isInitialized()) return 0;
	const SocketHandle s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCK) return 0;
	sockaddr_in to{};
	to.sin_family = AF_INET;
	to.sin_port = htons(9);
	to.sin_addr.s_addr = htonl(0xC0000201u);   // 192.0.2.1 (文書用に取ってある住所)
	std::uint32_t ip = 0;
	if (::connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0)
	{
		sockaddr_in self{};
#ifdef _WIN32
		int len = sizeof(self);
#else
		socklen_t len = sizeof(self);
#endif
		if (::getsockname(s, reinterpret_cast<sockaddr*>(&self), &len) == 0) ip = ntohl(self.sin_addr.s_addr);
	}
	closeSocket(s);
	return ip;
}

struct Datagram
{
	NetAddress from;
	std::vector<std::uint8_t> bytes;   ///< 行き先の 1 byte を外した中身
};

class DatagramEndpoint
{
public:
	DatagramEndpoint() = default;
	~DatagramEndpoint() { close(); }
	DatagramEndpoint(const DatagramEndpoint&) = delete;
	DatagramEndpoint& operator=(const DatagramEndpoint&) = delete;

	/// @brief 口を開く。port 0 は OS が選ぶ
	bool open(ListenScope scope, std::uint16_t port, std::string* error)
	{
		close();
		if (!m_wsa.isInitialized()) return fail(error, "ソケットを使えない (WSAStartup が失敗した)");
		m_socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_socket == INVALID_SOCK) return fail(error, "UDP の口を作れない");
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(listenAddressV4(scope));
		addr.sin_port = htons(port);
		if (!setNonBlocking(m_socket) || ::bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR)
		{
			close();
			return fail(error, "port " + std::to_string(port) + " を開けない (他のプログラムが使っているかもしれない)");
		}
		// host 権威の状態は数十 KB を 1 度に送る。OS の既定の受け箱 (Windows は 64 KB) では読む前にあふれて全部落ちる
		const int buffer = kSocketBuffer;
		(void)::setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
		(void)::setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
		m_recv.resize(kMaxDatagram);
		return true;
	}

	void close() noexcept
	{
		if (m_socket != INVALID_SOCK) closeSocket(m_socket);
		m_socket = INVALID_SOCK;
	}

	[[nodiscard]] bool isOpen() const noexcept { return m_socket != INVALID_SOCK; }

	/// @brief 開いた口の住所 (Network で開いた時の ip は 0 = 全部の口)
	[[nodiscard]] NetAddress localAddress() const noexcept
	{
		sockaddr_in addr{};
#ifdef _WIN32
		int len = sizeof(addr);
#else
		socklen_t len = sizeof(addr);
#endif
		if (m_socket == INVALID_SOCK || ::getsockname(m_socket, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return {};
		return NetAddress{ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port)};
	}

	void setConditions(const LinkConditions& c) noexcept
	{
		m_link = c;
		m_rng = c.seed != 0 ? c.seed : 1u;
	}

	/// @brief 時計を進め、時刻の来た packet を送り、届いた packet を行き先ごとの箱に入れる
	void pump(std::uint64_t nowMs)
	{
		m_now = nowMs;
		flushDue();
		readSocket();
	}

	[[nodiscard]] std::uint64_t now() const noexcept { return m_now; }

	void send(std::uint8_t channel, NetAddress to, const void* data, std::size_t size)
	{
		if (m_socket == INVALID_SOCK || !to.valid()) return;
		Pending p{to, m_now, {}};
		p.bytes.resize(size + 1);
		p.bytes[0] = channel;
		if (size > 0) std::memcpy(p.bytes.data() + 1, data, size);
		if (!m_link.active()) { sendNow(p); return; }
		if (m_link.lossPercent > 0.0f && static_cast<float>(nextRandom() % 10000u) < m_link.lossPercent * 100.0f)
		{
			++m_dropped;
			return;
		}
		p.deliverAt = m_now + m_link.latencyMs + (m_link.jitterMs > 0 ? nextRandom() % (m_link.jitterMs + 1u) : 0u);
		m_outgoing.push_back(std::move(p));
	}

	/// @brief 届いた packet (行き先ごと)。読んだ側が clear する
	[[nodiscard]] std::vector<Datagram>& inbox(std::uint8_t channel) noexcept
	{
		return channel == kChannelLobby ? m_lobby : channel == kChannelAuthority ? m_authority : m_rollback;
	}

	[[nodiscard]] std::uint64_t sentPackets() const noexcept { return m_sent; }
	[[nodiscard]] std::uint64_t receivedPackets() const noexcept { return m_received; }
	[[nodiscard]] std::uint64_t droppedPackets() const noexcept { return m_dropped; }

private:
	static constexpr std::size_t kMaxDatagram = 65536;
	static constexpr int kSocketBuffer = 4 * 1024 * 1024;
	/// 届いた packet を溜めておく上限。読まれない箱が際限なく伸びないように、超えた分は捨てる
	static constexpr std::size_t kMaxQueued = 1024;

	struct Pending
	{
		NetAddress to;
		std::uint64_t deliverAt = 0;
		std::vector<std::uint8_t> bytes;
	};

	static bool fail(std::string* error, std::string why)
	{
		if (error != nullptr) *error = std::move(why);
		return false;
	}

	[[nodiscard]] static bool isPeerGone(int err) noexcept
	{
#ifdef _WIN32
		return err == WSAECONNRESET;
#else
		return err == ECONNREFUSED;
#endif
	}

	[[nodiscard]] std::uint32_t nextRandom() noexcept
	{
		m_rng ^= m_rng << 13;
		m_rng ^= m_rng >> 17;
		m_rng ^= m_rng << 5;
		return m_rng;
	}

	void sendNow(const Pending& p)
	{
		sockaddr_in dest{};
		dest.sin_family = AF_INET;
		dest.sin_addr.s_addr = htonl(p.to.ip);
		dest.sin_port = htons(p.to.port);
		const int sent = ::sendto(m_socket, reinterpret_cast<const char*>(p.bytes.data()), static_cast<int>(p.bytes.size()), 0,
			reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
		if (sent != SOCK_ERR) ++m_sent;
	}

	void flushDue()
	{
		std::size_t kept = 0;
		for (std::size_t i = 0; i < m_outgoing.size(); ++i)
		{
			if (m_outgoing[i].deliverAt <= m_now) sendNow(m_outgoing[i]);
			else
			{
				if (kept != i) m_outgoing[kept] = std::move(m_outgoing[i]);
				++kept;
			}
		}
		m_outgoing.resize(kept);
	}

	void readSocket()
	{
		while (m_socket != INVALID_SOCK)
		{
			sockaddr_in from{};
#ifdef _WIN32
			int len = sizeof(from);
#else
			socklen_t len = sizeof(from);
#endif
			const int got = ::recvfrom(m_socket, reinterpret_cast<char*>(m_recv.data()), static_cast<int>(m_recv.size()), 0,
				reinterpret_cast<sockaddr*>(&from), &len);
			if (got == SOCK_ERR)
			{
				// 閉じた口へ送った後の ICMP (接続の拒否) は 1 回で消えるので読み続ける。止めると後ろの packet が届かない
				if (isPeerGone(lastSocketError())) continue;
				break;
			}
			if (got < 1) continue;
			++m_received;
			if (m_recv[0] != kChannelLobby && m_recv[0] != kChannelRollback && m_recv[0] != kChannelAuthority) continue;
			auto& box = inbox(m_recv[0]);
			if (box.size() >= kMaxQueued) continue;
			box.push_back(Datagram{NetAddress{ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)},
				std::vector<std::uint8_t>(m_recv.begin() + 1, m_recv.begin() + got)});
		}
	}

	WsaGuard m_wsa;
	SocketHandle m_socket = INVALID_SOCK;
	LinkConditions m_link{};
	std::uint32_t m_rng = 1;
	std::uint64_t m_now = 0;
	std::vector<std::uint8_t> m_recv;
	std::vector<Pending> m_outgoing;
	std::vector<Datagram> m_lobby;
	std::vector<Datagram> m_rollback;
	std::vector<Datagram> m_authority;
	std::uint64_t m_sent = 0;
	std::uint64_t m_received = 0;
	std::uint64_t m_dropped = 0;
};

} // namespace mitiru::network
