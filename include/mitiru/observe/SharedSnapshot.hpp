#pragma once

/// @file SharedSnapshot.hpp
/// @brief 別プロセス inspector との 1-writer / N-reader IPC primitive
/// @details
/// engine の差別化軸 5 (modular sub-window architecture) の foundation。
/// 走ってる gameplay process がフレーム毎に JSON state を temp file に書き出し、
/// 別 process (`mitiru_inspector` 等) が polling で読んで自分のウィンドウに描画する。
///
/// 設計判断:
/// - 別 process にする: 独立 Win32 window が自然に取れる、multi-monitor が無料で効く
/// - shared memory ではなく temp file: 構造化簡単、開発時に `cat` で生 JSON を
///   覗ける、process が殺されても残骸は OS が temp 掃除する時に消える
/// - lock-free write: 完全 atomic rename pattern (`*.tmp` に書いて rename)
///   reader は midway read で truncated JSON を見ない。POSIX / NTFS 両対応
/// - sentinel pid: 同一マシンで複数 game 走ってても衝突しない
///
/// 使い方 (gameplay 側):
/// @code
///   mitiru::observe::SharedSnapshot snap;  // 自動で <temp>/mitiru_inspector_<pid>.json
///   // each frame:
///   nlohmann::json st = ...;
///   snap.write(st);
/// @endcode
///
/// 使い方 (inspector 側):
/// @code
///   mitiru::observe::SharedSnapshot::Reader r{producer_pid};
///   if (auto j = r.tryRead()) { ... }  // nullopt if no fresh data
/// @endcode

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

#include <mitiru/debug/TracyZones.hpp>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>   // getpid
#endif

namespace mitiru::observe
{

/// @brief snapshot 用 temp file の場所を組み立てる
inline std::filesystem::path sharedSnapshotPathForPid(int pid)
{
	const std::string name = "mitiru_inspector_" + std::to_string(pid) + ".json";
	return std::filesystem::temp_directory_path() / name;
}

/// @brief 読み手がいることを書き手へ知らせる印のファイル (snapshot と同じ場所の `.watch`)
inline std::filesystem::path sharedSnapshotWatchPath(const std::filesystem::path& snapshot)
{
	return std::filesystem::path(snapshot.string() + ".watch");
}

/// @brief 読み手側。読みに来ている間、印のファイルの更新時刻を進める
/// @details 書き手は印が新しい間だけ snapshot を組み立てて書く。読み手がいないゲーム (出荷した
///          ゲームや、ツール窓を開いていない普段の実行) が毎秒 10 回ファイルを書かずに済む。
class SnapshotWatchBeacon
{
public:
	explicit SnapshotWatchBeacon(const std::filesystem::path& snapshot) : m_path(sharedSnapshotWatchPath(snapshot)) {}

	/// @brief 何度呼んでもよい。実際に触るのは kInterval に 1 回
	/// @param haveRead 読み手が snapshot を 1 度でも読めたか。変わった時は待たずに印の中身へ `read` と書く。
	///        host は中身を見ない。ツール窓が読み始めたかを、外 (E2E や AI) から確かめるための口
	void touch(bool haveRead = false)
	{
		const auto now = std::chrono::steady_clock::now();
		const bool readChanged = haveRead != m_haveRead;
		if (!readChanged && m_touched && now - m_last < kInterval) { return; }
		m_touched = true;
		m_last = now;
		std::error_code ec;
		if (readChanged)
		{
			// 開けなかった時 (他のプロセスが印を触っている瞬間) は、次の touch で書き直す
			std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
			if (out && (out << (haveRead ? "read" : "")).flush()) { m_haveRead = haveRead; }
		}
		else if (!std::filesystem::exists(m_path, ec)) { std::ofstream(m_path, std::ios::binary | std::ios::app); }
		std::filesystem::last_write_time(m_path, std::filesystem::file_time_type::clock::now(), ec);
	}

	static constexpr std::chrono::milliseconds kInterval{500};

private:
	std::filesystem::path m_path;
	std::chrono::steady_clock::time_point m_last{};
	bool m_touched = false;
	bool m_haveRead = false;
};

/// @brief 実行中の process 側 (writer)
/// @details コンストラクタが自プロセスの pid を取って temp file パスを決める。
class SharedSnapshot
{
public:
	/// @brief 自プロセスの pid に紐づいた snapshot ファイルを書く writer を作る
	SharedSnapshot()
		: SharedSnapshot(thisPid())
	{
	}

	/// @brief 任意のキーに紐づいた snapshot ファイルを書く writer を作る (テスト用)
	/// @details 残っている snapshot は、同じ pid を使って殺されたゲームが書いたもの。窓が古いゲームの値を
	///          読まないよう消す
	explicit SharedSnapshot(int pidOverride)
		: m_pid(pidOverride),
		  m_path(sharedSnapshotPathForPid(m_pid)),
		  m_tmpPath(m_path.string() + ".tmp"),
		  m_watchPath(sharedSnapshotWatchPath(m_path))
	{
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
	}

	~SharedSnapshot()
	{
		// best-effort cleanup。Reader がまだ polling 中でも、次 tick で nullopt を
		// 観測するだけなので問題ない。
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
		std::filesystem::remove(m_watchPath, ec);
	}

	/// @brief 読み手が最近 (kReaderTimeout 以内に) 印を触ったか。ファイルを見るのは kReaderCheckInterval に 1 回
	/// @details 書き手はこれが false の間、snapshot を組み立てない。読み手が来れば次の確認で書き始める。
	[[nodiscard]] bool hasReader()
	{
		const auto now = std::chrono::steady_clock::now();
		if (m_readerChecked && now - m_readerCheckedAt < kReaderCheckInterval) { return m_hasReader; }
		m_readerChecked = true;
		m_readerCheckedAt = now;
		std::error_code ec;
		const auto touched = std::filesystem::last_write_time(m_watchPath, ec);
		m_hasReader = !ec && std::filesystem::file_time_type::clock::now() - touched < kReaderTimeout;
		return m_hasReader;
	}

	static constexpr std::chrono::milliseconds kReaderCheckInterval{250};
	static constexpr std::chrono::seconds kReaderTimeout{3};

	SharedSnapshot(const SharedSnapshot&) = delete;
	SharedSnapshot& operator=(const SharedSnapshot&) = delete;

	/// @brief 現フレームの snapshot を書き出す (atomic rename pattern)
	/// @return 書き込み成功で true。エラーは silent (poll-loop を妨げないため)
	/// @details 直前に書いた内容と一致するフレームは ofstream open + rename を丸ごと
	///          省略する (C11)。GameMemory が変化しないフレームは珍しくなく、
	///          disk I/O の方が dump() より支配的なコストだったため。
	bool write(const nlohmann::json& payload)
	{
		MITIRU_ZONE_NAMED("observe::SharedSnapshot::write");
		try
		{
			std::string serialized = payload.dump();
			if (m_hasLast && serialized == m_lastDump) { return true; }

			{
				std::ofstream out(m_tmpPath, std::ios::binary | std::ios::trunc);
				if (!out) { return false; }
				out << serialized;
			}
			std::error_code ec;
			std::filesystem::rename(m_tmpPath, m_path, ec);
			if (ec)
			{
				// rename 失敗 (例: Windows でファイル使用中)。unlink + rename を試す。
				std::filesystem::remove(m_path, ec);
				std::filesystem::rename(m_tmpPath, m_path, ec);
				if (ec) { return false; }
			}
			m_lastDump = std::move(serialized);  // 次回比較用に再利用 (再確保を避ける)
			m_hasLast  = true;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	/// @brief このプロセスの pid (snapshot ファイル名に使われた値)
	[[nodiscard]] int pid() const noexcept { return m_pid; }

	/// @brief snapshot ファイルの絶対パス
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	/// @brief inspector 側。任意 pid の snapshot ファイルを polling で読む
	class Reader
	{
	public:
		explicit Reader(int producerPid)
			: m_path(sharedSnapshotPathForPid(producerPid)),
			  m_beacon(m_path)
		{
		}

		/// @brief 最新の snapshot を試し読みする。mtime が前回読みより新しい時だけ返す
		[[nodiscard]] std::optional<nlohmann::json> tryRead()
		{
			m_beacon.touch();
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
				m_lastMtime = mt;
				m_haveLastMtime = true;
				return j;
			}
			catch (...)
			{
				// rename 途中の race / truncated read。次 tick で再試行する。
				return std::nullopt;
			}
		}

		[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	private:
		std::filesystem::path             m_path;
		SnapshotWatchBeacon               m_beacon;
		std::filesystem::file_time_type   m_lastMtime{};
		bool                              m_haveLastMtime{false};
	};

private:
	static int thisPid()
	{
#ifdef _WIN32
		return _getpid();
#else
		return ::getpid();
#endif
	}

	int                    m_pid;
	std::filesystem::path  m_path;
	std::filesystem::path  m_tmpPath;
	std::filesystem::path  m_watchPath;
	std::string            m_lastDump;
	bool                   m_hasLast{false};
	std::chrono::steady_clock::time_point m_readerCheckedAt{};
	bool                   m_readerChecked{false};
	bool                   m_hasReader{false};
};

}  // namespace mitiru::observe
