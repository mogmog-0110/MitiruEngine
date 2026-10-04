#pragma once

/// @file HostOnline.hpp
/// @brief mitiru_host のオンライン協力プレイ (--net / --net-host / --net-join)。2..4 人をロールバックか host 権威 (--net-mode) でつなぐ。
/// @details 進め方は network/OnlineSession.hpp。host は on_update を 1 回呼ぶ代わりに session を 1 フレーム進め
///          (EngineConfig::moduleFrameDriver)、確定した進行の intent だけを音・HUD・セーブへ流す。待合室の画面は
///          engine 同梱の assets/ui/net_lobby.rml で、操作は "net.*" の名前で host が受けてゲームへは渡さない
///          (HostOnlineUi.hpp)。待ち受けは既定で 127.0.0.1 だけ。外へ開くのは、利用者が自分で host を始めた時
///          (--net-host <port> か画面の「部屋を作る」) だけ。
///          ゲームも FrameIntents::netRequest (ABI v50) で部屋を作る・参加する・抜けるを頼め、描画は Screen::netView で
///          自分の席と ping を読める。ゲームが頼んで始めた対戦は、全員が on_init の直後の GameMemory から始める。

#include <algorithm>
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
#include <mitiru/debug/ConsoleOut.hpp>
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
	std::string mode;          ///< --net-mode rollback|authority (部屋を作る側だけ。参加者は host に従う)
	int rate = 20;             ///< --net-rate: host 権威で状態を配る回数 (毎秒、60 の約数)
	int stopFrame = 0;         ///< --net-frames N: frame N が確定したら checksum を出して終わる
	std::string error;

	[[nodiscard]] bool any() const noexcept { return chooser || !host.empty() || !join.empty(); }
};

/// @brief parseArgs の loop から呼ぶ。--net 系の引数なら読んで true (値の誤りは out.error)
inline bool parseOnlineArg(std::string_view a, int argc, char* argv[], int& i, OnlineArgs& out)
{
	if (a == "--net") { out.chooser = true; return true; }
	const bool text = a == "--net-host" || a == "--net-join" || a == "--net-sim" || a == "--net-mode";
	const bool number = a == "--net-players" || a == "--net-delay" || a == "--net-frames" || a == "--net-rate";
	if (!text && !number) return false;
	if (i + 1 >= argc)
	{
		out.error = std::string(a) + " の後に値を書いてください。";
		return true;
	}
	const std::string value = argv[++i];
	if (a == "--net-host") out.host = value;
	else if (a == "--net-join") out.join = value;
	else if (a == "--net-sim") out.sim = value;
	else if (a == "--net-mode") out.mode = value;
	else
	{
		int n = 0;
		std::size_t used = 0;
		try { n = std::stoi(value, &used); }
		catch (...) { used = 0; }
		if (used == 0 || used != value.size())
		{
			out.error = std::string(a) + " には整数を書いてください (" + value + " は整数ではありません)。";
			return true;
		}
		(a == "--net-players" ? out.players : a == "--net-delay" ? out.delay : a == "--net-rate" ? out.rate : out.stopFrame) = n;
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
		// 方式はゲームの依頼 (netRequest) で部屋を作る時にも使うので、--net 系が無くても読む
		if (!readMode(error)) return false;
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
		engine.setNetModeView(netModeView());
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

	bool readMode(std::string& error)
	{
		const OnlineArgs& a = m_args;
		if (!a.mode.empty() && a.mode != "rollback" && a.mode != "authority") error = "--net-mode には rollback か authority を書いてください (" + a.mode + " は使えません)。";
		else if (a.rate < 1 || a.rate > 60 || 60 % a.rate != 0) error = "--net-rate には 60 の約数 (10, 12, 15, 20, 30, 60 など) を書いてください。";
		else if (!a.mode.empty() && !a.join.empty()) error = "--net-mode は部屋を作る側だけが決めます。参加する側では外してください。";
		m_argMode = a.mode == "authority" ? network::NetMode::Authority : network::NetMode::Rollback;
		m_argSnapshotEvery = 60 / std::clamp(a.rate, 1, 60);
		m_mode = m_argMode;
		m_snapshotEvery = m_argSnapshotEvery;
		return error.empty();
	}

	bool validate(bool interactive, std::string& error)
	{
		const OnlineArgs& a = m_args;
		if (!a.error.empty()) error = a.error;
		else if (!a.host.empty() && !a.join.empty()) error = "--net-host と --net-join は一緒に使えません。どちらか一方にしてください。";
		else if (a.players < 2 || a.players > 4) error = "--net-players には 2 から 4 を書いてください。";
		else if (a.delay < 0 || a.delay > 15) error = "--net-delay には 0 から 15 を書いてください。";
		else if (a.stopFrame < 0 || a.stopFrame > 10000000) error = "--net-frames には 0 から 10000000 を書いてください。";
		else if (a.chooser && a.host.empty() && a.join.empty() && !interactive)
			error = "--net は待合室の画面で選ぶので、画面の無い実行では使えません。--net-host か --net-join を使ってください。";
		if (error.empty() && !a.sim.empty())
		{
			if (auto c = network::parseLinkConditions(a.sim)) m_link = *c;
			else error = "--net-sim " + a.sim + " を読めません。latency,jitter,loss (ミリ秒, ミリ秒, %) の形で書いてください。";
		}
		if (error.empty() && !a.host.empty()) error = requestHost(a.host);
		if (error.empty() && !a.join.empty()) error = requestJoin(a.join);
		return error.empty();
	}

	/// "47100" は全部の口で待つ (外の相手を待つ)。"127.0.0.1:47100" は同じ PC からだけ受ける
	std::string requestHost(std::string_view text)
	{
		const auto addr = network::parseAddress(text);
		if (!addr) return "--net-host " + std::string(text) + " を読めません。port か ip:port の形で書いてください。";
		m_request = Request{true, addr->isLoopback() ? ListenScope::Loopback : ListenScope::Network, *addr};
		return {};
	}

	std::string requestJoin(std::string_view text)
	{
		const auto addr = network::parseAddressOrCode(text);
		if (!addr) return "参加先 " + std::string(text) + " を読めません。ip:port か参加コードを書いてください。";
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
	[[nodiscard]] module::NetModeView netModeView() const;
	void applyModeRequest(const module::NetModeRequest& m);

	static bool updateThunk(void* ctx, const module::InputSnapshot* in, module::FrameIntents* out)
	{
		return static_cast<Engine*>(ctx)->callModuleUpdate(in, out);
	}

	static bool predictThunk(void* ctx, void* drawMemory, const module::InputSnapshot* local, std::uint8_t player)
	{
		return static_cast<Engine*>(ctx)->callModuleNetPredict(drawMemory, local, player);
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
		if (m_engine != nullptr) m_engine->setModuleDrawMemory(nullptr);
		if (!m_session) return m_holdUntilOnline ? ModuleFrameDrive::Idle : ModuleFrameDrive::Local;
		if (m_session->phase() == OnlinePhase::Failed) return ModuleFrameDrive::Idle;
		m_session->tick(nowMs(), network::rollback::sampleLocal(live));
		// host 権威の参加者は、自分の分を先に進めた写しを描く (シミュレーションの GameMemory は host の状態のまま)
		if (m_engine != nullptr) m_engine->setModuleDrawMemory(m_session->predictedDrawMemory());
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
		o.mode = m_mode;
		o.snapshotEvery = m_snapshotEvery;
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
			console::noticef("オンライン協力プレイを始められませんでした (%s)。", error.c_str());
			return;
		}
		const bool predicts = m_engine->moduleHasNetPredict();
		session->setCalls(network::rollback::RollbackCalls{m_engine, &updateThunk, &rebuildThunk, predicts ? &predictThunk : nullptr});
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
			console::noticef("%d 人用の待合室を開きました。参加する人に参加コード %s (住所 %s) を伝えてください。%s", m_players,
				network::encodeJoinCode(m_hostShown).c_str(), network::toString(m_hostShown).c_str(),
				r.scope == ListenScope::Network ? "外の相手を待つので、ファイアウォールの確認が出たら許可してください。" : "");
		}
		else console::verbosef("%s の待合室へ参加を申し込みました。", network::toString(r.addr).c_str());
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
			console::notice("待合室の画面を出せません (UI の無いビルドか、DX12 で動いていません)。--net-host か --net-join を使ってください。");
		}
	}

	void reportChanges()
	{
		if (!m_session) return;
		const OnlinePhase phase = m_session->phase();
		if (phase != m_reportedPhase)
		{
			m_reportedPhase = phase;
			if (phase == OnlinePhase::Failed) console::noticef("オンライン協力プレイが止まりました (%s)。", m_session->error().c_str());
			if (phase == OnlinePhase::Running) announceStart();
		}
		if (!m_desyncReported && !m_session->desyncReport().empty())
		{
			m_desyncReported = true;
			console::noticef("参加者の間でゲームの状態が食い違いました (%s)。", m_session->desyncReport().c_str());
		}
		const int disconnects = sessionDisconnects();
		if (disconnects > m_reportedDisconnects)
		{
			m_reportedDisconnects = disconnects;
			console::noticef("相手との接続が切れました (%d 人)。その人の入力は%sから空になります。", m_reportedDisconnects,
				m_session->mode() == network::NetMode::Authority ? " host が切れたと決めたフレーム" : "全員で決めたフレーム");
		}
	}

	void announceStart()
	{
		const auto& s = m_session->lobby().start();
		if (s.mode == network::NetMode::Authority)
		{
			console::verbosef("オンライン協力プレイが始まりました。自分は %dP / %d 人で、host 権威 (状態を毎秒 %d 回配る) です。%s",
				s.seat + 1, s.players, 60 / s.snapshotEvery, s.seat == 0 ? "" : "host の状態の間を補間して描きます。");
		}
		else
		{
			console::verbosef("オンライン協力プレイが始まりました。自分は %dP / %d 人で、入力遅延は %d フレームです。", s.seat + 1,
				s.players, s.inputDelay);
		}
		if (m_args.stopFrame > 0) m_session->watchFrame(m_args.stopFrame);
	}

	[[nodiscard]] int sessionDisconnects() const
	{
		if (const auto* peer = m_session->peer()) return peer->stats().disconnects;
		if (const auto* host = m_session->authorityHost()) return host->stats().disconnects;
		return 0;
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
		if (m_args.stopFrame <= 0 || !m_session || m_session->phase() != OnlinePhase::Running) return;
		if (m_stopAtMs != 0)
		{
			if (nowMs() >= m_stopAtMs) engine.requestStop();
			return;
		}
		if (const auto* peer = m_session->peer())
		{
			if (peer->stats().frame < m_args.stopFrame + peer->config().predictionWindow + 2) return;
			printFinalChecksum(*peer);
		}
		else if (!printAuthorityChecksum()) return;
		m_stopAtMs = nowMs() + kLingerMs;
	}

	/// @return frame N を通り過ぎて checksum を出せたら true
	/// @details --net-frames が頼んだ結果なので常に出す。`[net]` の行は tests/e2e/run_online.py が読む
	bool printAuthorityChecksum()
	{
		const auto* host = m_session->authorityHost();
		const auto* client = m_session->authorityClient();
		const int frame = host != nullptr ? host->stats().frame : client != nullptr ? client->stats().frame : -1;
		if (frame <= m_args.stopFrame) return false;
		const bool watched = host != nullptr ? host->watched() : client->watched();
		const std::uint32_t sum = host != nullptr ? host->watchedChecksum() : client->watchedChecksum();
		if (watched) std::fprintf(stderr, "[net] frame %d checksum %08x (GameMemory と窓口)\n", m_args.stopFrame, sum);
		else std::fprintf(stderr, "[net] frame %d は描かずに飛ばしたので checksum がありません\n", m_args.stopFrame);
		const auto& s = m_session->lobby().start();
		const double seconds = static_cast<double>(frame) / 60.0;
		if (host != nullptr)
		{
			const auto& st = host->stats();
			std::fprintf(stderr, "[net] %dP / %d 人  host 権威 %d Hz  snapshots %d  full %d  held %d  skipped %d  down %.0f B/s (1 人あたり)\n",
				s.seat + 1, s.players, 60 / s.snapshotEvery, st.snapshots, st.fullSnapshots, st.heldInputs, st.skippedInputs,
				static_cast<double>(st.bytesSent) / seconds / static_cast<double>((std::max)(1, s.players - 1)));
			m_exit = watched ? 0 : 1;
			return true;
		}
		const auto& st = client->stats();
		std::fprintf(stderr, "[net] %dP / %d 人  host 権威 %d Hz  steps %d  jumps %d  stalls %d  drifts %d  dropped %d  predicted %d  down %.0f B/s  up %.0f B/s  ping %u ms\n",
			s.seat + 1, s.players, 60 / s.snapshotEvery, st.steps, st.jumps, st.stalls, st.drifts, st.dropped, st.predictions,
			static_cast<double>(st.bytesReceived) / seconds, static_cast<double>(st.bytesSent) / seconds, client->pingMs());
		m_exit = watched && st.drifts == 0 ? 0 : 1;
		return true;
	}

	void printFinalChecksum(const network::rollback::RollbackPeer& peer)
	{
		const auto& st = peer.stats();
		const network::rollback::FrameChecksum* c = peer.checksumOf(m_args.stopFrame);
		if (c != nullptr)
		{
			std::fprintf(stderr, "[net] frame %d checksum %08x (GameMemory %08x / 窓口 %08x)\n", c->frame, c->total, c->memory, c->side);
		}
		else std::fprintf(stderr, "[net] frame %d の checksum が残っていません\n", m_args.stopFrame);
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
	network::NetMode m_mode = network::NetMode::Rollback;
	int m_snapshotEvery = 3;
	network::NetMode m_argMode = network::NetMode::Rollback;   ///< 起動の引数 (--net-mode)。ゲームの依頼の 0 はこれに戻す
	int m_argSnapshotEvery = 3;                                ///< 起動の引数 (--net-rate)
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
		if (!args.any() && args.mode.empty()) return true;
		error = "この mitiru_host はオンライン協力プレイ無しでビルドされています。使うときは cmake -DMITIRU_WITH_GEKKONET=ON でビルドし直してください。";
		return false;
	}
	void attach(Engine&) noexcept {}
	void onFrame(Engine&) {}
	void onModuleFrame(const module::FrameIntents& intents)
	{
		if (intents.netRequest.kind == module::kNetRequestNone) return;
		debug::warnOnce("net.request.unbuilt",
			"ゲームが hud.net* でオンライン協力プレイを頼みましたが、この mitiru_host はオンライン協力プレイ無しでビルドされています。"
			"使うときは cmake -DMITIRU_WITH_GEKKONET=ON でビルドし直してください。");
	}
	[[nodiscard]] int exitCode() const noexcept { return 0; }
	[[nodiscard]] bool active() const noexcept { return false; }
};

#endif // MITIRU_HAS_GEKKONET

} // namespace mitiru::host

#if defined(MITIRU_HAS_GEKKONET)
#include "HostOnlineUi.hpp"
#endif
