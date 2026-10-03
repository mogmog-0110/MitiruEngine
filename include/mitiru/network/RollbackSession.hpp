#pragma once

/// @file RollbackSession.hpp
/// @brief ゲーム DLL 1 つ (ModuleApi + GameMemory) を GekkoNet のロールバック対戦の 1 人として進める
///
/// GekkoNet が出す 3 種のイベントを、エンジンが元から持つ仕組みにそのまま写す:
///   Save    → GameMemory と「1 フレーム前の入力」と窓口の image を保存 (rewind の ring と同じ扱い)
///   Load    → GameMemory → on_rebuild → 窓口の順に書き戻す (host の巻き戻しと同じ順)
///   Advance → 全員の入力を InputSnapshot に合成して on_update を 1 回 (replay と同じ扱い)
/// ゲーム DLL は回線もロールバックも知らない。全状態を GameMemory と窓口 (MITIRU_SIDE_STATE、ADR 0054) に
/// 持ち、on_update が入力だけで決まる (= replay が bit-exact) ゲームなら、そのまま対戦に載る。
/// 窓口の image は長さが変わるので、GekkoNet の保存枠は RollbackConfig::sideStateCapacity まで取っておく。
/// 枠を超えたら対戦を続けずに止め、error() で要る大きさを知らせる。
///
/// 巻き戻し中と先読み (runahead) 中の on_update が出した intent (音・セーブ要求など) は捨て、
/// 確定した進行の intent だけを intents() で渡す。同じ音が巻き戻しのたびに鳴らないようにするため。

#if defined(MITIRU_HAS_GEKKONET)

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gekkonet.h>

#include "mitiru/module/ModuleApi.hpp"
#include "mitiru/module/SideStateHost.hpp"
#include "mitiru/network/RollbackInput.hpp"

namespace mitiru::network::rollback
{

struct RollbackConfig
{
	int numPlayers = 2;
	int localPlayer = 0;              ///< この peer が操作する player 番号
	int inputDelay = 2;               ///< 自分の入力を何フレーム遅らせて使うか (予測外れを減らす)
	int predictionWindow = 8;         ///< 相手の入力を最大何フレーム先まで予測して進めるか
	bool desyncDetection = true;      ///< 確定したフレームの checksum を相手と突き合わせる
	unsigned disconnectTimeoutMs = 0; ///< この時間 packet が来ない相手を切る (0 = 切らない。切るかは呼ぶ側が決める)
	std::uint32_t sideStateCapacity = 0;  ///< 窓口の image に取る byte 数 (0 = 始めの image の 4 倍と 64 KB の大きい方)
	SnapshotBase snapshot{};
	std::array<Keymap, kMaxPlayers> keymaps{kKeymapPlayer1, kKeymapPlayer2, Keymap{}, Keymap{}};
};

/// @brief 相手と checksum が食い違った確定フレーム
struct DesyncInfo
{
	int frame = -1;
	std::uint32_t local = 0;
	std::uint32_t remote = 0;
	int remotePlayer = -1;
};

struct RollbackStats
{
	bool started = false;
	int frame = -1;                   ///< 最後に進めたフレーム
	int advances = 0;                 ///< on_update を呼んだ回数 (巻き戻しの進め直しを含む)
	int resimulated = 0;              ///< そのうち巻き戻しの進め直し
	int loads = 0;                    ///< 巻き戻した回数
	int desyncs = 0;                  ///< checksum が相手と食い違った回数
	int disconnects = 0;              ///< 切れた相手の数
	std::uint32_t disconnectedMask = 0;   ///< bit p = player p が切れた
	DesyncInfo firstDesync{};
};

/// @brief 保存したフレームの checksum。total は GekkoNet が相手と比べる値、memory と side はその内訳
struct FrameChecksum
{
	int frame = -1;
	std::uint32_t total = 0;
	std::uint32_t memory = 0;         ///< GameMemory の分
	std::uint32_t side = 0;           ///< 窓口の image の分 (窓口が無ければ 0)
};

class RollbackPeer
{
public:
	static constexpr int kChecksumRing = 128;

	/// @param remotes player 番号 → 相手の住所 (自分の番号の要素は使わない)
	/// @param sides   DLL の窓口を取り込んだもの (無い game は null)。peer より長く生かす
	RollbackPeer(const module::ModuleApi& api, void* memory, const RollbackConfig& cfg, GekkoNetAdapter* adapter,
		std::span<const GekkoNetAddress> remotes, module::SideStateHost* sides = nullptr)
		: m_update(api.on_update), m_rebuild(api.on_rebuild), m_memorySize(api.memorySize), m_memory(memory), m_cfg(cfg),
		  m_sides(sides != nullptr && !sides->empty() ? sides : nullptr),
		  m_intents(std::make_unique<module::FrameIntents>()), m_scratch(std::make_unique<module::FrameIntents>())
	{
		m_intents->reset();
		m_error = rejectReason(api, memory, cfg, m_sides);
		if (m_error == nullptr) m_error = reserveSideState();
		if (m_error != nullptr) return;
		gekko_create(&m_session, GekkoGameSession);
		GekkoConfig gc{};
		gc.num_players = static_cast<unsigned char>(cfg.numPlayers);
		gc.input_prediction_window = static_cast<unsigned char>(cfg.predictionWindow);
		gc.input_size = sizeof(PadInput);
		gc.state_size = static_cast<unsigned int>(stateSize());
		gc.desync_detection = cfg.desyncDetection;
		gekko_start(m_session, &gc);
		gekko_net_adapter_set(m_session, adapter);
		gekko_set_disconnect_timeout(m_session, cfg.disconnectTimeoutMs);
		for (int p = 0; p < cfg.numPlayers; ++p)
		{
			GekkoNetAddress addr = p < static_cast<int>(remotes.size()) ? remotes[static_cast<std::size_t>(p)] : GekkoNetAddress{};
			if (p == cfg.localPlayer) gekko_add_actor(m_session, GekkoLocalPlayer, nullptr);
			else gekko_add_actor(m_session, GekkoRemotePlayer, &addr);
		}
		gekko_set_local_delay(m_session, cfg.localPlayer, static_cast<unsigned char>(cfg.inputDelay));
	}
	~RollbackPeer()
	{
		if (m_session != nullptr) gekko_destroy(&m_session);
	}
	RollbackPeer(const RollbackPeer&) = delete;
	RollbackPeer& operator=(const RollbackPeer&) = delete;

	/// @brief on_update / on_rebuild を直に呼ばず calls を通す (ctx は peer より長く生かす)
	void setCalls(const RollbackCalls& calls) noexcept { m_calls = calls; }

	/// @brief 1 フレーム分: 自分の入力を渡し、GekkoNet が求める保存・復元・進行をすべて済ませる
	void tick(PadInput local)
	{
		if (m_session == nullptr || m_error != nullptr) return;
		gekko_network_poll(m_session);
		gekko_add_local_input(m_session, m_cfg.localPlayer, &local);
		// 前の update と今の poll が出した分。update が始めに消すので、ここで 1 回だけ読む
		handleSessionEvents();
		int count = 0;
		GekkoGameEvent** events = gekko_update_session(m_session, &count);
		for (int i = 0; i < count; ++i) handleGameEvent(*events[i]);
	}

	/// @brief 進めずに回線だけ回す (相手より先に進みすぎた時に 1 フレーム待つ)
	void pollNetwork()
	{
		if (m_session == nullptr || m_error != nullptr) return;
		gekko_network_poll(m_session);
	}

	/// @brief 確定した進行で、最後に on_update が出した intent (巻き戻し中・先読み中の分は含まない)
	[[nodiscard]] const module::FrameIntents& intents() const noexcept { return *m_intents; }

	/// @brief 前に取ってから確定した進行があれば、その intent を out へ写して true
	bool takeConfirmedIntents(module::FrameIntents& out)
	{
		if (!m_freshIntents) return false;
		m_freshIntents = false;
		std::memcpy(&out, m_intents.get(), sizeof(out));
		return true;
	}

	/// @brief 対戦に載せられないゲームなら理由 (null = 載る)。理由がある peer の tick は何もしない
	[[nodiscard]] const char* error() const noexcept { return m_error; }
	[[nodiscard]] const RollbackStats& stats() const noexcept { return m_stats; }
	[[nodiscard]] const RollbackConfig& config() const noexcept { return m_cfg; }

	/// @brief frame を保存した時の checksum (最後の保存。古くて残っていなければ null)
	/// @details 予測が外れたフレームは保存し直されるので、相手の入力が全部届いたフレームの値は確定した状態の値になる
	[[nodiscard]] const FrameChecksum* checksumOf(int frame) const noexcept
	{
		if (frame < 0) return nullptr;
		const FrameChecksum& c = m_sums[static_cast<std::size_t>(frame % kChecksumRing)];
		return c.frame == frame ? &c : nullptr;
	}

	/// @brief 相手より平均で何フレーム先にいるか (時計合わせ用)
	[[nodiscard]] float framesAhead() const noexcept { return m_session != nullptr ? gekko_frames_ahead(m_session) : 0.0f; }

	/// @brief player の回線の様子 (自分の番号は全部 0)
	[[nodiscard]] GekkoNetworkStats networkStats(int player) const noexcept
	{
		GekkoNetworkStats s{};
		if (m_session != nullptr && player != m_cfg.localPlayer) gekko_network_stats(m_session, player, &s);
		return s;
	}

	/// @brief 各フレームで最後に使った全員の入力を残す (検証用。null で止める)
	void setInputLog(std::vector<std::array<PadInput, kMaxPlayers>>* log) noexcept { m_log = log; }

	/// @brief GekkoNet に取らせる保存枠の大きさ (窓口の image の分は上限)
	[[nodiscard]] std::size_t stateSize() const noexcept { return baseSize() + m_sideCapacity; }

private:
	[[nodiscard]] std::size_t baseSize() const noexcept
	{
		return m_memorySize + sizeof(PadInput) * static_cast<std::size_t>(m_cfg.numPlayers);
	}

	[[nodiscard]] static const char* rejectReason(const module::ModuleApi& api, const void* memory, const RollbackConfig& cfg,
		const module::SideStateHost* sides)
	{
		if (api.on_update == nullptr || memory == nullptr || api.memorySize == 0)
			return "GameMemory の大きさ (memorySize) を申告していない: 保存・復元できない";
		if ((api.stateFlags & module::kModuleStatePartial) != 0 && (sides == nullptr || !sides->coversScene()))
			return "GameMemory が状態の一部しか持たない (kModuleStatePartial): 巻き戻すと場面がずれる";
		if (cfg.numPlayers < 2 || cfg.numPlayers > kMaxPlayers || cfg.localPlayer < 0 || cfg.localPlayer >= cfg.numPlayers)
			return "人数か自分の番号が範囲外";
		// GekkoNet へは 1 バイトで渡る。丸めると設定と違う遅延・予測幅で動く
		if (cfg.inputDelay < 0 || cfg.inputDelay > 255 || cfg.predictionWindow < 0 || cfg.predictionWindow > 255)
			return "inputDelay / predictionWindow は 0..255";
		return nullptr;
	}

	/// @brief 始めの窓口の image を取り、GekkoNet の保存枠に足す大きさを決める
	[[nodiscard]] const char* reserveSideState()
	{
		if (m_sides == nullptr) return nullptr;
		std::string why;
		if (!m_sides->capture(m_memory, true, m_sideImage, &why))
			return fail("GameMemory の外に持つ状態を保存できない: " + why);
		const std::size_t automatic = (std::max)(m_sideImage.size() * 4, std::size_t{64} * 1024);
		m_sideCapacity = m_cfg.sideStateCapacity > 0 ? m_cfg.sideStateCapacity : automatic;
		m_sideImage.reserve(m_sideCapacity);
		if (m_sideImage.size() > m_sideCapacity) return fail(capacityMessage(m_sideImage.size()));
		return nullptr;
	}

	[[nodiscard]] static std::string capacityMessage(std::size_t need)
	{
		return "窓口の image が sideStateCapacity を超えた (" + std::to_string(need) + " byte 要る。RollbackConfig::sideStateCapacity を広げる)";
	}

	/// @brief 対戦を止める理由を残す。以後の tick は何もしない
	const char* fail(std::string why)
	{
		m_errorText = std::move(why);
		m_error = m_errorText.c_str();
		return m_error;
	}

	void handleSessionEvents()
	{
		int count = 0;
		GekkoSessionEvent** events = gekko_session_events(m_session, &count);
		for (int i = 0; i < count; ++i)
		{
			const GekkoSessionEvent& e = *events[i];
			if (e.type == GekkoSessionStarted) m_stats.started = true;
			if (e.type == GekkoPlayerDisconnected)
			{
				++m_stats.disconnects;
				const int h = e.data.disconnected.handle;
				if (h >= 0 && h < 32) m_stats.disconnectedMask |= 1u << h;
			}
			if (e.type != GekkoDesyncDetected) continue;
			++m_stats.desyncs;
			if (m_stats.firstDesync.frame >= 0) continue;
			m_stats.firstDesync = DesyncInfo{e.data.desynced.frame, e.data.desynced.local_checksum,
				e.data.desynced.remote_checksum, e.data.desynced.remote_handle};
		}
	}

	void handleGameEvent(const GekkoGameEvent& e)
	{
		if (m_error != nullptr) return;
		if (e.type == GekkoSaveEvent) save(e.data.save);
		else if (e.type == GekkoLoadEvent) load(e.data.load);
		else if (e.type == GekkoAdvanceEvent) advance(e.data.adv);
	}

	/// @brief [GameMemory][1 フレーム前の入力][窓口の image]。checksum は全体に取るので、窓口の食い違いも desync になる
	void save(const GekkoGameEvent::GekkoEventData::GekkoSave& s)
	{
		std::memcpy(s.state, m_memory, m_memorySize);
		std::memcpy(s.state + m_memorySize, m_prev.data(), sizeof(PadInput) * static_cast<std::size_t>(m_cfg.numPlayers));
		std::size_t len = baseSize();
		std::uint32_t side = 0;
		if (m_sides != nullptr)
		{
			std::string why;
			if (!m_sides->capture(m_memory, true, m_sideImage, &why))
			{
				fail("GameMemory の外に持つ状態を保存できない: " + why);
				return;
			}
			if (m_sideImage.size() > m_sideCapacity)
			{
				fail(capacityMessage(m_sideImage.size()));
				return;
			}
			std::memcpy(s.state + len, m_sideImage.data(), m_sideImage.size());
			side = stateChecksum(m_sideImage.data(), m_sideImage.size());
			len += m_sideImage.size();
		}
		const std::uint32_t memory = stateChecksum(s.state, m_memorySize);
		s.state_len[0] = static_cast<unsigned int>(len);
		s.checksum[0] = stateChecksum(s.state + m_memorySize, len - m_memorySize, memory);
		if (s.frame >= 0) m_sums[static_cast<std::size_t>(s.frame % kChecksumRing)] = FrameChecksum{s.frame, s.checksum[0], memory, side};
	}

	void load(const GekkoGameEvent::GekkoEventData::GekkoLoad& l)
	{
		std::memcpy(m_memory, l.state, m_memorySize);
		std::memcpy(m_prev.data(), l.state + m_memorySize, sizeof(PadInput) * static_cast<std::size_t>(m_cfg.numPlayers));
		if (!rebuild()) return;
		++m_stats.loads;
		if (m_sides == nullptr) return;
		std::string why;
		const std::size_t base = baseSize();
		if (l.state_len <= base || !m_sides->restore(m_memory, l.state + base, l.state_len - base, &why))
		{
			fail("GameMemory の外に持つ状態を戻せない: " + (why.empty() ? std::string("保存枠に image が無い") : why));
		}
	}

	bool rebuild()
	{
		if (m_calls.rebuild != nullptr)
		{
			if (m_calls.rebuild(m_calls.ctx)) return true;
			fail("on_rebuild で game が止まった");
			return false;
		}
		if (m_rebuild != nullptr) m_rebuild(m_memory, module::kModuleRebuildRestore);
		return true;
	}

	void advance(const GekkoGameEvent::GekkoEventData::GekkoAdvance& a)
	{
		const auto n = static_cast<std::size_t>(m_cfg.numPlayers);
		std::array<PadInput, kMaxPlayers> now{};
		std::memcpy(now.data(), a.inputs, sizeof(PadInput) * n);
		composeSnapshot(m_snap, std::span<const PadInput>(now.data(), n), std::span<const PadInput>(m_prev.data(), n),
			m_cfg.keymaps, m_cfg.snapshot);
		const bool speculative = a.rolling_back || a.running_ahead;
		module::FrameIntents* out = speculative ? m_scratch.get() : m_intents.get();
		out->reset();
		if (m_calls.update != nullptr)
		{
			if (!m_calls.update(m_calls.ctx, &m_snap, out))
			{
				fail("on_update で game が止まった");
				return;
			}
		}
		else m_update(m_memory, m_cfg.snapshot.dt, &m_snap, out);
		if (!speculative) m_freshIntents = true;
		m_prev = now;
		m_stats.frame = a.frame;
		++m_stats.advances;
		if (a.rolling_back) ++m_stats.resimulated;
		logInputs(a.frame, now);
	}

	void logInputs(int frame, const std::array<PadInput, kMaxPlayers>& now)
	{
		if (m_log == nullptr || frame < 0) return;
		if (m_log->size() <= static_cast<std::size_t>(frame)) m_log->resize(static_cast<std::size_t>(frame) + 1);
		(*m_log)[static_cast<std::size_t>(frame)] = now;
	}

	using UpdateFn = void (*)(void*, float, const module::InputSnapshot*, module::FrameIntents*);
	UpdateFn m_update = nullptr;
	void (*m_rebuild)(void*, std::uint32_t) = nullptr;
	std::uint32_t m_memorySize = 0;
	void* m_memory = nullptr;
	const char* m_error = nullptr;
	std::string m_errorText;
	RollbackConfig m_cfg;
	RollbackCalls m_calls{};
	module::SideStateHost* m_sides = nullptr;
	std::vector<std::uint8_t> m_sideImage;   ///< save ごとの capture 先 (枠の大きさまで先に取ってある)
	std::size_t m_sideCapacity = 0;
	GekkoSession* m_session = nullptr;
	std::array<PadInput, kMaxPlayers> m_prev{};
	module::InputSnapshot m_snap{};          ///< 合成先。10 KB 余りあるので毎回 stack に積まない
	std::unique_ptr<module::FrameIntents> m_intents;
	std::unique_ptr<module::FrameIntents> m_scratch;
	bool m_freshIntents = false;
	std::array<FrameChecksum, kChecksumRing> m_sums{};
	std::vector<std::array<PadInput, kMaxPlayers>>* m_log = nullptr;
	RollbackStats m_stats;
};

} // namespace mitiru::network::rollback

#endif // MITIRU_HAS_GEKKONET
