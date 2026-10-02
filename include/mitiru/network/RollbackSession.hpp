#pragma once

/// @file RollbackSession.hpp
/// @brief ゲーム DLL 1 つ (ModuleApi + GameMemory) を GekkoNet のロールバック対戦の 1 人として進める
///
/// GekkoNet が出す 3 種のイベントを、エンジンが元から持つ仕組みにそのまま写す:
///   Save    → GameMemory と「1 フレーム前のボタン」を memcpy で保存 (rewind の ring と同じ扱い)
///   Load    → それを memcpy で書き戻す
///   Advance → 全員のボタンを InputSnapshot に合成して on_update を 1 回 (replay と同じ扱い)
/// ゲーム DLL は回線もロールバックも知らない。GameMemory が flat POD で全状態を持ち、on_update が
/// 入力だけで決まる (= replay が bit-exact) ゲームなら、そのまま対戦に載る。
///
/// 巻き戻し中と先読み (runahead) 中の on_update が出した intent (音・セーブ要求など) は捨て、
/// 確定した進行の intent だけを intents() で渡す。同じ音が巻き戻しのたびに鳴らないようにするため。

#if defined(MITIRU_HAS_GEKKONET)

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gekkonet.h>

#include "mitiru/module/ModuleApi.hpp"
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
	SnapshotBase snapshot{};
	std::array<Keymap, kMaxPlayers> keymaps{kKeymapPlayer1, kKeymapPlayer2, Keymap{}, Keymap{}};
};

struct RollbackStats
{
	bool started = false;
	int frame = -1;                   ///< 最後に進めたフレーム
	int advances = 0;                 ///< on_update を呼んだ回数 (巻き戻しの進め直しを含む)
	int resimulated = 0;              ///< そのうち巻き戻しの進め直し
	int loads = 0;                    ///< 巻き戻した回数
	int desyncs = 0;                  ///< checksum が相手と食い違った回数
	int firstDesyncFrame = -1;
};

/// @brief GameMemory と保存した状態を FNV-1a で 32bit に畳む (desync 検出用)
[[nodiscard]] inline std::uint32_t stateChecksum(const void* data, std::size_t size) noexcept
{
	std::uint32_t h = 2166136261u;
	const auto* p = static_cast<const std::uint8_t*>(data);
	for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
	return h;
}

class RollbackPeer
{
public:
	/// @param remotes player 番号 → 相手の住所 (自分の番号の要素は使わない)
	RollbackPeer(const module::ModuleApi& api, void* memory, const RollbackConfig& cfg, GekkoNetAdapter* adapter,
		std::span<const GekkoNetAddress> remotes)
		: m_update(api.on_update), m_memorySize(api.memorySize), m_memory(memory), m_cfg(cfg),
		  m_intents(std::make_unique<module::FrameIntents>()), m_scratch(std::make_unique<module::FrameIntents>())
	{
		m_intents->reset();
		m_error = rejectReason(api, memory, cfg);
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
		gekko_set_disconnect_timeout(m_session, 0);   // 遅延を足した回線でも切らない (切断は呼ぶ側が決める)
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

	/// @brief 1 フレーム分: 自分の入力を渡し、GekkoNet が求める保存・復元・進行をすべて済ませる
	void tick(PadInput local)
	{
		if (m_session == nullptr) return;
		gekko_network_poll(m_session);
		gekko_add_local_input(m_session, m_cfg.localPlayer, &local);
		handleSessionEvents();
		int count = 0;
		GekkoGameEvent** events = gekko_update_session(m_session, &count);
		for (int i = 0; i < count; ++i) handleGameEvent(*events[i]);
	}

	/// @brief 確定した進行で、最後に on_update が出した intent (巻き戻し中・先読み中の分は含まない)
	[[nodiscard]] const module::FrameIntents& intents() const noexcept { return *m_intents; }

	/// @brief 対戦に載せられないゲームなら理由 (null = 載る)。理由がある peer の tick は何もしない
	[[nodiscard]] const char* error() const noexcept { return m_error; }
	[[nodiscard]] const RollbackStats& stats() const noexcept { return m_stats; }

	/// @brief 各フレームで最後に使った全員のボタンを残す (検証用。null で止める)
	void setInputLog(std::vector<std::array<PadInput, kMaxPlayers>>* log) noexcept { m_log = log; }

	[[nodiscard]] std::size_t stateSize() const noexcept
	{
		return m_memorySize + sizeof(PadInput) * static_cast<std::size_t>(m_cfg.numPlayers);
	}

private:
	[[nodiscard]] static const char* rejectReason(const module::ModuleApi& api, const void* memory, const RollbackConfig& cfg)
	{
		if (api.on_update == nullptr || memory == nullptr || api.memorySize == 0)
			return "GameMemory の大きさ (memorySize) を申告していない: 保存・復元できない";
		if ((api.stateFlags & module::kModuleStatePartial) != 0)
			return "GameMemory が状態の一部しか持たない (kModuleStatePartial): 巻き戻すと場面がずれる";
		if (cfg.numPlayers < 2 || cfg.numPlayers > kMaxPlayers || cfg.localPlayer < 0 || cfg.localPlayer >= cfg.numPlayers)
			return "人数か自分の番号が範囲外";
		// GekkoNet へは 1 バイトで渡る。丸めると設定と違う遅延・予測幅で動く
		if (cfg.inputDelay < 0 || cfg.inputDelay > 255 || cfg.predictionWindow < 0 || cfg.predictionWindow > 255)
			return "inputDelay / predictionWindow は 0..255";
		return nullptr;
	}

	void handleSessionEvents()
	{
		int count = 0;
		GekkoSessionEvent** events = gekko_session_events(m_session, &count);
		for (int i = 0; i < count; ++i)
		{
			if (events[i]->type == GekkoSessionStarted) m_stats.started = true;
			if (events[i]->type != GekkoDesyncDetected) continue;
			++m_stats.desyncs;
			if (m_stats.firstDesyncFrame < 0) m_stats.firstDesyncFrame = events[i]->data.desynced.frame;
		}
	}

	void handleGameEvent(const GekkoGameEvent& e)
	{
		if (e.type == GekkoSaveEvent) save(e.data.save);
		else if (e.type == GekkoLoadEvent) load(e.data.load);
		else if (e.type == GekkoAdvanceEvent) advance(e.data.adv);
	}

	void save(const GekkoGameEvent::GekkoEventData::GekkoSave& s)
	{
		std::memcpy(s.state, m_memory, m_memorySize);
		std::memcpy(s.state + m_memorySize, m_prev.data(), sizeof(PadInput) * static_cast<std::size_t>(m_cfg.numPlayers));
		s.state_len[0] = static_cast<unsigned int>(stateSize());
		s.checksum[0] = stateChecksum(s.state, stateSize());
	}

	void load(const GekkoGameEvent::GekkoEventData::GekkoLoad& l)
	{
		std::memcpy(m_memory, l.state, m_memorySize);
		std::memcpy(m_prev.data(), l.state + m_memorySize, sizeof(PadInput) * static_cast<std::size_t>(m_cfg.numPlayers));
		++m_stats.loads;
	}

	void advance(const GekkoGameEvent::GekkoEventData::GekkoAdvance& a)
	{
		const auto n = static_cast<std::size_t>(m_cfg.numPlayers);
		std::array<PadInput, kMaxPlayers> now{};
		std::memcpy(now.data(), a.inputs, sizeof(PadInput) * n);
		module::InputSnapshot snap;
		composeSnapshot(snap, std::span<const PadInput>(now.data(), n), std::span<const PadInput>(m_prev.data(), n),
			m_cfg.keymaps, m_cfg.snapshot);
		const bool speculative = a.rolling_back || a.running_ahead;
		module::FrameIntents* out = speculative ? m_scratch.get() : m_intents.get();
		out->reset();
		m_update(m_memory, m_cfg.snapshot.dt, &snap, out);
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
	std::uint32_t m_memorySize = 0;
	void* m_memory = nullptr;
	const char* m_error = nullptr;
	RollbackConfig m_cfg;
	GekkoSession* m_session = nullptr;
	std::array<PadInput, kMaxPlayers> m_prev{};
	std::unique_ptr<module::FrameIntents> m_intents;
	std::unique_ptr<module::FrameIntents> m_scratch;
	std::vector<std::array<PadInput, kMaxPlayers>>* m_log = nullptr;
	RollbackStats m_stats;
};

} // namespace mitiru::network::rollback

#endif // MITIRU_HAS_GEKKONET
