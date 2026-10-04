#pragma once

/// @file HostOnlineUi.hpp
/// @brief HostOnline の待合室の画面 (assets/ui/net_lobby.rml) と、ゲーム (netRequest / netView は ABI v50、netModeRequest /
///        netModeView は v51) との受け渡し。
///        HostOnline.hpp の最後から読む。
/// @details 画面からは "net.*" の操作だけが来て、host は view.net_* へ今の様子を押し出す (signal-only)。
///          遊び始めたら画面は閉じるが、view.net_ping などは押し出し続けるので、ゲームの RML の HUD が読める。
///          ゲームの依頼は画面の操作と同じ所へ流す。

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "HostOnline.hpp"

namespace mitiru::host
{

inline bool HostOnline::onUiAction(std::string_view name, std::string_view payload)
{
	if (name.rfind("net.", 0) != 0 && name != "input:net.address") return false;
	const auto p = nlohmann::json::parse(payload, nullptr, false);
	if (name == "input:net.address")
	{
		if (p.is_object() && p.contains("value") && p["value"].is_string()) m_typedAddress = p["value"].get<std::string>();
	}
	else if (name == "net.players")
	{
		if (p.is_number()) m_players = std::clamp(static_cast<int>(p.get<double>()), 2, 4);
	}
	else if (name == "net.host" && !m_session)
	{
		// 画面で部屋を作るのは、利用者が外の相手を待つと決めた時なので全部の口で待つ
		m_request = Request{true, network::ListenScope::Network, network::NetAddress{0, kDefaultPort}};
	}
	else if (name == "net.join" && !m_session)
	{
		if (const auto a = network::parseAddressOrCode(m_typedAddress))
		{
			m_request = Request{false, a->isLoopback() ? network::ListenScope::Loopback : network::ListenScope::Network, *a};
		}
		else m_error = "参加先 " + m_typedAddress + " を読めません。ip:port か参加コードを書いてください。";
	}
	else if (name == "net.ready")
	{
		m_ready = !m_ready;
		if (m_session) m_session->setReady(m_ready);
	}
	else if (name == "net.leave")
	{
		if (m_engine != nullptr) m_engine->setModuleDrawMemory(nullptr);
		m_session.reset();
		m_request.reset();
		m_error.clear();
		m_reportedPhase = OnlinePhase::Lobby;
		m_chooserOpen = false;
	}
	return true;
}

inline void HostOnline::onModuleFrame(const module::FrameIntents& intents)
{
	const module::NetRequest& r = intents.netRequest;
	if (r.kind == module::kNetRequestNone || m_engine == nullptr) return;
	// player を付けた依頼は、その席の PC だけが受ける (シミュレーションは全員の PC で同じ依頼を出す)
	if (r.player != 0 && (!m_session || m_session->localPlayer() + 1 != r.player)) return;
	if (!m_gameRequests)
	{
		debug::warnOnce("net.request.recording", "録画と再生の間は、ゲームが hud.net* で頼んだオンライン協力プレイを受け付けません。");
		return;
	}
	install(m_engine->mutableConfig());
	if (r.kind == module::kNetRequestOpen) m_chooserOpen = true;
	else if (r.kind == module::kNetRequestHost && !m_session)
	{
		m_players = std::clamp(r.players == 0 ? 2 : static_cast<int>(r.players), 2, 4);
		applyModeRequest(intents.netModeRequest);
		m_request = Request{true, network::ListenScope::Network, network::NetAddress{0, kDefaultPort}};
		m_restartOnStart = true;
	}
	else if (r.kind == module::kNetRequestJoin && !m_session)
	{
		m_typedAddress = std::string(r.code, strnlen(r.code, sizeof(r.code)));
		(void)onUiAction("net.join", "{}");
		m_restartOnStart = m_request.has_value();
	}
	else if (r.kind == module::kNetRequestLeave) (void)onUiAction("net.leave", "{}");
	else if (r.kind == module::kNetRequestReady)
	{
		m_ready = r.ready != 0;
		if (m_session) m_session->setReady(m_ready);
	}
}

namespace detail
{
[[nodiscard]] inline const char* onlinePhaseName(const OnlineSession* s) noexcept
{
	if (s == nullptr) return "choose";
	switch (s->phase())
	{
	case OnlinePhase::Lobby: return "lobby";
	case OnlinePhase::Running: return "running";
	default: return "failed";
	}
}

/// 遊んでいる間の ping は相手ごとの往復のうち一番遅いもの (ロールバックは GekkoNet が測った平均、host 権威は参加者は host との往復)
[[nodiscard]] inline int onlinePingMs(const OnlineSession& s)
{
	if (const auto* c = s.authorityClient()) return c->pingMs();
	if (const auto* h = s.authorityHost())
	{
		int worst = 0;
		for (int p = 1; p < h->config().players; ++p) worst = (std::max)(worst, static_cast<int>(h->pingMs(p)));
		return worst;
	}
	const auto* peer = s.peer();
	if (peer == nullptr) return s.lobby().hosting() ? 0 : s.lobby().pingToHostMs();
	float worst = 0.0f;
	for (int p = 0; p < peer->config().numPlayers; ++p) worst = (std::max)(worst, peer->networkStats(p).avg_ping);
	return static_cast<int>(worst + 0.5f);
}

/// 席 i が繋がっているか (始まる前は待合室の席、始まった後は回線が切れたと決めていないか)
[[nodiscard]] inline bool onlineSeatPresent(const OnlineSession& s, int i) noexcept
{
	const std::uint32_t bit = 1u << i;
	if (const auto* peer = s.peer()) return (peer->stats().disconnectedMask & bit) == 0;
	if (const auto* h = s.authorityHost()) return (h->stats().disconnectedMask & bit) == 0;
	if (const auto* c = s.authorityClient()) return (c->presentMask() & bit) != 0;
	return s.lobby().seat(i).occupied;
}

/// 席 i との往復の時間 (自分の席は使わない)
[[nodiscard]] inline float onlineSeatPingMs(const OnlineSession& s, int i)
{
	if (const auto* peer = s.peer()) return peer->networkStats(i).avg_ping;
	if (const auto* h = s.authorityHost()) return static_cast<float>(h->pingMs(i));
	if (const auto* c = s.authorityClient()) return i == 0 ? static_cast<float>(c->pingMs()) : 0.0f;
	return static_cast<float>(i == 0 && !s.lobby().hosting() ? s.lobby().pingToHostMs() : s.lobby().seat(i).pingMs);
}

/// 待合室の画面に出す方式。参加者は host の返事 (Welcome) が来るまで分からないので空
[[nodiscard]] inline const char* onlineModeName(const OnlineSession* s, network::NetMode chosen) noexcept
{
	const bool known = s == nullptr || s->lobby().hosting() || s->lobby().phase() != network::LobbyPhase::Connecting;
	if (!known) return "";
	const network::NetMode mode = s != nullptr ? s->mode() : chosen;
	return mode == network::NetMode::Authority ? "authority" : "rollback";
}
} // namespace detail

inline module::NetView HostOnline::netView() const
{
	module::NetView v{};
	const OnlineSession* s = m_session.get();
	if (s == nullptr)
	{
		v.state = m_chooserOpen || m_holdUntilOnline ? module::kNetStateLobby : module::kNetStateOffline;
		return v;
	}
	v.localPlayer = static_cast<std::uint8_t>(std::clamp(s->localPlayer(), 0, module::kMaxNetPlayers - 1));
	const auto* peer = s->peer();
	const int players = std::clamp(s->lobby().players(), 0, module::kMaxNetPlayers);
	for (int i = 0; i < players; ++i)
	{
		if (detail::onlineSeatPresent(*s, i) || i == v.localPlayer) v.present = static_cast<std::uint8_t>(v.present | (1u << i));
		if (i == v.localPlayer) continue;
		v.pingMs[i] = static_cast<std::uint16_t>(std::clamp(detail::onlineSeatPingMs(*s, i) + 0.5f, 0.0f, 65535.0f));
	}
	if (s->phase() == OnlinePhase::Failed) v.state = module::kNetStateStopped;
	else if (s->phase() == OnlinePhase::Lobby) v.state = module::kNetStateLobby;
	else if (peer != nullptr && peer->stats().desyncs > 0)
	{
		v.state = module::kNetStateDesync;
		v.desyncFrame = peer->stats().firstDesync.frame;
	}
	else v.state = module::kNetStateRunning;
	return v;
}

/// ゲームが部屋を作る時に頼んだ方式 (ABI v51)。0 の欄は起動の引数 (--net-mode / --net-rate) のまま
inline void HostOnline::applyModeRequest(const module::NetModeRequest& m)
{
	m_mode = m.mode == module::kNetModeRollback ? network::NetMode::Rollback
	       : m.mode == module::kNetModeAuthority ? network::NetMode::Authority : m_argMode;
	m_snapshotEvery = m.snapshotHz != 0 ? network::authority::snapshotEveryForRate(m.snapshotHz) : m_argSnapshotEvery;
}

/// 方式と遅れ (ABI v51)。host 権威の参加者は補間の様子と、自分の分を先に進めたフレームの数も出す
inline module::NetModeView HostOnline::netModeView() const
{
	module::NetModeView v{};
	const OnlineSession* s = m_session.get();
	if (s == nullptr || s->phase() != OnlinePhase::Running) return v;
	if (s->mode() == network::NetMode::Rollback)
	{
		v.mode = module::kNetModeRollback;
		return v;
	}
	v.mode = module::kNetModeAuthority;
	v.snapshotHz = static_cast<std::uint8_t>(60 / std::max(1, s->lobby().snapshotEvery()));
	if (const auto* c = s->authorityClient())
	{
		const auto& st = c->stats();
		v.interp = st.interp;
		v.predictedFrames = static_cast<std::uint8_t>(std::clamp(st.predictedFrames, 0, 255));
		v.renderDelayMs = static_cast<std::uint16_t>(std::clamp(st.renderDelayFrames * 1000 / 60, 0, 65535));
		v.snapshotAgeMs = static_cast<std::uint16_t>(std::min<std::uint64_t>(c->snapshotAgeMs(nowMs()), 65535));
	}
	return v;
}

/// 席ごとに state (0 = 空き / 1 = 準備中 / 2 = 準備 OK) と ping。RML は 1 つの値の比べ方だけで出し分ける
inline void HostOnline::pushSeat(ui_rml::RmlUiHost& ui, int i) const
{
	const std::string key = "view.net_seat" + std::to_string(i);
	const network::LobbySeat none{};
	const OnlineSession* s = m_session.get();
	const network::LobbySeat& seat = s != nullptr ? s->lobby().seat(i) : none;
	const bool me = s != nullptr && s->localPlayer() == i;
	// 参加者から見た host の席の ping は、自分と host の往復
	const int ping = (i == 0 && s != nullptr && !s->lobby().hosting()) ? s->lobby().pingToHostMs() : seat.pingMs;
	ui.setInt(key + "_state", !seat.occupied ? 0 : seat.ready ? 2 : 1);
	ui.setInt(key + "_ping", ping);
	ui.setBool(key + "_ping_shown", seat.occupied && !me && (i != 0 || !s->lobby().hosting()));
}

inline void HostOnline::pushUi(Engine& engine)
{
	auto& ui = engine.uiHost();
	if (!ui.active()) return;
	const OnlineSession* s = m_session.get();
	const std::string phase = detail::onlinePhaseName(s);
	ui.setText("view.net_phase", phase);
	ui.setText("view.net_error", s != nullptr && phase == "failed" ? s->error() : m_error);
	ui.setInt("view.net_players", s != nullptr ? s->lobby().players() : m_players);
	ui.setBool("view.net_hosting", s != nullptr && s->lobby().hosting());
	ui.setBool("view.net_ready", m_ready);
	ui.setText("view.net_code", s != nullptr && s->lobby().hosting() ? network::encodeJoinCode(m_hostShown) : std::string());
	ui.setText("view.net_address", s != nullptr ? network::toString(m_hostShown) : std::string());
	ui.setInt("view.net_seat", s != nullptr ? s->localPlayer() : -1);
	ui.setInt("view.net_ping", s != nullptr ? detail::onlinePingMs(*s) : 0);
	ui.setInt("view.net_rollbacks", s != nullptr && s->peer() != nullptr ? s->peer()->stats().loads : 0);
	ui.setText("view.net_mode", detail::onlineModeName(s, m_mode));
	ui.setInt("view.net_rate", 60 / (s != nullptr ? s->lobby().snapshotEvery() : m_snapshotEvery));
	for (int i = 0; i < 4; ++i) pushSeat(ui, i);
	// 待合室は遊び始めるまでだけ出す (画面に要る時だけ出す)
	const bool wantOverlay = m_interactive && phase != "running" && (m_holdUntilOnline || m_chooserOpen || s != nullptr);
	if (wantOverlay != m_overlayOpen)
	{
		m_overlayOpen = wantOverlay;
		if (wantOverlay) ui.openOverlay(kLobbyPage);
		else ui.closeOverlay(kLobbyPage);
	}
}

} // namespace mitiru::host
