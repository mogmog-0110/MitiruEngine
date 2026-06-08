#pragma once

/// @file ScrubControlChannel.hpp
/// @brief inspector (observer) → host への time-travel scrub 逆チャネル
/// @details
/// `SharedSnapshot.hpp` の forward channel (game→inspector, `mitiru_inspector_<pid>.json`)
/// に対応する reverse channel。inspector の time-travel graph を click した時に
/// host の GameMemory を「その frame の過去 bytes」へ巻き戻す click-to-scrub を実現する。
///
/// 位置づけ (ADR 0005 / 0017 との整合):
/// - これは **debug 専用の observer→host file IPC** であって gameplay signal flow では
///   ない。host を介さず、SharedSnapshot と同じ temp file 流儀で完結する。
/// - rewind は **host が** 行う (live GameMemory を過去 bytes で memcpy 上書き)。game
///   DLL は scrub を一切知らない (ADR 0005 の純関数モデルを保つ)。reader は host 側。
/// - 単調増加 `seq` が前回より進んでいる command を一度だけ適用する (重複適用を防ぐ)。
///
/// wire format (`mitiru_control_<pid>.json` — file 名は互換のため据え置き):
/// @code
///   { "scrubTo": 123, "seq": 5 }   // scrubTo = offsetFromNewest (0 = 最新)
/// @endcode
///
/// 使い方 (inspector 側 / writer):
/// @code
///   mitiru::observe::ScrubControlWriter w{producerPid};
///   w.write({{"scrubTo", offset}, {"seq", ++mySeq}});
/// @endcode
///
/// 使い方 (host 側 / reader):
/// @code
///   mitiru::observe::ScrubControlReader r;  // 自プロセス pid
///   if (auto j = r.poll()) {
///       const long seq = j->value("seq", 0);
///       if (seq > lastSeq) { lastSeq = seq; scrubOffset = j->value("scrubTo", 0); }
///   }
/// @endcode

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>   // getpid
#endif

namespace mitiru::observe
{

/// @brief scrub control file の場所を組み立てる (`%TEMP%/mitiru_control_<pid>.json`)
/// @note file 名は旧 ControlChannel と互換 (既存ツール窓を壊さない)。
inline std::filesystem::path scrubControlPathForPid(int pid)
{
	const std::string name = "mitiru_control_" + std::to_string(pid) + ".json";
	return std::filesystem::temp_directory_path() / name;
}

namespace detail
{
inline int scrubThisPid()
{
#ifdef _WIN32
	return _getpid();
#else
	return ::getpid();
#endif
}
}  // namespace detail

/// @brief inspector 側 — 監視対象 pid の scrub control file に command を書く (writer)
/// @details atomic rename pattern (`*.tmp` に書いて rename)。host が midway read で
///          truncated JSON を観測しないようにする。
class ScrubControlWriter
{
public:
	explicit ScrubControlWriter(int producerPid)
		: m_path(scrubControlPathForPid(producerPid)),
		  m_tmpPath(m_path.string() + ".tmp")
	{
	}

	/// @brief command を書き出す (atomic rename)
	/// @return 書き込み成功で true。エラーは silent (UI loop を壊さないため)
	bool write(const nlohmann::json& payload)
	{
		try
		{
			{
				std::ofstream out(m_tmpPath, std::ios::binary | std::ios::trunc);
				if (!out) { return false; }
				out << payload.dump();
			}
			std::error_code ec;
			std::filesystem::rename(m_tmpPath, m_path, ec);
			if (ec)
			{
				// rename 失敗 (例: Windows でファイル使用中)。unlink + retry。
				std::filesystem::remove(m_path, ec);
				std::filesystem::rename(m_tmpPath, m_path, ec);
				if (ec) { return false; }
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	/// @brief scrub control file の絶対パス
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
	std::filesystem::path m_path;
	std::filesystem::path m_tmpPath;
};

/// @brief host 側 — 自プロセス宛の scrub control file を polling 読み (reader)
/// @details mtime が前回読みより新しい時だけ parse して返す。filesystem だけに依存する
///          header-only 実装。
class ScrubControlReader
{
public:
	/// @brief 自プロセスの pid に紐づいた control file を読む reader を作る
	ScrubControlReader()
		: m_path(scrubControlPathForPid(detail::scrubThisPid()))
	{
	}

	/// @brief 任意 pid の control file を読む reader を作る (テスト用)
	explicit ScrubControlReader(int pidOverride)
		: m_path(scrubControlPathForPid(pidOverride))
	{
	}

	/// @brief 最新の command を試し読みする。mtime が前回読みより新しい時だけ返す
	[[nodiscard]] std::optional<nlohmann::json> poll()
	{
		std::error_code ec;
		if (!std::filesystem::exists(m_path, ec)) { return std::nullopt; }

		const auto mt = std::filesystem::last_write_time(m_path, ec);
		if (ec) { return std::nullopt; }
		if (m_haveLastMtime && mt == m_lastMtime) { return std::nullopt; }

		try
		{
			std::ifstream in(m_path, std::ios::binary);
			if (!in) { return std::nullopt; }
			auto j = nlohmann::json::parse(in);
			m_lastMtime     = mt;
			m_haveLastMtime = true;
			return j;
		}
		catch (...)
		{
			// rename 途中の race / truncated read。次 tick で再試行する。
			return std::nullopt;
		}
	}

	/// @brief scrub control file の絶対パス
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

private:
	std::filesystem::path           m_path;
	std::filesystem::file_time_type m_lastMtime{};
	bool                            m_haveLastMtime{false};
};

}  // namespace mitiru::observe
