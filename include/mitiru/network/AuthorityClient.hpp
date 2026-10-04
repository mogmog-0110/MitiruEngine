#pragma once

/// @file AuthorityClient.hpp
/// @brief host 権威の参加者の側: 入力を host へ送り、届いた状態の間を補間して描く
///
/// GameMemory は描画のためだけに持つ。状態 n が届いたら GameMemory を状態 n に戻し、そこから次の状態までの
/// フレームは、host が状態と一緒に送る入力で on_update を 1 回ずつ呼んで作る。ゲームは入力だけで決まるので、
/// 作ったフレームは host のそのフレームと同じになり、2 つの状態の間を線形に混ぜるより正確に補間できる
/// (GameMemory の中身の意味を engine は知らないので、混ぜ方を決められない)。音と HUD の intent もその on_update が出す。
/// 描くのは作れる一番先のフレーム (届いた状態か、届いた入力の先) から kMarginFrames だけ後ろで、届く間隔の揺れを吸う。
/// 作ったフレームが次の状態と違えば drift として数える (GameMemory と窓口の外に状態があるか、入力以外を読んでいる)。

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <mitiru/network/AuthorityHost.hpp>
#include <mitiru/network/NetCorrections.hpp>

namespace mitiru::network::authority
{

struct AuthorityClientStats
{
	int frame = -1;                      ///< GameMemory が今どのフレームの状態か (まだ無ければ -1)
	int newestFrame = -1;                ///< 届いた一番新しい状態
	std::uint64_t bytesReceived = 0;     ///< host からの packet の合計 (行き先の 1 byte を含む、IP と UDP の頭は含まない)
	std::uint64_t packetsReceived = 0;
	std::uint64_t bytesSent = 0;         ///< 入力の packet の合計
	std::uint64_t packetsSent = 0;
	int snapshots = 0;                   ///< 組み上げて読めた状態
	int dropped = 0;                     ///< 組み上げたが読めなかった (壊れている・差分の元を持っていない)
	int steps = 0;                       ///< 補間のために on_update を呼んだ回数
	int jumps = 0;                       ///< 補間できず、状態へ飛んだ回数
	int stalls = 0;                      ///< 進めたかったが入力が届いていなかったフレーム
	int drifts = 0;                      ///< 作ったフレームが届いた状態と違った回数
	int firstDriftFrame = -1;
	int predictions = 0;                 ///< 自分の分を先に進めた写しを作ったフレーム
	int predictedFrames = 0;             ///< 今の写しで先に進めたフレームの数 (0 = 写しを作っていない)
	int corrections = 0;                 ///< 先読みが host の入力の使い方と食い違い、描く自分を正したフレーム
	int renderDelayFrames = 0;           ///< 描いている状態が、作れる一番先のフレームよりどれだけ後ろか
	std::uint8_t interp = 0;             ///< 今のフレームの進め方 (module::kNetInterp*)
};

class AuthorityClient
{
public:
	/// 作れる一番先のフレームから何フレーム後ろを描くか。届く間隔の揺れ (jitter) をこの分だけ吸う
	static constexpr int kMarginFrames = 2;
	/// これより遅れたら補間をやめて一番新しい状態へ飛ぶ
	static constexpr int kMaxLagFrames = 45;
	/// 自分の分を先に進めるフレームの上限。これより多く溜まった入力は、新しい方からこの数だけ当てる
	static constexpr int kMaxPredictFrames = 30;

	AuthorityClient(const module::ModuleApi& api, void* memory, const AuthorityConfig& cfg, DatagramEndpoint& net,
		module::SideStateHost* sides, std::uint64_t nowMs)
		: m_update(api.on_update), m_rebuild(api.on_rebuild), m_memorySize(api.memorySize), m_memory(memory), m_cfg(cfg),
		  m_net(&net), m_sides(sides != nullptr && !sides->empty() ? sides : nullptr),
		  m_intents(std::make_unique<module::FrameIntents>()), m_snap(std::make_unique<module::InputSnapshot>()),
		  m_predictSnap(std::make_unique<module::InputSnapshot>()), m_lastHeardMs(nowMs)
	{
		m_intents->reset();
		if (api.on_update == nullptr || memory == nullptr || api.memorySize == 0) fail("GameMemory の大きさ (memorySize) を申告していない");
		else if (cfg.players < 2 || cfg.players > kMaxPlayers || cfg.snapshotEvery < 1 || cfg.snapshotEvery > 60) fail("人数か状態の間隔が範囲外");
		std::size_t side = 0;
		std::string why;
		if (m_sides != nullptr && m_sides->capture(m_memory, true, m_side, &why)) side = m_side.size();
		m_maxImage = maxImageBytes(m_memorySize, side);
		m_maxBody = m_maxImage + 64;
		m_reassembler.setLimit(m_maxBody + 16);
	}

	void setCalls(const RollbackCalls& calls) noexcept { m_calls = calls; }

	/// @brief 1 フレーム分: 自分の入力を host へ送り、届いた状態を読み、描く状態を 0..2 フレーム進める
	void tick(std::uint64_t nowMs, const PadInput& local)
	{
		if (!m_error.empty()) return;
		m_now = nowMs;
		sendInput(local);
		receive();
		if (m_now > m_lastHeardMs + m_cfg.disconnectTimeoutMs) fail("host との接続が切れた");
		if (m_error.empty()) keepShownBase();
		if (m_error.empty()) playout();
		if (m_error.empty()) predict();
		if (m_error.empty()) detectCorrection();
	}

	/// @brief 先読みが外れて描く自分を正したら、正す前と後の組を ring へ残す (null で止める)
	void setCorrections(NetCorrectionRing* ring) noexcept { m_corrections = ring; }

	bool takeConfirmedIntents(module::FrameIntents& out)
	{
		if (!m_fresh) return false;
		m_fresh = false;
		std::memcpy(&out, m_intents.get(), sizeof(out));
		return true;
	}

	[[nodiscard]] const std::string& error() const noexcept { return m_error; }
	[[nodiscard]] const AuthorityClientStats& stats() const noexcept { return m_stats; }
	[[nodiscard]] const AuthorityConfig& config() const noexcept { return m_cfg; }
	[[nodiscard]] std::uint16_t pingMs() const noexcept { return m_rttMs; }
	/// @brief host が最後に知らせた、繋がっている席の bit
	[[nodiscard]] std::uint8_t presentMask() const noexcept { return m_present; }
	[[nodiscard]] std::uint32_t watchedChecksum() const noexcept { return m_watchedSum; }
	[[nodiscard]] bool watched() const noexcept { return m_watched; }
	/// @brief 自分の分を先に進めた描画用の写し。先読みしていない (関数が無い・まだ状態が無い) なら nullptr
	[[nodiscard]] void* drawMemory() noexcept { return m_stats.predictedFrames > 0 ? m_drawMemory.data() : nullptr; }
	/// @brief 一番新しい状態が届いてからの時間。まだ届いていなければ 0
	[[nodiscard]] std::uint64_t snapshotAgeMs(std::uint64_t nowMs) const noexcept
	{
		return m_newestAtMs != 0 && nowMs > m_newestAtMs ? nowMs - m_newestAtMs : 0;
	}
	/// @brief 描く状態が frame に来た時の checksum を取っておく (--net-frames の突き合わせ用)
	void watchFrame(int frame) noexcept { m_watchFrame = frame; }

private:
	struct Received
	{
		std::uint32_t frame = kNoFrame;
		std::vector<std::uint8_t> image;
	};

	struct InputRow
	{
		std::uint32_t frame = kNoFrame;
		std::array<PadInput, kMaxPlayers> pads{};
		std::array<std::uint32_t, kMaxPlayers> seqs{};   ///< 席ごとに、そのフレームまでに host が使った入力の番号
	};

	static constexpr int kHistory = 16;
	static constexpr int kInputRing = 128;
	static constexpr int kLocalHistory = 128;
	static constexpr std::size_t kInputRowBytes = kPadInputWireBytes + 4;
	static constexpr std::size_t kMaxInputsBody = 8 + 63 * kMaxPlayers * kInputRowBytes;

	void fail(std::string why) { m_error = std::move(why); }

	void sendInput(const PadInput& local)
	{
		++m_seq;
		m_recent[m_seq % kInputRedundancy] = local;
		m_localHistory[m_seq % kLocalHistory] = local;
		const auto count = (std::min)(static_cast<std::uint32_t>(kInputRedundancy), m_seq);
		m_out.clear();
		putLe(m_out, kMsgInput, 1);
		putLe(m_out, m_seq, 4);
		putLe(m_out, m_stats.newestFrame >= 0 ? static_cast<std::uint32_t>(m_stats.newestFrame) : kNoFrame, 4);
		putLe(m_out, static_cast<std::uint32_t>(m_now), 4);
		putLe(m_out, m_hostTime, 4);
		putLe(m_out, (std::min<std::uint64_t>)(m_now - m_hostTimeAt, 65535), 2);
		putLe(m_out, count, 1);
		for (std::uint32_t k = 0; k < count; ++k) putPadInput(m_out, m_recent[(m_seq - k) % kInputRedundancy]);
		m_net->send(kChannelAuthority, m_cfg.addrs[0], m_out.data(), m_out.size());
		m_stats.bytesSent += m_out.size() + 1;
		++m_stats.packetsSent;
	}

	void receive()
	{
		auto& box = m_net->inbox(kChannelAuthority);
		for (const Datagram& d : box)
		{
			if (!(d.from == m_cfg.addrs[0])) continue;
			m_stats.bytesReceived += d.bytes.size() + 1;
			++m_stats.packetsReceived;
			WireReader r(d.bytes);
			const auto type = r.get(1);
			if (type == kMsgInputs)
			{
				const std::span<const std::uint8_t> packed(d.bytes.data() + r.at, d.bytes.size() - r.at);
				if (r.ok && m_codec.unpack(packed, kMaxInputsBody, m_inputsBody) && readInputs(m_inputsBody)) m_lastHeardMs = m_now;
				continue;
			}
			const auto frame = static_cast<std::uint32_t>(r.get(4));
			const int index = static_cast<int>(r.get(2));
			const int count = static_cast<int>(r.get(2));
			if (!r.ok || type != kMsgSnapshot) continue;
			m_lastHeardMs = m_now;
			const std::span<const std::uint8_t> payload(d.bytes.data() + r.at, d.bytes.size() - r.at);
			if (m_reassembler.add(frame, index, count, payload, m_assembled)) readSnapshot();
		}
		box.clear();
	}

	[[nodiscard]] Received* findSnapshot(std::uint32_t frame) noexcept
	{
		for (Received& s : m_received)
		{
			if (s.frame == frame && frame != kNoFrame) return &s;
		}
		return nullptr;
	}

	void readSnapshot()
	{
		if (!m_codec.unpack(m_assembled, m_maxBody, m_body)) { ++m_stats.dropped; return; }
		WireReader r(m_body);
		const auto frame = static_cast<std::uint32_t>(r.get(4));
		const auto baseFrame = static_cast<std::uint32_t>(r.get(4));
		const auto hostTime = static_cast<std::uint32_t>(r.get(4));
		const auto echo = static_cast<std::uint32_t>(r.get(4));
		const auto hold = static_cast<std::uint32_t>(r.get(2));
		const int players = static_cast<int>(r.get(1));
		const auto present = static_cast<std::uint8_t>(r.get(1));
		if (!r.ok || players != m_cfg.players || frame % static_cast<std::uint32_t>(m_cfg.snapshotEvery) != 0)
		{
			++m_stats.dropped;
			return;
		}
		const Received* base = baseFrame != kNoFrame ? findSnapshot(baseFrame) : nullptr;
		if ((baseFrame != kNoFrame && base == nullptr) ||
			!m_codec.decodeImage(r, base != nullptr ? std::span<const std::uint8_t>(base->image) : std::span<const std::uint8_t>(), m_maxImage, m_image) ||
			m_image.size() < m_memorySize)
		{
			++m_stats.dropped;
			return;
		}
		Received& slot = m_received[(frame / static_cast<std::uint32_t>(m_cfg.snapshotEvery)) % kHistory];
		slot.frame = frame;
		slot.image.swap(m_image);
		++m_stats.snapshots;
		if (static_cast<int>(frame) > m_stats.newestFrame)
		{
			m_stats.newestFrame = static_cast<int>(frame);
			m_newestAtMs = m_now;
		}
		m_present = present;
		m_hostTime = hostTime;
		m_hostTimeAt = m_now;
		const auto now32 = static_cast<std::uint32_t>(m_now);
		if (echo != 0 && now32 - echo >= hold) m_rttMs = static_cast<std::uint16_t>((std::min)(now32 - echo - hold, 65535u));
	}

	/// @brief host の入力の packet。先に全部を読んで確かめてから置く (壊れた packet で正しい行を消さない)
	bool readInputs(const std::vector<std::uint8_t>& body)
	{
		WireReader r(body);
		const auto first = static_cast<std::uint32_t>(r.get(4));
		const int count = static_cast<int>(r.get(1));
		const int players = static_cast<int>(r.get(1));
		if (!r.ok || count > inputHistoryFrames(m_cfg.snapshotEvery) || players != m_cfg.players ||
			r.remaining() != static_cast<std::size_t>(count * players) * kInputRowBytes)
		{
			return false;
		}
		for (int i = 0; i < count; ++i)
		{
			InputRow& row = m_inputs[(first + static_cast<std::uint32_t>(i)) % kInputRing];
			row.frame = first + static_cast<std::uint32_t>(i);
			for (int p = 0; p < m_cfg.players; ++p) row.pads[static_cast<std::size_t>(p)] = getPadInput(r);
			for (int p = 0; p < m_cfg.players; ++p) row.seqs[static_cast<std::size_t>(p)] = static_cast<std::uint32_t>(r.get(4));
		}
		if (count > 0) m_inputFrontier = (std::max)(m_inputFrontier, static_cast<int>(first) + count);
		return true;
	}

	[[nodiscard]] const InputRow* inputsOf(int frame) const noexcept
	{
		if (frame < 0) return nullptr;
		const InputRow& row = m_inputs[static_cast<std::size_t>(frame) % kInputRing];
		return row.frame == static_cast<std::uint32_t>(frame) ? &row : nullptr;
	}

	void playout()
	{
		if (m_stats.newestFrame < 0) return;
		if (m_stats.frame < 0)
		{
			restore(*findSnapshot(static_cast<std::uint32_t>(m_stats.newestFrame)));
			return;
		}
		// 状態が落ちても入力が届いていれば、その先まで作れる
		const int frontier = (std::max)(m_stats.newestFrame, m_inputFrontier);
		const int lag = frontier - kMarginFrames - m_stats.frame;
		if (lag > kMaxLagFrames)
		{
			++m_stats.jumps;
			m_stats.interp = module::kNetInterpStalled;
			restore(*findSnapshot(static_cast<std::uint32_t>(m_stats.newestFrame)));
			m_stats.renderDelayFrames = frontier - m_stats.frame;
			return;
		}
		const int steps = lag <= 0 ? 0 : lag > 2 * m_cfg.snapshotEvery ? 2 : 1;
		m_stats.interp = steps == 2 ? module::kNetInterpCatchUp : module::kNetInterpSteady;
		for (int i = 0; i < steps && m_error.empty(); ++i)
		{
			if (!stepOnce())
			{
				++m_stats.stalls;
				m_stats.interp = module::kNetInterpStalled;
				break;
			}
		}
		m_stats.renderDelayFrames = frontier - m_stats.frame;
	}

	/// 自分の入力の番号の並び。先読みの分と、1 回の tick で状態が進んだ分 (detectCorrection は 4 フレームまで見る)
	using SeqList = std::array<std::uint32_t, kMaxPredictFrames + 4>;

	/// @brief host が frame までに使った自分の入力の番号 (まだ無ければ 0)
	[[nodiscard]] std::uint32_t usedSeq(const InputRow& row) const noexcept
	{
		const std::uint32_t s = row.seqs[static_cast<std::size_t>(m_cfg.localPlayer)];
		return s == kNoFrame ? 0u : s;
	}

	/// @brief 描く写しを作る: GameMemory を写し、host がまだ使っていない自分の入力を古い方から 1 フレーム分ずつ当てる。
	///        シミュレーションの GameMemory は host の状態のまま (次の状態で食い違いを測る対象を変えない)
	void predict()
	{
		m_stats.predictedFrames = 0;
		if (m_calls.predict == nullptr || m_stats.frame <= 0) return;
		const InputRow* row = inputsOf(m_stats.frame - 1);
		if (row == nullptr) return;
		const std::uint32_t used = usedSeq(*row);
		if (static_cast<std::int32_t>(m_seq - used) <= 0) return;
		const std::uint32_t pending = (std::min)(m_seq - used, static_cast<std::uint32_t>(kMaxPredictFrames));
		SeqList seqs{};
		for (std::uint32_t k = 0; k < pending; ++k) seqs[k] = m_seq - pending + 1 + k;
		if (!holdSide(m_sideNow)) return;
		const bool ok = runChain(m_memory, row->pads, seqs.data(), static_cast<int>(pending), m_drawMemory);
		if (!releaseSide(m_sideNow) || !ok) return;
		m_stats.predictedFrames = static_cast<int>(pending);
		++m_stats.predictions;
	}

	/// @brief base を out へ写し、自分の入力 seqs を古い方から 1 フレーム分ずつ predict に当てる。others は他の席の入力と、
	///        最初のフレームの「前の入力」(押した瞬間の判定に使う)
	bool runChain(const void* base, const std::array<PadInput, kMaxPlayers>& others, const std::uint32_t* seqs, int n,
		std::vector<std::uint8_t>& out)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(base);
		out.assign(bytes, bytes + m_memorySize);
		const auto me = static_cast<std::size_t>(m_cfg.localPlayer);
		const auto players = static_cast<std::size_t>(m_cfg.players);
		std::array<PadInput, kMaxPlayers> before = others;
		for (int k = 0; k < n; ++k)
		{
			std::array<PadInput, kMaxPlayers> now = others;
			now[me] = m_localHistory[seqs[k] % kLocalHistory];
			rollback::composeSnapshot(*m_predictSnap, std::span<const PadInput>(now.data(), players),
				std::span<const PadInput>(before.data(), players), m_cfg.keymaps, m_cfg.snapshot);
			if (!m_calls.predict(m_calls.ctx, out.data(), m_predictSnap.get(), static_cast<std::uint8_t>(me))) return false;
			before = now;
		}
		return true;
	}

	/// @brief 窓口 (Jolt の world 等) を持つゲームの predict は窓口の中を進めてよい。その前の image を取っておき、
	///        releaseSide で戻す。シミュレーションの窓口は host の状態のまま残る
	bool holdSide(std::vector<std::uint8_t>& image)
	{
		std::string why;
		return m_sides == nullptr || m_sides->capture(m_memory, true, image, &why);
	}

	bool releaseSide(const std::vector<std::uint8_t>& image)
	{
		std::string why;
		if (m_sides == nullptr || m_sides->restore(m_memory, image.data(), image.size(), &why)) return true;
		fail("先読みの後に GameMemory の外に持つ状態を戻せない: " + why);
		return false;
	}

	/// @brief 状態を進める前の、描いていた状態を取っておく (先読みが外れたかを、同じ状態から作り直して比べるため)
	void keepShownBase()
	{
		m_baseFrame = -1;
		m_baseJumps = m_stats.jumps;
		if (m_corrections == nullptr || m_calls.predict == nullptr || m_stats.frame <= 0) return;
		if (!holdSide(m_sideBase)) return;
		const auto* bytes = static_cast<const std::uint8_t*>(m_memory);
		m_baseImage.assign(bytes, bytes + m_memorySize);
		m_baseFrame = m_stats.frame;
	}

	/// @brief 先読みは「host が自分の入力を 1 フレームに 1 つずつ順に使う」と見込む。host が入力を待って前の入力を続けたり、
	///        溜まった入力を飛ばしたりすると、描く自分の位置が変わる。その時、前のフレームの見込みのまま 1 フレーム進めた写し
	///        (正す前) と、host が使った入力で作った写し (正した後) を、同じ状態 (前のフレームの状態) から作って組にする
	void detectCorrection()
	{
		if (m_stats.jumps != m_baseJumps && m_corrections != nullptr) m_corrections->snap();
		const int from = m_baseFrame, to = m_stats.frame;
		if (from <= 0 || to <= from || m_stats.jumps != m_baseJumps || to - from > 4) return;
		const InputRow* first = inputsOf(from - 1);
		if (first == nullptr) return;
		SeqList seqs{};
		bool deviated = false;
		int n = 0;
		for (int g = from; g < to; ++g)
		{
			const InputRow* row = inputsOf(g);
			const InputRow* prev = inputsOf(g - 1);
			if (row == nullptr || prev == nullptr) return;
			deviated = deviated || usedSeq(*row) != usedSeq(*prev) + 1;
			seqs[static_cast<std::size_t>(n++)] = usedSeq(*row);
		}
		const std::uint32_t start = usedSeq(*first);
		const std::uint32_t last = usedSeq(*inputsOf(to - 1));
		if (!deviated || m_seq - start > static_cast<std::uint32_t>(kMaxPredictFrames) || static_cast<std::int32_t>(m_seq - last) < 0) return;
		for (std::uint32_t q = last + 1; static_cast<std::int32_t>(m_seq - q) >= 0; ++q) seqs[static_cast<std::size_t>(n++)] = q;
		SeqList assumed{};
		const int m = static_cast<int>(m_seq - start);
		for (int k = 0; k < m; ++k) assumed[static_cast<std::size_t>(k)] = start + 1 + static_cast<std::uint32_t>(k);
		if (!holdSide(m_sideNow)) return;
		bool ok = releaseSide(m_sideBase) && runChain(m_baseImage.data(), first->pads, assumed.data(), m, m_shownBefore);
		ok = ok && releaseSide(m_sideBase) && runChain(m_baseImage.data(), first->pads, seqs.data(), n, m_shownAfter);
		if (!releaseSide(m_sideNow) || !ok) return;
		if (m_corrections->push(m_shownBefore.data(), m_shownAfter.data())) ++m_stats.corrections;
	}

	/// @brief 描く状態を 1 フレーム進める。その frame の状態が届いていれば先に合わせる。入力が無ければ次の状態へ飛ぶ
	bool stepOnce()
	{
		const int f = m_stats.frame;
		if (Received* snap = findSnapshot(static_cast<std::uint32_t>(f)); snap != nullptr && m_appliedFrame != f)
		{
			if (m_exact) checkDrift(*snap);
			restore(*snap);
		}
		const InputRow* now = inputsOf(f);
		// 入力の packet は状態より遅れて届くことがある。少しの間は待ち、長く欠けたら届いている次の状態へ飛ぶ
		if (now == nullptr) return m_stats.newestFrame - f > 2 * m_cfg.snapshotEvery + kMarginFrames && jumpForward(f);
		const InputRow* prev = inputsOf(f - 1);
		if (prev == nullptr && f > 0) m_exact = false;
		const auto n = static_cast<std::size_t>(m_cfg.players);
		const auto& before = prev != nullptr ? prev->pads : (f > 0 ? now->pads : std::array<PadInput, kMaxPlayers>{});
		rollback::composeSnapshot(*m_snap, std::span<const PadInput>(now->pads.data(), n), std::span<const PadInput>(before.data(), n),
			m_cfg.keymaps, m_cfg.snapshot);
		m_intents->reset();
		const bool ok = m_calls.update != nullptr ? m_calls.update(m_calls.ctx, m_snap.get(), m_intents.get())
		                                          : (m_update(m_memory, m_cfg.snapshot.dt, m_snap.get(), m_intents.get()), true);
		if (!ok)
		{
			fail("on_update で game が止まった");
			return false;
		}
		m_fresh = true;
		++m_stats.steps;
		arrive(f + 1);
		return true;
	}

	/// @brief 入力が欠けて作れないフレームを飛ばし、届いている次の状態へ移る (描く絵はそのフレームの数だけ飛ぶ)
	bool jumpForward(int f)
	{
		const Received* next = nullptr;
		for (const Received& s : m_received)
		{
			if (s.frame == kNoFrame || static_cast<int>(s.frame) <= f) continue;
			if (next == nullptr || s.frame < next->frame) next = &s;
		}
		if (next == nullptr) return false;
		++m_stats.jumps;
		restore(*next);
		return true;
	}

	void checkDrift(const Received& snap)
	{
		bool same = std::memcmp(m_memory, snap.image.data(), m_memorySize) == 0;
		if (same && m_sides != nullptr)
		{
			std::string why;
			same = m_sides->capture(m_memory, true, m_side, &why) && m_side.size() == snap.image.size() - m_memorySize &&
			       std::memcmp(m_side.data(), snap.image.data() + m_memorySize, m_side.size()) == 0;
		}
		if (same) return;
		++m_stats.drifts;
		if (m_stats.firstDriftFrame < 0) m_stats.firstDriftFrame = static_cast<int>(snap.frame);
	}

	void restore(const Received& snap)
	{
		std::memcpy(m_memory, snap.image.data(), m_memorySize);
		if (!rebuild()) return;
		if (m_sides != nullptr)
		{
			std::string why;
			if (!m_sides->restore(m_memory, snap.image.data() + m_memorySize, snap.image.size() - m_memorySize, &why))
			{
				fail("GameMemory の外に持つ状態を戻せない: " + why);
				return;
			}
		}
		m_appliedFrame = static_cast<int>(snap.frame);
		m_exact = true;
		arrive(static_cast<int>(snap.frame));
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

	void arrive(int frame)
	{
		m_stats.frame = frame;
		if (frame != m_watchFrame) return;
		m_watchImage.assign(static_cast<const std::uint8_t*>(m_memory), static_cast<const std::uint8_t*>(m_memory) + m_memorySize);
		std::string why;
		if (m_sides != nullptr && m_sides->capture(m_memory, true, m_side, &why)) m_watchImage.insert(m_watchImage.end(), m_side.begin(), m_side.end());
		m_watchedSum = rollback::stateChecksum(m_watchImage.data(), m_watchImage.size());
		m_watched = true;
	}

	using UpdateFn = void (*)(void*, float, const module::InputSnapshot*, module::FrameIntents*);
	UpdateFn m_update = nullptr;
	void (*m_rebuild)(void*, std::uint32_t) = nullptr;
	std::uint32_t m_memorySize = 0;
	void* m_memory = nullptr;
	AuthorityConfig m_cfg;
	DatagramEndpoint* m_net = nullptr;
	module::SideStateHost* m_sides = nullptr;
	RollbackCalls m_calls{};
	std::unique_ptr<module::FrameIntents> m_intents;
	std::unique_ptr<module::InputSnapshot> m_snap;
	std::unique_ptr<module::InputSnapshot> m_predictSnap;
	std::array<PadInput, kLocalHistory> m_localHistory{};   ///< 送った自分の入力 (番号 % kLocalHistory)
	std::vector<std::uint8_t> m_drawMemory;                 ///< 自分の分を先に進めた描画用の写し
	NetCorrectionRing* m_corrections = nullptr;
	std::vector<std::uint8_t> m_baseImage;                  ///< 状態を進める前に描いていた状態 (m_baseFrame)
	std::vector<std::uint8_t> m_shownBefore;                ///< 正す前の写し
	std::vector<std::uint8_t> m_shownAfter;                 ///< 正した後の写し
	std::vector<std::uint8_t> m_sideBase;                   ///< m_baseFrame の窓口の image
	std::vector<std::uint8_t> m_sideNow;                    ///< 先読みの前の窓口の image
	int m_baseFrame = -1;
	int m_baseJumps = 0;
	std::uint64_t m_newestAtMs = 0;
	std::array<Received, kHistory> m_received{};
	std::array<InputRow, kInputRing> m_inputs{};
	std::array<PadInput, kInputRedundancy> m_recent{};
	Reassembler m_reassembler;
	SnapshotCodec m_codec;
	std::vector<std::uint8_t> m_assembled;
	std::vector<std::uint8_t> m_body;
	std::vector<std::uint8_t> m_inputsBody;
	std::vector<std::uint8_t> m_image;
	std::vector<std::uint8_t> m_side;
	std::vector<std::uint8_t> m_out;
	std::vector<std::uint8_t> m_watchImage;
	std::size_t m_maxImage = 0;
	std::size_t m_maxBody = 0;
	AuthorityClientStats m_stats;
	std::string m_error;
	std::uint64_t m_now = 0;
	std::uint64_t m_lastHeardMs = 0;
	std::uint32_t m_seq = 0;
	std::uint32_t m_hostTime = 0;
	std::uint64_t m_hostTimeAt = 0;
	std::uint16_t m_rttMs = 0;
	std::uint8_t m_present = 0;
	int m_appliedFrame = -1;
	int m_inputFrontier = -1;   ///< 届いた入力で作れる一番先のフレーム
	bool m_exact = false;
	bool m_fresh = false;
	int m_watchFrame = -1;
	std::uint32_t m_watchedSum = 0;
	bool m_watched = false;
};

} // namespace mitiru::network::authority
