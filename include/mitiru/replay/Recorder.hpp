#pragma once

/// @file Recorder.hpp
/// @brief フレーム単位の InputSnapshot recorder (axis 4: deterministic + replay)
/// @details
/// P4 phase の第一弾 infrastructure。1 game session の毎フレーム input を
/// append-only binary file に書き出し、後で `mitiru::replay::Player` から
/// 同じ列を再生できるようにする。
///
/// **File format (v5、envTag 追記)**:
///
/// @code
///   ヘッダー (104 byte、v5 で envTag 追加):
///       char[4]  magic            = "MTRR"          ; off 0
///       uint32_t version          = 5               ; off 4 (旧 v4 も読める、後述)
///       uint32_t frameSize        = sizeof(InputSnapshot) ; off 8
///       uint32_t frameCount       = total frames    ; off 12 (seek-back at close)
///       uint64_t rngSeed          = recording seed   ; off 16
///       uint64_t recordedAtUnixMs = wall clock at open ; off 24
///       uint64_t abiVersion       = kWireApiVersion (数値 + build 指紋) ; off 32 (v21+ で記録。旧録画は 0 = 不明)
///       char[64] envTag           = 記録環境の自由記述 (例 "dx12|NVIDIA GeForce RTX 5070 Ti|x64") ; off 40 (v5+。null 終端、余りは 0 埋め)
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
/// v4 → v5 は header 末尾に envTag を足しただけで frame record layout は不変なので、
/// `Player::open` は version 4 (40 byte header、envTag 無し) も引き続き読める
/// (`kHeaderBytesV4` で分岐)。v4 録画の `recordedEnvTag()` は空文字を返す。
///
/// **ABI 不一致録画の拒否**: ABI bump で InputSnapshot のサイズが変わると header の
/// frameSize が現 sizeof(InputSnapshot) と一致せず、Player::open が FrameSizeMismatch で
/// 拒否する (format version が同じでも再生不能 = 再録画が必要)。off 32 の abiVersion は
/// 診断用。host が拒否メッセージに記録時 ABI を表示するために読む (0 = 記録時 ABI 不明)。
///
/// **state blob とは**: caller (e.g. mitiru_host の --record) が「自分の game state を
/// memcpy / serialize したもの」。engine は中身を一切解釈しない (汎用)。
/// これにより「同 input・異コード」で state がどの frame から分岐したかを
/// `Player::diffState()` が判定できる (axis 4 / AI 回帰判定)。
///
/// **stateLen=0 の間引き記録**: 大きい GameMemory (数 MB 級) を毎フレーム丸ごと書くと
/// 録画コストが frame あたり数 ms〜十数 ms かかる。stateLen=0 は元々「state 無し」を
/// 表す合法値なので、caller が N フレームごとにしか state を書かない (mitiru_host の
/// `--record-state-every`) 運用も format 変更なしで成立する。stateLen=0 の frame は
/// `Player::diffState()` / `--replay-test` の byte 比較対象から自然に外れる
/// (recordedMem が空のまま比較条件に一致しないため)。
///
/// **設計判断**:
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

#include <mitiru/debug/TracyIntegration.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::replay
{

/// @brief recorder/player 共通の file magic
constexpr char        kMagic[4]      = {'M', 'T', 'R', 'R'};

/// @brief recorder/player 共通の format version
/// @details v3 → v4: InputSnapshot に rngSeed が増えて frameSize が変わった。
///          v4 → v5: header 末尾に envTag[64] を追記した (frame record は不変)。
///          旧 file は version / frameSize mismatch で graceful reject (再録画前提)。
constexpr std::uint32_t kFormatVersion   = 5;
/// @brief `Player::open` が引き続き読める旧 format (envTag 無し、header 40 byte)
constexpr std::uint32_t kFormatVersionV4 = 4;

/// @brief recorder/player 共通の header byte size (v5: 104 bytes)
constexpr std::size_t   kHeaderBytes     = 104;
/// @brief v4 file の header byte size (envTag が無い分だけ短い)
constexpr std::size_t   kHeaderBytesV4   = 40;
/// @brief envTag の byte 数 (null 終端込み、余りは 0 埋め)
constexpr std::size_t   kEnvTagBytes     = 64;

/// @brief header field offsets (single source of truth, read + write 共有)
constexpr std::size_t   kOffMagic      = 0;
constexpr std::size_t   kOffVersion    = 4;
constexpr std::size_t   kOffFrameSize  = 8;
constexpr std::size_t   kOffFrameCount = 12;
constexpr std::size_t   kOffRngSeed    = 16;
constexpr std::size_t   kOffRecordedAt = 24;
constexpr std::size_t   kOffAbiVersion = 32;  ///< 記録時の wire version (指紋入り、0 = 不明)。診断用
constexpr std::size_t   kOffEnvTag     = 40;  ///< 記録環境の自由記述 (v5+ のみ、v4 file には存在しない)

/// @brief fnv1a-32 starting seed (single source of truth)
constexpr std::uint32_t kFnvSeed  = 0x811c9dc5u;
constexpr std::uint32_t kFnvPrime = 0x01000193u;

/// @brief 既存の fnv1a-32 @p hash に @p len バイトを畳み込む (rolling 形式)。
/// @details 一つの連続配列に連結せずとも、複数の離れた buffer (head + state blob)
///          にまたがって checksum を計算できる。
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

/// @brief 任意バイト列の fnv1a-32 (frame checksum)
/// @details http://www.isthe.com/chongo/tech/comp/fnv/。非暗号用途
[[nodiscard]] inline std::uint32_t fnv1a32(const void* data, std::size_t len) noexcept
{
	return fnv1aAppend(kFnvSeed, data, len);
}

/// @brief `mitiru::module::InputSnapshot` 用の append-only binary recorder。
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

	/// @brief 出力 file を開いて header を書く。IO 失敗時は false を返す。
	/// @param path   出力 file
	/// @param rngSeed 記録時に有効だった deterministic RNG seed。header に保存し、
	///        replay が同じ列を re-seed できるようにする (axis 4)。
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
		const std::uint32_t fc0   = 0;  // close() で seek-back して上書きする
		const std::uint64_t abi   = module::kWireApiVersion;  // 診断用 (frameSize 不一致時の表示、指紋入り)
		std::memcpy(header + kOffVersion,    &ver,            sizeof(ver));
		std::memcpy(header + kOffFrameSize,  &fs,             sizeof(fs));
		std::memcpy(header + kOffFrameCount, &fc0,            sizeof(fc0));
		std::memcpy(header + kOffRngSeed,    &m_rngSeed,      sizeof(m_rngSeed));
		std::memcpy(header + kOffRecordedAt, &m_recordedAtMs, sizeof(m_recordedAtMs));
		std::memcpy(header + kOffAbiVersion, &abi,            sizeof(abi));
		m_out.write(reinterpret_cast<const char*>(header), kHeaderBytes);
		m_frameCount = 0;
		return m_out.good();
	}

	/// @brief header の envTag 欄を書く。`open()` 直後の 1 回だけ呼ぶ想定
	///        (host が backend/GPU を確定させたタイミングで呼べるよう、open() 自体の
	///        引数には含めていない)。64 byte を超える分は切り詰め、null 終端する。
	bool writeEnvTag(const std::string& tag)
	{
		if (!m_out.is_open()) { return false; }

		char buf[kEnvTagBytes] = {};
		const std::size_t n = tag.size() < kEnvTagBytes - 1 ? tag.size() : kEnvTagBytes - 1;
		std::memcpy(buf, tag.data(), n);

		const auto savedPos = m_out.tellp();
		m_out.seekp(static_cast<std::streamoff>(kOffEnvTag), std::ios::beg);
		m_out.write(buf, kEnvTagBytes);
		m_out.seekp(savedPos);
		return m_out.good();
	}

	/// @brief 1 frame を追記する: InputSnapshot + 任意の state blob。
	/// @param frameIdx   論理 frame index
	/// @param snap       この frame の input snapshot (POD memcpy)
	/// @param stateBlob  この frame の game state を表す caller 所有のバイト列
	///                   (memcpy / serialize した struct、または hash)。null 可。
	/// @param stateLen   @p stateBlob のバイト数。0 = state を記録しない。
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

		// 7-1: 録画中だけ Tracy のタイムラインへ mtrr フレーム番号を刻む (非録画時はここを
		// 通らないので no-op と同義。Tracy 無効ビルドでは tagMtrrFrame 自体が no-op)。
		debug::TracyHelper::tagMtrrFrame(frameIdx);

		// レイアウト: [frameIdx u32][payload InputSnapshot][stateLen u32][state][checksum u32]
		constexpr std::size_t kPayloadBytes = sizeof(module::InputSnapshot);
		std::uint8_t          head[sizeof(std::uint32_t) + kPayloadBytes];
		std::memcpy(head, &frameIdx, sizeof(frameIdx));
		std::memcpy(head + sizeof(frameIdx), &snap, kPayloadBytes);

		// head → stateLen field → state バイト列 の順で rolling checksum を取る。
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
			// 総数が確定したので frameCount field を上書きする。
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
