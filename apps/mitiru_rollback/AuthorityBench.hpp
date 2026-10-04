#pragma once

// mitiru_rollback --authority: 同じゲーム DLL を host と参加者として読み、127.0.0.1 の UDP の上で host 権威の対戦をさせる。
// 回線の量 (毎秒の byte 数) を測り、参加者が補間で作ったフレームが host のフレームと一致するかを確かめる。
// 時計はこのツールが進めるミリ秒で、壁時計は待たない。待ち受けは 127.0.0.1 だけ。

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include <mitiru/network/AuthorityClient.hpp>
#include <mitiru/network/AuthorityHost.hpp>
#include <mitiru/network/DatagramEndpoint.hpp>

namespace authority_bench
{

namespace net = mitiru::network;
namespace auth = mitiru::network::authority;

/// IPv4 と UDP の頭の大きさ。回線に乗る量はこれを packet ごとに足したもの
inline constexpr double kIpUdpHeader = 28.0;

struct Settings
{
	int frames = 600;
	int rate = 20;
	net::LinkConditions link{};
};

struct Endpoints
{
	net::DatagramEndpoint host;
	net::DatagramEndpoint client;
};

inline bool openEndpoints(Endpoints& e, const net::LinkConditions& link)
{
	std::string error;
	if (!e.host.open(net::ListenScope::Loopback, 0, &error) || !e.client.open(net::ListenScope::Loopback, 0, &error))
	{
		std::fprintf(stderr, "mitiru_rollback: 127.0.0.1 で通信の口を開けません (%s)。\n", error.c_str());
		return false;
	}
	net::LinkConditions l = link;
	e.host.setConditions(l);
	l.seed = link.seed + 1;
	e.client.setConditions(l);
	return true;
}

inline void printDirection(const char* name, std::uint64_t bytes, std::uint64_t packets, double seconds)
{
	std::printf("%s  %.0f B/s (UDP の中身)  %.0f B/s (IP と UDP の頭を含む)  %.1f packets/s\n", name,
		static_cast<double>(bytes) / seconds, (static_cast<double>(bytes) + kIpUdpHeader * static_cast<double>(packets)) / seconds,
		static_cast<double>(packets) / seconds);
}

/// @param bot   (seed, player, frame) → その人の入力
/// @return 一致なら 0、食い違いか止まったら 1
template <class Game, class Bot>
int run(const Settings& s, Game& hostGame, Game& clientGame, std::uint32_t seed, Bot bot)
{
	Endpoints e;
	if (!openEndpoints(e, s.link)) return 2;
	auth::AuthorityConfig cfg;
	cfg.snapshotEvery = 60 / s.rate;
	cfg.addrs = {e.host.localAddress(), e.client.localAddress()};
	auth::AuthorityHost host(*hostGame.api, hostGame.memory, cfg, e.host, &hostGame.sides, 0);
	cfg.localPlayer = 1;
	auth::AuthorityClient client(*clientGame.api, clientGame.memory, cfg, e.client, &clientGame.sides, 0);
	const int watch = s.frames - 60;
	host.watchFrame(watch);
	client.watchFrame(watch);
	for (int t = 0; t < s.frames + 600 && (host.stats().frame < s.frames || client.stats().frame <= watch); ++t)
	{
		const std::uint64_t now = static_cast<std::uint64_t>(t) * 1000 / 60;
		e.host.pump(now);
		host.tick(now, t < s.frames ? bot(seed, 0, t) : auth::PadInput{});
		e.client.pump(now);
		client.tick(now, t < s.frames ? bot(seed, 1, t) : auth::PadInput{});
		if (!host.error().empty() || !client.error().empty()) break;
		std::this_thread::yield();
	}
	if (!host.error().empty() || !client.error().empty())
	{
		std::fprintf(stderr, "mitiru_rollback: 対戦が止まりました (%s%s)。\n", host.error().c_str(), client.error().c_str());
		return 1;
	}
	const auto& h = host.stats();
	const auto& c = client.stats();
	const double seconds = static_cast<double>(h.frame) / 60.0;
	std::printf("authority %d Hz  frames %d  GameMemory %u B  link %u ms ± %u ms, loss %.1f %%\n", 60 / cfg.snapshotEvery, h.frame,
		hostGame.api->memorySize, s.link.latencyMs, s.link.jitterMs, s.link.lossPercent);
	printDirection("host → 参加者", h.bytesSent, h.packetsSent, seconds);
	printDirection("参加者 → host", c.bytesSent, c.packetsSent, seconds);
	std::printf("snapshots %d (丸ごと %d)  参加者: steps %d  jumps %d  stalls %d  drifts %d  dropped %d  ping %u ms\n", h.snapshots,
		h.fullSnapshots, c.steps, c.jumps, c.stalls, c.drifts, c.dropped, client.pingMs());
	const bool same = host.watched() && client.watched() && host.watchedChecksum() == client.watchedChecksum();
	std::printf("frame %d checksum host %08x  参加者 %08x\n", watch, host.watchedChecksum(), client.watchedChecksum());
	const bool ok = same && c.drifts == 0;
	std::printf("%s\n", ok ? "OK" : "DESYNC (参加者が作ったフレームが host と違う。GameMemory の外に状態があるか、入力以外で結果が変わる)");
	return ok ? 0 : 1;
}

} // namespace authority_bench
