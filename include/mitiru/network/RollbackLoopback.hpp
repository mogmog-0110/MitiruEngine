#pragma once

/// @file RollbackLoopback.hpp
/// @brief 同じプロセスの中で GekkoNet の peer 同士をつなぐ回線 (遅延を tick 単位で足せる)
///
/// 実ソケットを使わず、packet を宛先の受信箱に積んで latencyTicks 後に渡す。遅延は tick (= 呼ぶ側の
/// 1 フレーム) で数えるので、同じ入力なら毎回同じ順で packet が届き、ロールバックの起き方まで再現する。
/// GekkoNet の adapter は関数ポインタだけで利用者のポインタを持てないため、同時に生きられる
/// RollbackLoopback は 1 つだけ (2 つ目を作ると active() が差し替わる)。

#if defined(MITIRU_HAS_GEKKONET)

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include <gekkonet.h>

namespace mitiru::network::rollback
{

class RollbackLoopback
{
public:
	static constexpr int kMaxPeers = 4;

	explicit RollbackLoopback(int latencyTicks) : m_latency(latencyTicks)
	{
		for (int i = 0; i < kMaxPeers; ++i) m_ids[i] = static_cast<std::uint8_t>(i);
		active() = this;
	}
	~RollbackLoopback()
	{
		if (active() == this) active() = nullptr;
	}
	RollbackLoopback(const RollbackLoopback&) = delete;
	RollbackLoopback& operator=(const RollbackLoopback&) = delete;

	/// @brief peer 番号 (0..kMaxPeers-1) の送受信口。GekkoSession に渡す
	[[nodiscard]] GekkoNetAdapter* adapter(int peer) noexcept
	{
		return (peer >= 0 && peer < kMaxPeers) ? &adapters()[peer] : nullptr;
	}

	/// @brief peer 番号を宛先として gekko_add_actor に渡す住所
	[[nodiscard]] GekkoNetAddress address(int peer) noexcept
	{
		if (peer < 0 || peer >= kMaxPeers) return GekkoNetAddress{nullptr, 0};
		return GekkoNetAddress{&m_ids[peer], 1};
	}

	/// @brief 1 フレーム進める (遅延の時計)
	void tick() noexcept { ++m_now; }

	[[nodiscard]] std::uint64_t packetsSent() const noexcept { return m_sent; }

private:
	struct Packet
	{
		int from = 0;
		std::int64_t deliverAt = 0;
		std::vector<char> bytes;
	};

	static RollbackLoopback*& active() noexcept
	{
		static RollbackLoopback* s = nullptr;
		return s;
	}

	template <int Peer>
	static void sendFrom(GekkoNetAddress* addr, const char* data, int length)
	{
		RollbackLoopback* net = active();
		if (net == nullptr || addr == nullptr || addr->size != 1) return;
		const int to = static_cast<const std::uint8_t*>(addr->data)[0];
		if (to >= kMaxPeers) return;
		net->m_inbox[to].push_back(Packet{Peer, net->m_now + net->m_latency, std::vector<char>(data, data + length)});
		++net->m_sent;
	}

	/// @return GekkoNet が free_data で 1 つずつ解放する (結果・住所・中身は別々に malloc する)
	template <int Peer>
	static GekkoNetResult** receiveAt(int* length)
	{
		length[0] = 0;
		RollbackLoopback* net = active();
		if (net == nullptr) return nullptr;
		auto& inbox = net->m_inbox[Peer];
		auto& out = net->m_handedOut[Peer];
		out.clear();
		while (!inbox.empty() && inbox.front().deliverAt <= net->m_now)
		{
			out.push_back(toResult(inbox.front()));
			inbox.pop_front();
		}
		length[0] = static_cast<int>(out.size());
		return out.data();
	}

	[[nodiscard]] static GekkoNetResult* toResult(const Packet& p)
	{
		auto* r = static_cast<GekkoNetResult*>(std::malloc(sizeof(GekkoNetResult)));
		auto* from = static_cast<std::uint8_t*>(std::malloc(1));
		void* data = std::malloc(p.bytes.size());
		from[0] = static_cast<std::uint8_t>(p.from);
		std::memcpy(data, p.bytes.data(), p.bytes.size());
		r->addr = GekkoNetAddress{from, 1};
		r->data_len = static_cast<unsigned int>(p.bytes.size());
		r->data = data;
		return r;
	}

	static void freeData(void* p) { std::free(p); }

	static GekkoNetAdapter* adapters() noexcept
	{
		static GekkoNetAdapter table[kMaxPeers] = {
			{&sendFrom<0>, &receiveAt<0>, &freeData},
			{&sendFrom<1>, &receiveAt<1>, &freeData},
			{&sendFrom<2>, &receiveAt<2>, &freeData},
			{&sendFrom<3>, &receiveAt<3>, &freeData},
		};
		return table;
	}

	int m_latency = 0;
	std::int64_t m_now = 0;
	std::uint64_t m_sent = 0;
	std::uint8_t m_ids[kMaxPeers] = {};
	std::deque<Packet> m_inbox[kMaxPeers];
	std::vector<GekkoNetResult*> m_handedOut[kMaxPeers];
};

} // namespace mitiru::network::rollback

#endif // MITIRU_HAS_GEKKONET
