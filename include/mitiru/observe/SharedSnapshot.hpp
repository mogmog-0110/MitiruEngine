#pragma once

/// @file SharedSnapshot.hpp
/// @brief 別プロセス inspector との 1-writer / N-reader IPC primitive
/// @details
/// engine の差別化軸 5 (modular sub-window architecture) の foundation。
/// 走ってる gameplay process がフレーム毎に JSON state を temp file に書き出し、
/// 別 process (`mitiru_inspector` 等) が polling で読んで自分のウィンドウに描画する。
///
/// 設計判断:
/// - CEF multi-process を使わない: V8 proxy resolver の single-process 制約に
///   引っかからない、独立 Win32 window が自然に取れる、multi-monitor が無料で効く
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

#include <cstdint>
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

/// @brief snapshot 用 temp file の場所を組み立てる
inline std::filesystem::path sharedSnapshotPathForPid(int pid)
{
	const std::string name = "mitiru_inspector_" + std::to_string(pid) + ".json";
	return std::filesystem::temp_directory_path() / name;
}

/// @brief 走ってる process 側 (writer)
/// @details コンストラクタが自プロセスの pid を取って temp file パスを決める。
class SharedSnapshot
{
public:
	/// @brief 自プロセスの pid に紐づいた snapshot ファイルを書く writer を作る
	SharedSnapshot()
		: m_pid(thisPid()),
		  m_path(sharedSnapshotPathForPid(m_pid)),
		  m_tmpPath(m_path.string() + ".tmp")
	{
	}

	/// @brief 任意のキーに紐づいた snapshot ファイルを書く writer を作る (テスト用)
	explicit SharedSnapshot(int pidOverride)
		: m_pid(pidOverride),
		  m_path(sharedSnapshotPathForPid(m_pid)),
		  m_tmpPath(m_path.string() + ".tmp")
	{
	}

	~SharedSnapshot()
	{
		// Best-effort cleanup. Reader may still be polling; if so they'll just
		// observe nullopt next tick, which is fine.
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
	}

	SharedSnapshot(const SharedSnapshot&) = delete;
	SharedSnapshot& operator=(const SharedSnapshot&) = delete;

	/// @brief 現フレームの snapshot を書き出す (atomic rename pattern)
	/// @return 書き込み成功で true。エラーは silent (poll-loop を壊さないため)
	bool write(const nlohmann::json& payload)
	{
		try
		{
			{
				std::ofstream out(m_tmpPath, std::ios::binary | std::ios::trunc);
				if (!out) { return false; }
				// dump(0) でコンパクト。inspector 側で開いて grep したいなら
				// dump(2) に切り替え可能だが size が 5x になるので default は compact。
				out << payload.dump();
			}
			std::error_code ec;
			std::filesystem::rename(m_tmpPath, m_path, ec);
			if (ec)
			{
				// rename failed (e.g. file in use on Windows). Try unlink + rename.
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

	/// @brief このプロセスの pid (snapshot ファイル名に使われた値)
	[[nodiscard]] int pid() const noexcept { return m_pid; }

	/// @brief snapshot ファイルの絶対パス
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	/// @brief inspector 側 — 任意 pid の snapshot ファイルを polling 読み
	class Reader
	{
	public:
		explicit Reader(int producerPid)
			: m_path(sharedSnapshotPathForPid(producerPid))
		{
		}

		/// @brief 最新の snapshot を試し読みする。mtime が前回読みより新しい時だけ返す
		[[nodiscard]] std::optional<nlohmann::json> tryRead()
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
				m_lastMtime = mt;
				m_haveLastMtime = true;
				return j;
			}
			catch (...)
			{
				// Mid-rename race or truncated read; try again next tick.
				return std::nullopt;
			}
		}

		[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	private:
		std::filesystem::path             m_path;
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
};

}  // namespace mitiru::observe
