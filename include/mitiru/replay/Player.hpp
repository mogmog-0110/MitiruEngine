#pragma once

/// @file Player.hpp
/// @brief フレーム単位の InputSnapshot player (axis 4: deterministic + replay)
/// @details
/// `mitiru::replay::Recorder` で書き出した append-only binary を読んで、
/// 1 frame ずつ `InputSnapshot` を取り出す。Checksum mismatch / truncation /
/// magic mismatch を検出して `eof()` / `readNext()=false` で返す。
///
/// 設計判断:
/// - header validate は `open()` で 1 度だけ
/// - frame ごとに `readNext()` を呼ぶ pull 型 API。host loop が自分の好きな
///   タイミングで読み出す
/// - failure mode (checksum / IO / EOF) を区別したいときは `lastError()` を読む
///
/// 関連: `include/mitiru/replay/Recorder.hpp` (書き込み側)

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/replay/Recorder.hpp>

namespace mitiru::replay
{

/// @brief Player の最後の I/O 失敗の分類
enum class PlayerError : std::uint8_t
{
	None             = 0,
	FileNotOpen      = 1,
	HeaderTooShort   = 2,
	MagicMismatch    = 3,
	VersionMismatch  = 4,
	FrameSizeMismatch= 5,
	FrameTruncated   = 6,
	ChecksumMismatch = 7,
	CodecUnavailable = 8   ///< v6 の圧縮済みフレームを、zstd の無い構成で読もうとした
};

/// @brief `mitiru::module::InputSnapshot` 用の append-only binary player。
/// @details 1 instance = 1 input file。`open()` で header を検証、
///          `readNext()` で 1 frame ずつ読み出し、`eof()` で終了確認。
class Player
{
public:
	Player() = default;
	Player(const Player&)            = delete;
	Player& operator=(const Player&) = delete;
	Player(Player&&)                 = delete;
	Player& operator=(Player&&)      = delete;

	~Player() { close(); }

	/// @brief open して header を検証する。false の時は `lastError()` を見ること。
	bool open(const std::string& path)
	{
		close();
		m_in.open(path, std::ios::binary);
		if (!m_in.is_open())
		{
			m_lastError = PlayerError::FileNotOpen;
			return false;
		}

		// v4/v5 共通の先頭 40 byte だけまず読む。version が分かってから残りの有無を決める
		// (v5 は envTag 分だけ header が長い、v4 はここで全部読み終わっている)。
		std::uint8_t header[kHeaderBytes] = {};
		m_in.read(reinterpret_cast<char*>(header), kHeaderBytesV4);
		if (m_in.gcount() != static_cast<std::streamsize>(kHeaderBytesV4))
		{
			m_lastError = PlayerError::HeaderTooShort;
			return false;
		}

		if (std::memcmp(header, kMagic, 4) != 0)
		{
			m_lastError = PlayerError::MagicMismatch;
			return false;
		}

		std::uint32_t ver = 0;
		std::memcpy(&ver, header + kOffVersion, sizeof(ver));
		if (ver != kFormatVersion && ver != kFormatVersionV5 && ver != kFormatVersionV4)
		{
			// 非対応 format。再録画が必要。
			m_lastError = PlayerError::VersionMismatch;
			return false;
		}

		m_version = ver;
		if (ver != kFormatVersionV4)
		{
			m_in.read(reinterpret_cast<char*>(header + kHeaderBytesV4), kHeaderBytes - kHeaderBytesV4);
			if (m_in.gcount() != static_cast<std::streamsize>(kHeaderBytes - kHeaderBytesV4))
			{
				m_lastError = PlayerError::HeaderTooShort;
				return false;
			}
			const char* tag = reinterpret_cast<const char*>(header + kOffEnvTag);
			std::size_t tagLen = 0;
			while (tagLen < kEnvTagBytes && tag[tagLen] != '\0') { ++tagLen; }
			m_envTag.assign(tag, tagLen);
		}
		else
		{
			m_envTag.clear();  // v4 録画は envTag を持たない
		}

		// header field は検証より先に全て読む。拒否時も呼び出し側が
		// recordedAbiVersion() / recordedFrameSize() で拒否理由を診断できる。
		std::memcpy(&m_frameSize,   header + kOffFrameSize,  sizeof(m_frameSize));
		std::memcpy(&m_totalFrames, header + kOffFrameCount, sizeof(std::uint32_t));
		std::memcpy(&m_rngSeed,     header + kOffRngSeed,    sizeof(m_rngSeed));
		std::memcpy(&m_recordedAt,  header + kOffRecordedAt, sizeof(m_recordedAt));
		std::memcpy(&m_recordedAbi, header + kOffAbiVersion, sizeof(m_recordedAbi));

		if (m_frameSize != sizeof(module::InputSnapshot))
		{
			// 別 ABI 世代の録画 (InputSnapshot サイズ不一致)。再録画が必要。
			m_lastError = PlayerError::FrameSizeMismatch;
			return false;
		}

		m_lastError  = PlayerError::None;
		m_eof        = false;
		m_framesRead = 0;
		m_prevSnap.assign(sizeof(module::InputSnapshot), std::uint8_t{0});
		m_packed.assign(sizeof(module::InputSnapshot), std::uint8_t{0});
		if (m_version == kFormatVersion) { m_zstd.open(); }
		return true;
	}

	/// @brief 次フレームの InputSnapshot を 1 件読む (state blob は破棄)。
	///        正常 read で true、 EOF / corruption で false。
	/// @details state trace を必要としない既存の replay path 用の薄い wrapper。
	bool readNext(module::InputSnapshot& outSnap, std::uint32_t& outFrameIdx)
	{
		std::vector<std::uint8_t> discardState;
		return readNextWithState(outSnap, discardState, outFrameIdx);
	}

	/// @brief 次フレームを InputSnapshot + state blob 込みで 1 件読む。
	///        正常 read で true、 EOF / corruption で false。
	/// @param outSnap     復号した InputSnapshot
	/// @param outState    復号した state blob バイト列 (state 無し frame なら empty)
	/// @param outFrameIdx この frame に記録された論理 frame index
	bool readNextWithState(module::InputSnapshot&     outSnap,
	                       std::vector<std::uint8_t>& outState,
	                       std::uint32_t&             outFrameIdx)
	{
		if (!m_in.is_open())
		{
			m_lastError = PlayerError::FileNotOpen;
			return false;
		}
		if (m_eof) { return false; }

		// レイアウト: [frameIdx u32][(v6) payloadLen u32][payload][stateLen u32][state][checksum u32]
		constexpr std::size_t kPayloadBytes = sizeof(module::InputSnapshot);
		std::uint8_t          head[sizeof(std::uint32_t) + kPayloadBytes];
		const ReadResult r = (m_version == kFormatVersion) ? readPackedHead(head) : readRawHead(head);
		if (r == ReadResult::CleanEof) { m_eof = true; return false; }
		if (r != ReadResult::Ok)
		{
			m_lastError = (r == ReadResult::NoCodec) ? PlayerError::CodecUnavailable : PlayerError::FrameTruncated;
			m_eof       = true;
			return false;
		}

		std::uint32_t stateLen = 0;
		m_in.read(reinterpret_cast<char*>(&stateLen), sizeof(stateLen));
		if (m_in.gcount() != static_cast<std::streamsize>(sizeof(stateLen)))
		{
			m_lastError = PlayerError::FrameTruncated;
			m_eof       = true;
			return false;
		}

		outState.resize(stateLen);
		if (stateLen > 0)
		{
			m_in.read(reinterpret_cast<char*>(outState.data()), stateLen);
			if (m_in.gcount() != static_cast<std::streamsize>(stateLen))
			{
				m_lastError = PlayerError::FrameTruncated;
				m_eof       = true;
				return false;
			}
		}

		std::uint32_t checksum = 0;
		m_in.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
		if (m_in.gcount() != static_cast<std::streamsize>(sizeof(checksum)))
		{
			m_lastError = PlayerError::FrameTruncated;
			m_eof       = true;
			return false;
		}

		// checksum は head + stateLen field + state バイト列を対象にする (Recorder と一致)。
		std::uint32_t expected = fnv1a32(head, sizeof(head));
		expected = fnv1aAppend(expected, &stateLen, sizeof(stateLen));
		if (stateLen > 0)
		{
			expected = fnv1aAppend(expected, outState.data(), stateLen);
		}
		if (expected != checksum)
		{
			m_lastError = PlayerError::ChecksumMismatch;
			m_eof       = true;
			return false;
		}

		std::memcpy(&outFrameIdx, head, sizeof(outFrameIdx));
		std::memcpy(&outSnap, head + sizeof(outFrameIdx), kPayloadBytes);
		++m_framesRead;
		return true;
	}

	/// @brief stream 終わりに達しているか (clean EOF も truncate も含む)
	[[nodiscard]] bool eof() const noexcept { return m_eof; }

	/// @brief 最後の failure 分類
	[[nodiscard]] PlayerError lastError() const noexcept { return m_lastError; }

	/// @brief 何 frame まで成功して読んだか
	[[nodiscard]] std::uint64_t framesRead() const noexcept { return m_framesRead; }

	/// @brief 記録時の RNG seed (header から)
	[[nodiscard]] std::uint64_t rngSeed() const noexcept { return m_rngSeed; }

	/// @brief header に書かれた総 frame 数 (close 時に確定された値)
	[[nodiscard]] std::uint32_t totalFrames() const noexcept { return m_totalFrames; }

	/// @brief 記録時刻 (unix ms)
	[[nodiscard]] std::uint64_t recordedAt() const noexcept { return m_recordedAt; }

	/// @brief 記録時の ABI version (kCurrentApiVersion)。0 = header に記録なし (不明)。
	///        open が FrameSizeMismatch で拒否した時のエラー表示用 (open 失敗後も読める)。
	[[nodiscard]] std::uint64_t recordedAbiVersion() const noexcept { return m_recordedAbi; }

	/// @brief header に書かれた 1 frame の InputSnapshot バイト数 (open 失敗後も読める)。
	[[nodiscard]] std::uint32_t recordedFrameSize() const noexcept { return m_frameSize; }

	/// @brief 記録環境の自由記述 (v5+ header の envTag)。v4 録画や未設定時は空文字。
	[[nodiscard]] const std::string& recordedEnvTag() const noexcept { return m_envTag; }

	/// @brief 冪等な close
	void close()
	{
		if (m_in.is_open()) { m_in.close(); }
		m_eof         = false;
		m_lastError   = PlayerError::None;
		m_framesRead  = 0;
		m_rngSeed     = 0;
		m_totalFrames = 0;
		m_recordedAt  = 0;
		m_recordedAbi = 0;
		m_frameSize   = 0;
		m_version     = 0;
		m_envTag.clear();
	}

	/// @brief file が open 中か
	[[nodiscard]] bool isOpen() const noexcept { return m_in.is_open(); }

	/// @brief 1 frame の diff 結果 (frame index + InputSnapshot 一致判定)
	struct FrameDiff
	{
		std::uint32_t frameIdx;
		bool          inputMatches;  ///< InputSnapshot が byte-equal か
	};

	/// @brief 2 つの replay file を frame ごとに比較する。
	/// @details 両 file を並走 readNext しながら、各 frame の InputSnapshot を
	///          byte-compare する。短い方の file が尽きた時点で停止する
	///          (length 差は呼び出し側が totalFrames で別途検知できる)。
	///          header 不正な file は空 vector を返す。
	[[nodiscard]] static std::vector<FrameDiff>
	diff(const std::string& pathA, const std::string& pathB)
	{
		std::vector<FrameDiff> result;
		Player a;
		Player b;
		if (!a.open(pathA) || !b.open(pathB)) { return result; }

		module::InputSnapshot snapA{};
		module::InputSnapshot snapB{};
		std::uint32_t         idxA = 0;
		std::uint32_t         idxB = 0;

		while (a.readNext(snapA, idxA) && b.readNext(snapB, idxB))
		{
			const bool eq =
				std::memcmp(&snapA, &snapB, sizeof(module::InputSnapshot)) == 0;
			result.push_back(FrameDiff{idxA, eq});
		}
		return result;
	}

	/// @brief StateDivergence で「分岐 frame なし」を表す sentinel。
	static constexpr std::uint32_t kNoDivergence =
		std::numeric_limits<std::uint32_t>::max();

	/// @brief frame 単位の state-trace 比較結果。
	/// @details 主眼は「同 input・異コード」: input が一致したまま state が
	///          分岐したら code-caused regression と判別できる。
	struct StateDivergence
	{
		bool                      diverged{false};
		std::uint32_t             firstDivergentFrame{kNoDivergence};
		std::uint32_t             totalFrames{0};
		bool                      inputMatchesAll{true}; ///< input も全 frame 一致か
		std::vector<std::uint8_t> frameMatch;            ///< 1=state match, 0=diverge
		std::vector<std::uint8_t> firstDivStateA;        ///< 分岐 frame の A state bytes
		std::vector<std::uint8_t> firstDivStateB;        ///< 分岐 frame の B state bytes
	};

	/// @brief 2 つの run の state blob を frame ごとに byte-compare する。
	/// @details 両 file を並走 readNextWithState しながら state を比較。最初に
	///          分岐した frame を記録する。短い方が尽きた時点で停止 (length 差は
	///          totalFrames で別途検知可能)。header 不正なら diverged=true,
	///          totalFrames=0 を返す (= 比較不能を呼び出し側が検知できる)。
	[[nodiscard]] static StateDivergence
	diffState(const std::string& runA, const std::string& runB)
	{
		StateDivergence out;
		Player a;
		Player b;
		if (!a.open(runA) || !b.open(runB))
		{
			out.diverged = true;  // 比較不能 → 分岐扱いにする
			return out;
		}

		module::InputSnapshot     snapA{};
		module::InputSnapshot     snapB{};
		std::vector<std::uint8_t> stateA;
		std::vector<std::uint8_t> stateB;
		std::uint32_t             idxA = 0;
		std::uint32_t             idxB = 0;

		while (a.readNextWithState(snapA, stateA, idxA) &&
		       b.readNextWithState(snapB, stateB, idxB))
		{
			const bool inputEq =
				std::memcmp(&snapA, &snapB, sizeof(module::InputSnapshot)) == 0;
			if (!inputEq) { out.inputMatchesAll = false; }

			const bool stateEq =
				(stateA.size() == stateB.size()) &&
				(stateA.empty() ||
				 std::memcmp(stateA.data(), stateB.data(), stateA.size()) == 0);

			out.frameMatch.push_back(stateEq ? 1u : 0u);

			if (!stateEq && !out.diverged)
			{
				out.diverged            = true;
				out.firstDivergentFrame = idxA;
				out.firstDivStateA      = stateA;
				out.firstDivStateB      = stateB;
			}
			++out.totalFrames;
		}
		return out;
	}

private:
	enum class ReadResult : std::uint8_t { Ok, CleanEof, Truncated, NoCodec };

	/// @brief v5 / v4: frameIdx と生の InputSnapshot をそのまま読む。
	ReadResult readRawHead(std::uint8_t* head)
	{
		constexpr std::size_t kHead = sizeof(std::uint32_t) + sizeof(module::InputSnapshot);
		m_in.read(reinterpret_cast<char*>(head), kHead);
		const auto got = m_in.gcount();
		if (got == 0) { return ReadResult::CleanEof; }
		return (got == static_cast<std::streamsize>(kHead)) ? ReadResult::Ok : ReadResult::Truncated;
	}

	/// @brief v6: frameIdx と詰めた payload を読み、前フレームとの XOR を解いて head に生の形で置く。
	ReadResult readPackedHead(std::uint8_t* head)
	{
		constexpr std::size_t kPayloadBytes = sizeof(module::InputSnapshot);
		m_in.read(reinterpret_cast<char*>(head), sizeof(std::uint32_t));
		const auto got = m_in.gcount();
		if (got == 0) { return ReadResult::CleanEof; }
		std::uint32_t payloadLen = 0;
		if (got != static_cast<std::streamsize>(sizeof(std::uint32_t))) { return ReadResult::Truncated; }
		m_in.read(reinterpret_cast<char*>(&payloadLen), sizeof(payloadLen));
		if (m_in.gcount() != static_cast<std::streamsize>(sizeof(payloadLen)) || payloadLen > kPayloadBytes)
		{
			return ReadResult::Truncated;
		}
		std::uint8_t* snap = head + sizeof(std::uint32_t);
		if (payloadLen == kPayloadBytes)
		{
			m_in.read(reinterpret_cast<char*>(snap), kPayloadBytes);
			if (m_in.gcount() != static_cast<std::streamsize>(kPayloadBytes)) { return ReadResult::Truncated; }
		}
		else
		{
			m_in.read(reinterpret_cast<char*>(m_packed.data()), payloadLen);
			if (m_in.gcount() != static_cast<std::streamsize>(payloadLen)) { return ReadResult::Truncated; }
			if (!m_zstd.available()) { return ReadResult::NoCodec; }
			if (m_zstd.decompress(m_packed.data(), payloadLen, snap, kPayloadBytes) != kPayloadBytes)
			{
				return ReadResult::Truncated;
			}
			for (std::size_t i = 0; i < kPayloadBytes; ++i) { snap[i] = static_cast<std::uint8_t>(snap[i] ^ m_prevSnap[i]); }
		}
		std::memcpy(m_prevSnap.data(), snap, kPayloadBytes);
		return ReadResult::Ok;
	}

	std::ifstream m_in;
	std::uint32_t m_version{0};
	std::vector<std::uint8_t> m_prevSnap;  ///< v6: 直前に復元した InputSnapshot (差分の元)
	std::vector<std::uint8_t> m_packed;
	util::ZstdContext         m_zstd;
	bool          m_eof{false};
	PlayerError   m_lastError{PlayerError::None};
	std::uint64_t m_framesRead{0};
	std::uint64_t m_rngSeed{0};
	std::uint32_t m_totalFrames{0};
	std::uint64_t m_recordedAt{0};
	std::uint64_t m_recordedAbi{0};  ///< header off 32 (記録時 kCurrentApiVersion、0 = 不明)
	std::uint32_t m_frameSize{0};    ///< header off 8 (記録時 sizeof(InputSnapshot))
	std::string   m_envTag;          ///< header off 40 (v5+。v4 録画は空文字)
};

}  // namespace mitiru::replay
