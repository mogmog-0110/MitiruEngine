#pragma once

/// @file HostOnline.hpp
/// @brief mitiru_host のオンライン協力プレイ (--net / --net-host / --net-join)。2..4 人をロールバックでつなぐ。
/// @details 進め方は network/OnlineSession.hpp。host は on_update を 1 回呼ぶ代わりに session を 1 フレーム進め
///          (EngineConfig::moduleFrameDriver)、確定した進行の intent だけを音・HUD・セーブへ流す。待合室の画面は
///          engine 同梱の assets/ui/net_lobby.rml で、操作は "net.*" の名前で host が受けてゲームへは渡さない
///          (HostOnlineUi.hpp)。待ち受けは既定で 127.0.0.1 だけ。外へ開くのは、利用者が自分で host を始めた時
///          (--net-host <port> か画面の「部屋を作る」) だけ。
///          ゲームも FrameIntents::netRequest (ABI v50) で部屋を作る・参加する・抜けるを頼め、描画は Screen::netView で
///          自分の席と ping を読める。ゲームが頼んで始めた対戦は、全員が on_init の直後の GameMemory から始める。

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <mitiru/core/Config.hpp>
#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/network/NetAddress.hpp>

#if defined(MITIRU_HAS_GEKKONET)
#include <mitiru/network/OnlineSession.hpp>
#endif

namespace mitiru::host
{

struct OnlineArgs
{
	bool chooser = false;      ///< --net: 待合室の画面で部屋を作るか参加するかを選ぶ
	std::string host;          ///< --net-host [ip:]port
	std::string join;          ///< --net-join ip:port か参加コード
	int players = 2;           ///< --net-players (host だけ)
	int delay = 2;             ///< --net-delay (入力遅延のフレーム数)
	std::string sim;           ///< --net-sim latency,jitter,loss (送る packet に足す)
	int stopFrame = 0;         ///< --net-frames N: frame N が確定したら checksum を出して終わる
	std::string error;

	[[nodiscard]] bool any() const noexcept { return chooser || !host.empty() || !join.empty(); }
};

/// @brief parseArgs の loop から呼ぶ。--net 系の引数なら読んで true (値の誤りは out.error)
inline bool parseOnlineArg(std::string_view a, int argc, char* argv[], int& i, OnlineArgs& out)
{
	if (a == "--net") { out.chooser = true; return true; }
	const bool text = a == "--net-host" || a == "--net-join" || a == "--net-sim";
	const bool number = a == "--net-players" || a == "--net-delay" || a == "--net-frames";
	if (!text && !number) return false;
	if (i + 1 >= argc)
	{
		out.error = std::string(a) + " に値が要る";
		return true;
	}
	const std::string value = argv[++i];
	if (a == "--net-host") out.host = value;
	else if (a == "--net-join") out.join = value;
	else if (a == "--net-sim") out.sim = value;
	else
	{
		int n = 0;
		std::size_t used = 0;
		try { n = std::stoi(value, &used); }
		catch (...) { used = 0; }
		if (used == 0 || used != value.size())
		{
			out.error = std::string(a) + " は整数: " + value;
			return true;
		}
		(a == "--net-players" ? out.players : a == "--net-delay" ? out.delay : out.stopFrame) = n;
	}
	return true;
}

#if defined(MITIRU_HAS_GEKKONET)

using network::ListenScope;
using network::rollback::OnlinePhase;
using network::rollback::OnlineSession;

class HostOnline
{
public:
	static constexpr std::string_view kLobbyPage = "mitiru:net_lobby.rml";
	static constexpr std::uint16_t kDefaultPort = 47100;
	/// 終わりの checksum を出した後も回線を回す時間。先に止まると、こちらの入力を待つ相手が終われない
	static constexpr std::uint64_t kLingerMs = 2000;

	/// @brief Engine を作る前に呼ぶ。引数が誤っていれば false と理由。gameRequests はゲームの netRequest で
	///        部屋を作れる実行か (録画と再生の間は、手元の入力だけで結果が決まらないので受けない)
	bool configure(const OnlineArgs& args, bool interactive, bool gameRequests, EngineConfig& cfg, std::string& error)
	{
		m_args = args;
		m_interactive = interactive;
		m_gameRequests = gameRequests;
		m_clock0 = std::chrono::steady_clock::now();
		if (!args.any()) return true;
		if (!validate(interactive, error)) return false;
		m_ready = !interactive;
		m_players = args.players;
		m_holdUntilOnline = true;
		if (interactive && cfg.uiDocument.empty()) cfg.uiDocument = std::string(kLobbyPage);
		install(cfg);
		return true;
	}

	void attach(Engine& engine) noexcept { m_engine = &engine; }

	/// @brief onFrameStart から毎フレーム呼ぶ
	void onFrame(Engine& engine)
	{
		if (!active()) return;
		if (!m_uiChecked) checkUi(engine);
		reportChanges();
		engine.setNetView(netView());
		if (++m_uiTick % 10 == 1) pushUi(engine);
		finishWhenDone(engine);
	}

	/// @brief on_update の後に呼ぶ。ゲームの netRequest (ABI v50) を受ける
	void onModuleFrame(const module::FrameIntents& intents);

	/// @brief --net-frames で終えた時の結果 (0 = 確定した checksum を出せて食い違いも無い)
	[[nodiscard]] int exitCode() const noexcept { return m_exit; }

	[[nodiscard]] bool active() const noexcept { return m_args.any() || m_installed; }

private:
	struct Request
	{
		bool hosting = false;
		ListenScope scope = ListenScope::Loopback;
		network::NetAddress addr{};
	};

	[[nodiscard]] std::uint64_t nowMs() const
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_clock0).count());
	}

	bool validate(bool interactive, std::string& error)
	{
		const OnlineArgs& a = m_args;
		if (!a.error.empty()) error = a.error;
		else if (!a.host.empty() && !a.join.empty()) error = "--net-host と --net-join は一緒に使えない";
		else if (a.players < 2 || a.players > 4) error = "--net-players は 2..4";
		else if (a.delay < 0 || a.delay > 15) error = "--net-delay は 0..15";
		else if (a.stopFrame < 0 || a.stopFrame > 10000000) error = "--net-frames は 0..10000000";
		else if (a.chooser && a.host.empty() && a.join.empty() && !interactive)
			error = "--net は画面で選ぶ。画面の無い実行では --net-host か --net-join を使う";
		if (error.empty() && !a.sim.empty())
		{
			if (auto c = network::parseLinkConditions(a.sim)) m_link = *c;
			else error = "--net-sim は latency,jitter,loss (ミリ秒, ミリ秒, %): " + a.sim;
		}
		if (error.empty() && !a.host.empty()) error = requestHost(a.host);
		if (error.empty() && !a.join.empty()) error = requestJoin(a.join);
		return error.empty();
	}

	/// "47100" は全部の口で待つ (外の相手を待つ)。"127.0.0.1:47100" は同じ PC からだけ受ける
	std::string requestHost(std::string_view text)
	{
		const auto addr = network::parseAddress(text);
		if (!addr) return "--net-host は port か ip:port: " + std::string(text);
		m_request = Request{true, addr->isLoopback() ? ListenScope::Loopback : ListenScope::Network, *addr};
		return {};
	}

	std::string requestJoin(std::string_view text)
	{
		const auto addr = network::parseAddressOrCode(text);
		if (!addr) return "参加先は ip:port か参加コード: " + std::string(text);
		m_request = Request{false, addr->isLoopback() ? ListenScope::Loopback : ListenScope::Network, *addr};
		return {};
	}

	/// 進め方と待合室の操作を engine に任せてもらう。起動の引数か、ゲームが初めて頼んだ時に 1 度だけ
	void install(EngineConfig& cfg)
	{
		if (m_installed) return;
		m_installed = true;
		auto previous = cfg.uiActionFilter;
		cfg.uiActionFilter = [this, previous](std::string_view name, std::string_view payload) {
			return onUiAction(name, payload) || (previous && previous(name, payload));
		};
		cfg.moduleFrameDriver = [this](const module::InputSnapshot& live, module::FrameIntents& out) { return drive(live, out); };
	}

	/// ゲームが頼んで始める対戦は、全員の GameMemory を on_init の直後にそろえてから待合室に入る
	void restartGameMemory()
	{
		const module::ModuleApi& api = m_engine->moduleApi();
		void* memory = m_engine->moduleMemory();
		if (api.on_init == nullptr || memory == nullptr || api.memorySize == 0) return;
		std::memset(memory, 0, api.memorySize);
		(void)m_engine->guardModuleCallback("on_init", [&] { api.on_init(memory); });
	}

	[[nodiscard]] module::NetView netView() const;

	static bool updateThunk(void* ctx, const module::InputSnapshot* in, module::FrameIntents* out)
	{
		return static_cast<Engine*>(ctx)->callModuleUpdate(in, out);
	}

	static bool rebuildThunk(void* ctx)
	{
		auto* engine = static_cast<Engine*>(ctx);
		const module::ModuleApi& api = engine->moduleApi();
		if (api.on_rebuild == nullptr) return true;
		return engine->guardModuleCallback("on_rebuild", [&] { api.on_rebuild(engine->moduleMemory(), module::kModuleRebuildRestore); });
	}

	/// @brief moduleFrameDriver の中身。待合室の間は game を進めない
	ModuleFrameDrive drive(const module::InputSnapshot& live, module::FrameIntents& out)
	{
		if (m_request && m_engine != nullptr)
		{
			if (m_restartOnStart) restartGameMemory();
			startSession(*m_request, live);
		}
		m_request.reset();
		m_restartOnStart = false;
		if (!m_session) return m_holdUntilOnline ? ModuleFrameDrive::Idle : ModuleFrameDrive::Local;
		if (m_session->phase() == OnlinePhase::Failed) return ModuleFrameDrive::Idle;
		m_session->tick(nowMs(), network::rollback::sampleLocal(live));
		if (m_engine != nullptr && m_engine->moduleFaulted()) return ModuleFrameDrive::Faulted;
		return m_session->takeConfirmedIntents(out) ? ModuleFrameDrive::Advanced : ModuleFrameDrive::Idle;
	}

	void startSession(const Request& r, const module::InputSnapshot& live)
	{
		network::rollback::OnlineOptions o;
		o.hosting = r.hosting;
		o.scope = r.scope;
		o.port = r.hosting ? r.addr.port : 0;
		o.hostAddress = r.addr;
		o.players = m_players;
		o.inputDelay = m_args.delay;
		o.link = m_link;
		o.ready = m_ready;
		o.rngSeed = m_engine->config().randomSeed;
		o.logicalW = live.logicalW != 0 ? live.logicalW : std::uint16_t{1280};
		o.logicalH = live.logicalH != 0 ? live.logicalH : std::uint16_t{720};
		auto session = std::make_unique<OnlineSession>();
		std::string error;
		if (!session->open(o, m_engine->moduleApi(), m_engine->moduleMemory(), &m_engine->moduleSideState(), nowMs(), &error))
		{
			m_error = error;
			m_startFailed = true;
			std::fprintf(stderr, "[net] 始められない: %s\n", error.c_str());
			return;
		}
		session->setCalls(network::rollback::RollbackCalls{m_engine, &updateThunk, &rebuildThunk});
		m_session = std::move(session);
		m_error.clear();
		m_startFailed = false;
		m_hostShown = r.hosting ? shownHostAddress(r) : r.addr;
		announceLobby(r);
	}

	/// 参加者に伝える住所。全部の口で待つ時は、外へ出る口の住所を推し量る
	[[nodiscard]] network::NetAddress shownHostAddress(const Request& r) const
	{
		network::NetAddress a = m_session->endpoint().localAddress();
		if (a.ip == 0) a.ip = network::guessLanAddressV4();
		if (a.ip == 0) a.ip = r.addr.ip;
		return a;
	}

	void announceLobby(const Request& r) const
	{
		if (r.hosting)
		{
			std::fprintf(stderr, "[net] 待合室を開いた: %s (参加コード %s) %d 人%s\n", network::toString(m_hostShown).c_str(),
				network::encodeJoinCode(m_hostShown).c_str(), m_players,
				r.scope == ListenScope::Network ? "。外の相手を待つので、ファイアウォールの確認が出たら許可する" : "");
		}
		else std::fprintf(stderr, "[net] %s の待合室へ参加を申し込んだ\n", network::toString(r.addr).c_str());
	}

	/// 画面 (RmlUi) が出せない実行では「準備できた」を押せないので、最初から準備ができていることにする
	void checkUi(Engine& engine)
	{
		m_uiChecked = true;
		if (!m_interactive || engine.uiHost().active()) return;
		m_ready = true;
		if (m_session) m_session->setReady(true);
		if (!m_request && !m_session)
		{
			std::fprintf(stderr, "[net] 待合室の画面を出せない (UI が無いビルドか DX12 でない)。--net-host か --net-join を使う\n");
		}
	}

	void reportChanges()
	{
		if (!m_session) return;
		const OnlinePhase phase = m_session->phase();
		if (phase != m_reportedPhase)
		{
			m_reportedPhase = phase;
			if (phase == OnlinePhase::Failed) std::fprintf(stderr, "[net] 止まった: %s\n", m_session->error().c_str());
			if (phase == OnlinePhase::Running)
			{
				const auto& s = m_session->lobby().start();
				std::fprintf(stderr, "[net] 始まった: 自分は %dP / %d 人、入力遅延 %d フレーム\n", s.seat + 1, s.players, s.inputDelay);
			}
		}
		const auto* peer = m_session->peer();
		if (peer == nullptr) return;
		if (peer->stats().desyncs > 0 && !m_desyncReported)
		{
			m_desyncReported = true;
			std::fprintf(stderr, "[net] 食い違い: %s\n", m_session->desyncReport().c_str());
		}
		if (peer->stats().disconnects > m_reportedDisconnects)
		{
			m_reportedDisconnects = peer->stats().disconnects;
			std::fprintf(stderr, "[net] 相手が切れた (%d 人)。その人の入力は全員で決めたフレームから空になる\n", m_reportedDisconnects);
		}
	}

	void finishWhenDone(Engine& engine)
	{
		const bool failed = m_startFailed || (m_session && m_session->phase() == OnlinePhase::Failed);
		if (failed && !m_interactive && m_exit == 0)
		{
			m_exit = 1;
			engine.requestStop();
			return;
		}
		if (m_args.stopFrame <= 0 || !m_session || m_session->peer() == nullptr) return;
		if (m_stopAtMs != 0)
		{
			if (nowMs() >= m_stopAtMs) engine.requestStop();
			return;
		}
		const auto& peer = *m_session->peer();
		if (peer.stats().frame < m_args.stopFrame + peer.config().predictionWindow + 2) return;
		printFinalChecksum(peer);
		m_stopAtMs = nowMs() + kLingerMs;
	}

	void printFinalChecksum(const network::rollback::RollbackPeer& peer)
	{
		const auto& st = peer.stats();
		const network::rollback::FrameChecksum* c = peer.checksumOf(m_args.stopFrame);
		if (c != nullptr)
		{
			std::fprintf(stderr, "[net] frame %d checksum %08x (GameMemory %08x / 窓口 %08x)\n", c->frame, c->total, c->memory, c->side);
		}
		else std::fprintf(stderr, "[net] frame %d の checksum が残っていない\n", m_args.stopFrame);
		std::fprintf(stderr, "[net] %dP / %d 人  rollbacks %d  resimulated %d  advances %d  desyncs %d  waited %d  dropped %llu\n",
			peer.config().localPlayer + 1, peer.config().numPlayers, st.loads, st.resimulated, st.advances, st.desyncs,
			m_session->waitedTicks(), static_cast<unsigned long long>(m_session->endpoint().droppedPackets()));
		m_exit = (c != nullptr && st.desyncs == 0) ? 0 : 1;
	}

	bool onUiAction(std::string_view name, std::string_view payload);
	void pushUi(Engine& engine);
	void pushSeat(ui_rml::RmlUiHost& ui, int i) const;

	OnlineArgs m_args;
	network::LinkConditions m_link{};
	bool m_interactive = false;
	bool m_gameRequests = false;    ///< ゲームの netRequest を受ける実行か
	bool m_installed = false;       ///< 進め方を引き受けたか (起動の引数か、ゲームの依頼で)
	bool m_holdUntilOnline = false; ///< 対戦が始まるまで game を進めない (--net 系で起動した時)
	bool m_restartOnStart = false;  ///< 次に始める session の前に GameMemory を on_init の直後に戻す
	bool m_chooserOpen = false;     ///< ゲームが待合室の画面を頼んだ (kNetRequestOpen)
	bool m_ready = true;
	int m_players = 2;
	std::string m_typedAddress;
	std::optional<Request> m_request;
	std::unique_ptr<OnlineSession> m_session;
	network::NetAddress m_hostShown{};
	std::string m_error;
	Engine* m_engine = nullptr;
	std::chrono::steady_clock::time_point m_clock0{};
	OnlinePhase m_reportedPhase = OnlinePhase::Lobby;
	bool m_desyncReported = false;
	int m_reportedDisconnects = 0;
	bool m_overlayOpen = false;
	bool m_uiChecked = false;
	bool m_startFailed = false;   ///< 口を開けなかった (port が使用中など)。人が見ていない実行はここで止める
	int m_uiTick = 0;
	std::uint64_t m_stopAtMs = 0;
	int m_exit = 0;
};

#else

/// GekkoNet 無しのビルド。--net 系を渡された時だけ理由を返して止める。ゲームの netRequest は 1 度だけ知らせて捨てる
class HostOnline
{
public:
	bool configure(const OnlineArgs& args, bool, bool, EngineConfig&, std::string& error)
	{
		if (!args.any()) return true;
		error = "この mitiru_host はオンライン協力プレイ無しでビルドした (cmake -DMITIRU_WITH_GEKKONET=ON で入る)";
		return false;
	}
	void attach(Engine&) noexcept {}
	void onFrame(Engine&) {}
	void onModuleFrame(const module::FrameIntents& intents)
	{
		if (intents.netRequest.kind == module::kNetRequestNone) return;
		debug::warnOnce("net.request.unbuilt",
			"hud.net*: この mitiru_host はオンライン協力プレイ無しでビルドした (cmake -DMITIRU_WITH_GEKKONET=ON で入る)");
	}
	[[nodiscard]] int exitCode() const noexcept { return 0; }
	[[nodiscard]] bool active() const noexcept { return false; }
};

#endif // MITIRU_HAS_GEKKONET

} // namespace mitiru::host

#if defined(MITIRU_HAS_GEKKONET)
#include "HostOnlineUi.hpp"
#endif
