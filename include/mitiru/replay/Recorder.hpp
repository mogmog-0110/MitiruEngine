#pragma once

/// @file Recorder.hpp
/// @brief Frame-by-frame InputSnapshot recorder (axis 4: deterministic + replay)
/// @details
/// P4 phase の第一弾 infrastructure。1 game session の毎フレーム input を
/// append-only binary file に書き出し、後で `mitiru::replay::Player` から
/// 同じ列を再生できるようにする。
///
/// **File format (v3)**:
///
/// @code
///   header (40 bytes):
///       char[4]  magic            = "MTRR"          ; off 0
///       uint32_t version          = 3               ; off 4
///       uint32_t frameSize        = sizeof(InputSnapshot) ; off 8
///       uint32_t frameCount       = total frames    ; off 12 (seek-back at close)
///       uint64_t rngSeed          = recording seed   ; off 16
///       uint64_t recordedAtUnixMs = wall clock at open ; off 24
///       uint64_t reserved         = 0               ; off 32
///
///   frame record (variable, repeated):
///       uint32_t frameIdx
///       uint8_t  payload[frameSize]   ; raw InputSnapshot bytes
///       uint32_t stateLen             ; bytes of trailing state blob (0 = none)
///       uint8_t  state[stateLen]      ; caller-supplied game-state blob/hash
///       uint32_t checksum             ; fnv1a-32 over [frameIdx | payload | stateLen | state]
/// @endcode
///
/// v2 → v3 は後方互換なし (frame record に stateLen + state を追加した)。
/// v2/v1 file は version mismatch で graceful reject (再録画前提)。
///
/// **state blob とは**: caller (e.g. subsys_replay demo) が「自分の game state を
/// memcpy / serialize したもの」。engine は中身を一切解釈しない (汎用)。
/// これにより「同 input・異コード」で state がどの frame から分岐したかを
/// `Player::diffState()` が判定できる (axis 4 / AI 回帰判定)。
///
/// **設計判断 (ADR 0005 と整合)**:
/// - InputSnapshot は POD なので memcpy が安全
/// - checksum は frame-level の corruption / truncation を検出するためで、
///   暗号強度は不要。fnv1a-32 で十分。
/// - Header-only にしてあるので consumer は単に include するだけ。
///   テスト / dogfood で別 lib を build しなくて済む。
///
/// 関連: `include/mitiru/replay/Player.hpp` (読み出し側)

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::replay
{

/// @brief recorder/player 共通の file magic
constexpr char        kMagic[4]      = {'M', 'T', 'R', 'R'};

/// @brief recorder/player 共通の format version
constexpr std::uint32_t kFormatVersion = 3;

/// @brief recorder/player 共通の header byte size (v2: 40 bytes)
constexpr std::size_t   kHeaderBytes   = 40;

/// @brief header field offsets (single source of truth, read + write 共有)
constexpr std::size_t   kOffMagic      = 0;
constexpr std::size_t   kOffVersion    = 4;
constexpr std::size_t   kOffFrameSize  = 8;
constexpr std::size_t   kOffFrameCount = 12;
constexpr std::size_t   kOffRngSeed    = 16;
constexpr std::size_t   kOffRecordedAt = 24;
constexpr std::size_t   kOffReserved   = 32;

/// @brief fnv1a-32 starting seed (single source of truth)
constexpr std::uint32_t kFnvSeed  = 0x811c9dc5u;
constexpr std::uint32_t kFnvPrime = 0x01000193u;

/// @brief fold @p len bytes into an existing fnv1a-32 @p hash (rolling form).
/// @details lets a checksum span multiple disjoint buffers (head + state blob)
///          without first concatenating them into one contiguous array.
[[nodiscard]] inline std::uint32_t
fnv1aAppend(std::uint32_t hash, const void* data, std::size_t len) noexcept
{
	const auto* bytes = static_cast<const std::uint8_t*>(data);
	for (std::size_t i = 0; i < len; ++i)
	{
		hash ^= bytes[i];
		hash *= kFnvPrime;
	}
	return hash;
}

/// @brief fnv1a-32 over arbitrary bytes (frame checksum)
/// @details http://www.isthe.com/chongo/tech/comp/fnv/ — non-cryptographic
[[nodiscard]] inline std::uint32_t fnv1a32(const void* data, std::size_t len) noexcept
{
	return fnv1aAppend(kFnvSeed, data, len);
}

/// @brief Append-only binary recorder for `mitiru::module::InputSnapshot`.
/// @details 1 instance = 1 output file。`open()` で header を書き、
///          `record()` で 1 frame ずつ追記、`close()` で flush。
///          再 open はしない (replay session を新しく作ること)。
class Recorder
{
public:
	Recorder() = default;
	Recorder(const Recorder&)            = delete;
	Recorder& operator=(const Recorder&) = delete;
	Recorder(Recorder&&)                 = delete;
	Recorder& operator=(Recorder&&)      = delete;

	~Recorder() { close(); }

	/// @brief open output file and write header. Returns false on IO failure.
	/// @param path   output file
	/// @param rngSeed deterministic RNG seed in effect at record time. Stored
	///        in the header so replay can re-seed the same sequence (axis 4).
	bool open(const std::string& path, std::uint64_t rngSeed = 0)
	{
		close();
		m_out.open(path, std::ios::binary | std::ios::trunc);
		if (!m_out.is_open())
		{
			return false;
		}

		m_rngSeed       = rngSeed;
		m_recordedAtMs  = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());

		std::uint8_t header[kHeaderBytes] = {};
		std::memcpy(header + kOffMagic, kMagic, 4);
		const std::uint32_t ver   = kFormatVersion;
		const std::uint32_t fs    = static_cast<std::uint32_t>(sizeof(module::InputSnapshot));
		const std::uint32_t fc0   = 0;  // patched at close() via seek-back
		const std::uint64_t rsvd  = 0;
		std::memcpy(header + kOffVersion,    &ver,            sizeof(ver));
		std::memcpy(header + kOffFrameSize,  &fs,             sizeof(fs));
		std::memcpy(header + kOffFrameCount, &fc0,            sizeof(fc0));
		std::memcpy(header + kOffRngSeed,    &m_rngSeed,      sizeof(m_rngSeed));
		std::memcpy(header + kOffRecordedAt, &m_recordedAtMs, sizeof(m_recordedAtMs));
		std::memcpy(header + kOffReserved,   &rsvd,           sizeof(rsvd));
		m_out.write(reinterpret_cast<const char*>(header), kHeaderBytes);
		m_frameCount = 0;
		return m_out.good();
	}

	/// @brief append 1 frame: InputSnapshot + optional state blob.
	/// @param frameIdx   logical frame index
	/// @param snap       this frame's input snapshot (POD memcpy)
	/// @param stateBlob  caller-owned bytes describing game state at this frame
	///                   (memcpy / serialized struct, or a hash). May be null.
	/// @param stateLen   bytes in @p stateBlob. 0 = no state recorded.
	/// @details file が open でなければ no-op false。checksum は
	///          [frameIdx | payload | stateLen | state] 全体を covers するので、
	///          state の truncation / corruption も read 時に検出される。
	bool record(std::uint32_t                frameIdx,
	            const module::InputSnapshot& snap,
	            const void*                  stateBlob = nullptr,
	            std::uint32_t                stateLen  = 0)
	{
		if (!m_out.is_open()) { return false; }
		if (stateBlob == nullptr) { stateLen = 0; }

		// Layout: [frameIdx u32][payload InputSnapshot][stateLen u32][state][checksum u32]
		constexpr std::size_t kPayloadBytes = sizeof(module::InputSnapshot);
		std::uint8_t          head[sizeof(std::uint32_t) + kPayloadBytes];
		std::memcpy(head, &frameIdx, sizeof(frameIdx));
		std::memcpy(head + sizeof(frameIdx), &snap, kPayloadBytes);

		// Rolling checksum over head, then stateLen field, then state bytes.
		std::uint32_t checksum = fnv1a32(head, sizeof(head));
		checksum = fnv1aAppend(checksum, &stateLen, sizeof(stateLen));
		if (stateLen > 0)
		{
			checksum = fnv1aAppend(checksum, stateBlob, stateLen);
		}

		m_out.write(reinterpret_cast<const char*>(head), sizeof(head));
		m_out.write(reinterpret_cast<const char*>(&stateLen), sizeof(stateLen));
		if (stateLen > 0)
		{
			m_out.write(reinterpret_cast<const char*>(stateBlob), stateLen);
		}
		m_out.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
		if (!m_out.good()) { return false; }

		++m_frameCount;
		return true;
	}

	/// @brief flush + close. idempotent.
	/// @details close 時に header の frameCount field を seek-back して総数を
	///          確定上書きする (録画前は frame 数が未知なので 0 で書いてある)。
	void close()
	{
		if (m_out.is_open())
		{
			// Patch the frameCount field now that the total is known.
			const std::uint32_t fc =
				static_cast<std::uint32_t>(m_frameCount);
			m_out.seekp(static_cast<std::streamoff>(kOffFrameCount),
			            std::ios::beg);
			if (m_out.good())
			{
				m_out.write(reinterpret_cast<const char*>(&fc), sizeof(fc));
			}
			m_out.flush();
			m_out.close();
		}
	}

	/// @brief 何 frame 記録したか (debug / UI 用)
	[[nodiscard]] std::uint64_t frameCount() const noexcept { return m_frameCount; }

	/// @brief 記録した RNG seed
	[[nodiscard]] std::uint64_t rngSeed() const noexcept { return m_rngSeed; }

	/// @brief 記録時刻 (unix ms)
	[[nodiscard]] std::uint64_t recordedAt() const noexcept { return m_recordedAtMs; }

	/// @brief file が open 中か
	[[nodiscard]] bool isOpen() const noexcept { return m_out.is_open(); }

private:
	std::ofstream m_out;
	std::uint64_t m_frameCount{0};
	std::uint64_t m_rngSeed{0};
	std::uint64_t m_recordedAtMs{0};
};

}  // namespace mitiru::replay
