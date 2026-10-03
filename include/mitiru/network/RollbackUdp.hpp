#pragma once

/// @file RollbackUdp.hpp
/// @brief GekkoNet の送受信を DatagramEndpoint (本物の UDP) の rollback 側の行き先へつなぐ
///
/// GekkoNet の住所は 6 byte (ip 4 + port 2、どちらもネットワークのバイト順)。adapter の関数は利用者の
/// ポインタを受け取らないので、同じプロセスで同時に使う回線は slot (0..3) で分ける。host は 1 つ、
/// 同じプロセスに 4 人を置く試験は 4 つ使う。

#if defined(MITIRU_HAS_GEKKONET)

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <gekkonet.h>

#include <mitiru/network/DatagramEndpoint.hpp>

namespace mitiru::network::rollback
{

inline constexpr std::size_t kWireAddressSize = 6;

inline void writeWireAddress(const NetAddress& a, std::uint8_t (&out)[kWireAddressSize]) noexcept
{
	out[0] = static_cast<std::uint8_t>(a.ip >> 24);
	out[1] = static_cast<std::uint8_t>(a.ip >> 16);
	out[2] = static_cast<std::uint8_t>(a.ip >> 8);
	out[3] = static_cast<std::uint8_t>(a.ip);
	out[4] = static_cast<std::uint8_t>(a.port >> 8);
	out[5] = static_cast<std::uint8_t>(a.port);
}

[[nodiscard]] inline NetAddress readWireAddress(const void* data) noexcept
{
	const auto* b = static_cast<const std::uint8_t*>(data);
	return NetAddress{(static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
		(static_cast<std::uint32_t>(b[2]) << 8) | b[3], static_cast<std::uint16_t>((b[4] << 8) | b[5])};
}

class RollbackUdp
{
public:
	static constexpr int kSlots = 4;

	/// @param slot 同じプロセスで同時に使う回線ごとに別の番号
	RollbackUdp(DatagramEndpoint& net, int slot) : m_net(&net), m_slot(slot)
	{
		if (slot >= 0 && slot < kSlots) active(slot) = this;
	}
	~RollbackUdp()
	{
		if (m_slot >= 0 && m_slot < kSlots && active(m_slot) == this) active(m_slot) = nullptr;
	}
	RollbackUdp(const RollbackUdp&) = delete;
	RollbackUdp& operator=(const RollbackUdp&) = delete;

	[[nodiscard]] GekkoNetAdapter* adapter() noexcept
	{
		return (m_slot >= 0 && m_slot < kSlots) ? &adapters()[m_slot] : nullptr;
	}

	/// @brief 席ごとの住所を GekkoNet に渡す形にする (自分の席も埋めるが使われない)
	void setRemotes(const std::array<NetAddress, 4>& addrs, int players) noexcept
	{
		for (int i = 0; i < players && i < 4; ++i)
		{
			writeWireAddress(addrs[static_cast<std::size_t>(i)], m_wire[static_cast<std::size_t>(i)]);
			m_remotes[static_cast<std::size_t>(i)] = GekkoNetAddress{m_wire[static_cast<std::size_t>(i)], kWireAddressSize};
		}
	}

	[[nodiscard]] const std::array<GekkoNetAddress, 4>& remotes() const noexcept { return m_remotes; }

private:
	static RollbackUdp*& active(int slot) noexcept
	{
		static RollbackUdp* s[kSlots] = {};
		return s[slot];
	}

	template <int Slot>
	static void sendFrom(GekkoNetAddress* addr, const char* data, int length)
	{
		RollbackUdp* self = active(Slot);
		if (self == nullptr || addr == nullptr || addr->size != kWireAddressSize || length < 0) return;
		self->m_net->send(kChannelRollback, readWireAddress(addr->data), data, static_cast<std::size_t>(length));
	}

	/// @return GekkoNet が free_data で 1 つずつ解放する (結果・住所・中身は別々に malloc する)
	template <int Slot>
	static GekkoNetResult** receiveAt(int* length)
	{
		length[0] = 0;
		RollbackUdp* self = active(Slot);
		if (self == nullptr) return nullptr;
		self->m_net->pump(self->m_net->now());
		auto& box = self->m_net->inbox(kChannelRollback);
		self->m_handedOut.clear();
		for (const Datagram& d : box) self->m_handedOut.push_back(toResult(d));
		box.clear();
		length[0] = static_cast<int>(self->m_handedOut.size());
		return self->m_handedOut.data();
	}

	[[nodiscard]] static GekkoNetResult* toResult(const Datagram& d)
	{
		auto* r = static_cast<GekkoNetResult*>(std::malloc(sizeof(GekkoNetResult)));
		auto* from = static_cast<std::uint8_t*>(std::malloc(kWireAddressSize));
		void* data = std::malloc(d.bytes.empty() ? 1 : d.bytes.size());
		std::uint8_t wire[kWireAddressSize];
		writeWireAddress(d.from, wire);
		std::memcpy(from, wire, kWireAddressSize);
		if (!d.bytes.empty()) std::memcpy(data, d.bytes.data(), d.bytes.size());
		r->addr = GekkoNetAddress{from, static_cast<unsigned int>(kWireAddressSize)};
		r->data_len = static_cast<unsigned int>(d.bytes.size());
		r->data = data;
		return r;
	}

	static void freeData(void* p) { std::free(p); }

	static GekkoNetAdapter* adapters() noexcept
	{
		static GekkoNetAdapter table[kSlots] = {
			{&sendFrom<0>, &receiveAt<0>, &freeData},
			{&sendFrom<1>, &receiveAt<1>, &freeData},
			{&sendFrom<2>, &receiveAt<2>, &freeData},
			{&sendFrom<3>, &receiveAt<3>, &freeData},
		};
		return table;
	}

	DatagramEndpoint* m_net = nullptr;
	int m_slot = 0;
	std::uint8_t m_wire[4][kWireAddressSize] = {};
	std::array<GekkoNetAddress, 4> m_remotes{};
	std::vector<GekkoNetResult*> m_handedOut;
};

} // namespace mitiru::network::rollback

#endif // MITIRU_HAS_GEKKONET
