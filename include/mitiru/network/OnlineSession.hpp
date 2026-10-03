#pragma once

/// @file OnlineSession.hpp
/// @brief オンライン協力プレイ 1 人分: UDP の口を開き、待合室 (NetLobby) で席と決まりを受け、RollbackPeer で進める
///
/// 進め方は RollbackPeer と同じで、ゲーム DLL は回線を知らない。始まる前 (待合室の間) は game を進めない。
/// 全員が on_init 直後の同じ GameMemory から始めることを、待合室の GameFingerprint で確かめてから進める。
/// 相手より先へ進みすぎた時は 1 フレーム待つ (GekkoNet の frames ahead が 1 を超えたら、1 つおきに進めない)。

#if defined(MITIRU_HAS_GEKKONET)

#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/module/SideStateHost.hpp>
#include <mitiru/network/DatagramEndpoint.hpp>
#include <mitiru/network/NetLobby.hpp>
#include <mitiru/network/RollbackSession.hpp>
#include <mitiru/network/RollbackUdp.hpp>

namespace mitiru::network::rollback
{

struct OnlineOptions
{
	bool hosting = false;
	ListenScope scope = ListenScope::Loopback;   ///< 外の相手を待つ host だけ Network にする
	std::uint16_t port = 0;                      ///< 開く port (参加者は 0 = OS が選ぶ)
	NetAddress hostAddress{};                    ///< 参加者が Hello を送る先
	int players = 2;                             ///< host が決める人数 (2..4)
	int inputDelay = 2;
	int predictionWindow = 8;
	unsigned disconnectTimeoutMs = 5000;
	std::uint64_t rngSeed = 1;
	float dt = 1.0f / 60.0f;
	std::uint16_t logicalW = 1280;
	std::uint16_t logicalH = 720;
	LinkConditions link{};
	int slot = 0;                                ///< 同じプロセスで同時に使う session ごとに別の番号 (RollbackUdp)
	bool ready = true;                           ///< 待合室に入った時の自分の準備
};

enum class OnlinePhase : std::uint8_t
{
	Lobby,
	Running,
	Failed,
};

class OnlineSession
{
public:
	/// @brief 口を開いて待合室に入る。memory は on_init の直後 (まだ 1 度も進めていない) であること
	bool open(const OnlineOptions& o, const module::ModuleApi& api, void* memory, module::SideStateHost* sides,
		std::uint64_t nowMs, std::string* error)
	{
		m_opt = o;
		m_api = api;
		m_memory = memory;
		m_sides = sides;
		if (!m_net.open(o.scope, o.hosting ? o.port : 0, error)) return false;
		m_net.setConditions(o.link);
		m_udp = std::make_unique<RollbackUdp>(m_net, o.slot);
		const GameFingerprint game = fingerprint();
		if (o.hosting)
		{
			LobbyStart params;
			params.players = o.players;
			params.inputDelay = o.inputDelay;
			params.predictionWindow = o.predictionWindow;
			params.rngSeed = o.rngSeed;
			params.logicalW = o.logicalW;
			params.logicalH = o.logicalH;
			m_lobby.host(m_net, game, params, nowMs, o.ready);
		}
		else m_lobby.join(m_net, o.hostAddress, game, nowMs, o.ready);
		m_phase = OnlinePhase::Lobby;
		return true;
	}

	void setCalls(const RollbackCalls& calls) noexcept
	{
		m_calls = calls;
		if (m_peer) m_peer->setCalls(calls);
	}

	void setReady(bool ready) { m_lobby.setReady(ready); }

	/// @brief 1 フレーム分。待合室なら packet を処理し、始まっていれば自分の入力を渡して進める
	void tick(std::uint64_t nowMs, const PadInput& local)
	{
		m_net.pump(nowMs);
		if (m_phase == OnlinePhase::Lobby) tickLobby(nowMs);
		if (m_phase != OnlinePhase::Running) return;
		// host は遅れて届いた待合室の ping に Start を返す。参加者の待合室はもう要らない
		if (m_lobby.hosting()) m_lobby.update(nowMs);
		else m_net.inbox(kChannelLobby).clear();
		if (shouldWait())
		{
			m_peer->pollNetwork();
			return;
		}
		m_peer->tick(local);
		if (m_peer->error() != nullptr) fail(std::string("対戦を続けられない: ") + m_peer->error());
	}

	/// @brief 前に取ってから確定した進行があれば、その intent を out へ写して true
	bool takeConfirmedIntents(module::FrameIntents& out) { return m_peer && m_peer->takeConfirmedIntents(out); }

	[[nodiscard]] OnlinePhase phase() const noexcept { return m_phase; }
	[[nodiscard]] const NetLobby& lobby() const noexcept { return m_lobby; }
	[[nodiscard]] const RollbackPeer* peer() const noexcept { return m_peer.get(); }
	[[nodiscard]] const DatagramEndpoint& endpoint() const noexcept { return m_net; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] int localPlayer() const noexcept { return m_lobby.localSeat(); }
	[[nodiscard]] int waitedTicks() const noexcept { return m_waited; }

	/// @brief 最初の食い違いの説明 (無ければ空)。相手の側の同じ説明と並べると、どちらが食い違ったか分かる
	[[nodiscard]] std::string desyncReport() const
	{
		if (!m_peer || m_peer->stats().desyncs == 0) return {};
		const DesyncInfo& d = m_peer->stats().firstDesync;
		char line[256];
		std::snprintf(line, sizeof(line), "frame %d で player %d と状態が食い違った (checksum 自分 %08x / 相手 %08x)",
			d.frame, d.remotePlayer, d.local, d.remote);
		std::string out = line;
		if (const FrameChecksum* c = m_peer->checksumOf(d.frame))
		{
			std::snprintf(line, sizeof(line), "\n  自分のその frame の内訳: GameMemory %08x / 窓口 %08x", c->memory, c->side);
			out += line;
		}
		out += "\n  相手の側の内訳と比べ、食い違った方 (GameMemory か窓口) の外に状態が残っていないか、"
		       "入力以外 (時刻・乱数・ポインタ値) を読んでいないかを確かめる";
		return out;
	}

private:
	[[nodiscard]] GameFingerprint fingerprint()
	{
		GameFingerprint g;
		g.abi = module::kWireApiVersion;
		g.memorySize = m_api.memorySize;
		if (m_memory != nullptr) g.memoryChecksum = stateChecksum(m_memory, m_api.memorySize);
		std::vector<std::uint8_t> image;
		std::string why;
		if (m_sides != nullptr && !m_sides->empty() && m_sides->capture(m_memory, true, image, &why))
		{
			g.sideChecksum = stateChecksum(image.data(), image.size());
		}
		return g;
	}

	void tickLobby(std::uint64_t nowMs)
	{
		m_lobby.update(nowMs);
		m_net.inbox(kChannelRollback).clear();   // 先に始めた相手の同期の packet。始めてから GekkoNet が送り直す
		if (m_lobby.phase() == LobbyPhase::Failed) fail(m_lobby.error());
		else if (m_lobby.phase() == LobbyPhase::Started) startMatch();
	}

	void startMatch()
	{
		const LobbyStart& s = m_lobby.start();
		m_udp->setRemotes(s.addrs, s.players);
		RollbackConfig cfg;
		cfg.numPlayers = s.players;
		cfg.localPlayer = s.seat;
		cfg.inputDelay = s.inputDelay;
		cfg.predictionWindow = s.predictionWindow;
		cfg.disconnectTimeoutMs = m_opt.disconnectTimeoutMs;
		cfg.snapshot = SnapshotBase{m_opt.dt, s.logicalW, s.logicalH, s.rngSeed};
		const auto& remotes = m_udp->remotes();
		m_peer = std::make_unique<RollbackPeer>(m_api, m_memory, cfg, m_udp->adapter(),
			std::span<const GekkoNetAddress>(remotes.data(), static_cast<std::size_t>(s.players)), m_sides);
		if (m_peer->error() != nullptr)
		{
			fail(std::string("対戦に載せられない: ") + m_peer->error());
			return;
		}
		m_peer->setCalls(m_calls);
		m_phase = OnlinePhase::Running;
	}

	[[nodiscard]] bool shouldWait()
	{
		if (m_peer->framesAhead() < 1.0f || m_waitedLast)
		{
			m_waitedLast = false;
			return false;
		}
		m_waitedLast = true;
		++m_waited;
		return true;
	}

	void fail(std::string why)
	{
		m_phase = OnlinePhase::Failed;
		m_error = std::move(why);
	}

	OnlineOptions m_opt{};
	module::ModuleApi m_api{};
	void* m_memory = nullptr;
	module::SideStateHost* m_sides = nullptr;
	DatagramEndpoint m_net;
	std::unique_ptr<RollbackUdp> m_udp;
	NetLobby m_lobby;
	std::unique_ptr<RollbackPeer> m_peer;
	RollbackCalls m_calls{};
	OnlinePhase m_phase = OnlinePhase::Lobby;
	std::string m_error;
	bool m_waitedLast = false;
	int m_waited = 0;
};

} // namespace mitiru::network::rollback

#endif // MITIRU_HAS_GEKKONET
