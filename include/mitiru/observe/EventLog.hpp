#pragma once

/// @file EventLog.hpp
/// @brief append-only JSONL event timeline — dual-readable (window + AI)
/// @details
/// `SharedSnapshot.hpp` が「現フレームの state」を 1 ファイルに上書きするのに対し、
/// EventLog は「時間軸に沿った疎な節目イベント」を `%TEMP%/mitiru_events_<pid>.jsonl`
/// に **append-only JSONL** (1 行 1 event) で蓄積する。
///
/// 二重可読 (dual-readable) の思想:
/// - 人間/inspector: `mitiru_inspector --panel events` が tail-poll で読んで描画
/// - AI agent: `tail -f` / Read で生 JSONL をそのまま機械可読として処理可能
///
/// 設計判断 (SharedSnapshot の作法に倣う):
/// - temp file: shared memory より構造化が簡単、`cat` で覗ける、OS が掃除する
/// - 1 ファイル 1 run: `open()` で truncate して fresh start (前 run の残骸を混ぜない)
/// - flush 必須: AI が即 tail で読めるよう、emit ごとに明示 flush
/// - file open は 1 回だけ (hot path で呼ばれても open syscall を撒かない)
/// - event は疎なはず (被弾 / 死亡 / 違反 等の節目) なので throttle はしない
///
/// wire format (1 行 = 1 JSON object):
/// @code
///   {"frame":120,"t":2.01,"type":"hit","data":{"dmg":10,"hp_after":90}}
///   {"frame":121,"t":2.03,"type":"enemy_death","data":{"x":640.0,"y":120.0}}
/// @endcode
///
/// 使い方 (gameplay 側):
/// @code
///   mitiru::observe::EventLog log;
///   log.open(GetCurrentProcessId());
///   // each gameplay milestone:
///   log.emit(frame, "hit", {{"dmg", 10}, {"hp_after", mem.hp}});
/// @endcode

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

namespace mitiru::observe
{

/// @brief event log 用 temp file の場所を組み立てる (`%TEMP%/mitiru_events_<pid>.jsonl`)
inline std::filesystem::path eventLogPathForPid(int pid)
{
	const std::string name = "mitiru_events_" + std::to_string(pid) + ".jsonl";
	return std::filesystem::temp_directory_path() / name;
}

/// @brief 走ってる process 側 (writer)。append-only JSONL を蓄積する。
/// @details RAII: デストラクタでストリームを閉じる。ファイルは inspector / AI が
///          run 後も読めるよう残す (SharedSnapshot のように消さない — 履歴だから)。
class EventLog
{
public:
	EventLog() = default;

	~EventLog()
	{
		if (m_out.is_open()) { m_out.close(); }
	}

	EventLog(const EventLog&)            = delete;
	EventLog& operator=(const EventLog&) = delete;

	/// @brief pid に紐づいた JSONL を開く (truncate, fresh per run)。1 回だけ呼ぶ。
	/// @return open 成功で true。失敗は silent でも分かるよう bool で返す。
	bool open(int pid)
	{
		m_pid  = pid;
		m_path = eventLogPathForPid(pid);
		// trunc: fresh per run — 前回の残骸を混ぜない
		m_out.open(m_path, std::ios::binary | std::ios::trunc);
		m_startTime = std::chrono::steady_clock::now();
		return m_out.is_open();
	}

	/// @brief 1 event を JSONL として append + flush する。
	/// @param frame  gameplay frame counter (時系列軸)
	/// @param type   event 種別 ("hit" / "enemy_death" / "invariant_violation" 等)
	/// @param data   event 固有のペイロード (任意 JSON)
	/// @details flush は AI が tail で即読めるための必須要件。エラーは silent
	///          (hot path / poll loop を壊さない)。
	void emit(std::uint32_t frame, std::string_view type, const nlohmann::json& data)
	{
		if (!m_out.is_open()) { return; }
		try
		{
			const double t = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - m_startTime).count();
			nlohmann::json line = {
				{"frame", frame},
				{"t",     std::round(t * 1000.0) / 1000.0},  // ms 精度で十分
				{"type",  std::string(type)},
				{"data",  data},
			};
			m_out << line.dump() << '\n';
			m_out.flush();  // AI が tail -f で即読めるよう毎回 flush
		}
		catch (...)
		{
			// JSON dump / write 失敗は無視 (gameplay を止めない)
		}
	}

	/// @brief このログが開けているか
	[[nodiscard]] bool isOpen() const noexcept { return m_out.is_open(); }

	/// @brief JSONL ファイルの絶対パス
	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	/// @brief writer の pid
	[[nodiscard]] int pid() const noexcept { return m_pid; }

	/// @brief inspector 側 — 任意 pid の JSONL を mtime-poll で tail 読みする (reader)
	/// @details 直近 N 行のみ保持。SharedSnapshot::Reader と同じ mtime 判定で、
	///          変化が無ければ何もしない。途中 write の torn line は parse 失敗で
	///          skip する (append + flush なので部分行は次 poll で完成して読める)。
	class Reader
	{
	public:
		struct Event
		{
			std::uint32_t  frame{0};
			double         t{0.0};
			std::string    type;
			nlohmann::json data;
		};

		explicit Reader(int producerPid)
			: m_path(eventLogPathForPid(producerPid))
		{
		}

		explicit Reader(std::filesystem::path path)
			: m_path(std::move(path))
		{
		}

		/// @brief 最新の全 event を読み直し、直近 maxKeep 件を返す。
		/// @details mtime が前回読みと同じなら何もせず false を返す (再描画不要)。
		///          全文を読み直すのは JSONL がせいぜい数百行と疎なため十分軽い。
		/// @return ファイルが更新されて再読込した場合 true。
		bool poll(std::size_t maxKeep = 64)
		{
			std::error_code ec;
			if (!std::filesystem::exists(m_path, ec)) { return false; }

			const auto mt = std::filesystem::last_write_time(m_path, ec);
			if (ec) { return false; }
			if (m_haveLastMtime && mt == m_lastMtime) { return false; }

			std::vector<Event> parsed;
			try
			{
				std::ifstream in(m_path, std::ios::binary);
				if (!in) { return false; }
				std::string line;
				while (std::getline(in, line))
				{
					if (line.empty()) { continue; }
					try
					{
						auto j = nlohmann::json::parse(line);
						Event e;
						e.frame = j.value("frame", 0u);
						e.t     = j.value("t", 0.0);
						e.type  = j.value("type", std::string{});
						e.data  = j.value("data", nlohmann::json::object());
						parsed.push_back(std::move(e));
					}
					catch (...)
					{
						// torn last line (mid-write) — skip; completes next poll
					}
				}
			}
			catch (...)
			{
				return false;
			}

			m_lastMtime     = mt;
			m_haveLastMtime = true;

			// 直近 maxKeep 件のみ保持 (UI band を超える分は捨てる)
			if (parsed.size() > maxKeep)
			{
				m_events.assign(parsed.end() - static_cast<std::ptrdiff_t>(maxKeep),
				                parsed.end());
			}
			else
			{
				m_events = std::move(parsed);
			}
			return true;
		}

		/// @brief 直近 poll で読み込んだ event 列 (oldest first)
		[[nodiscard]] const std::vector<Event>& events() const noexcept { return m_events; }

		[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

	private:
		std::filesystem::path           m_path;
		std::filesystem::file_time_type m_lastMtime{};
		bool                            m_haveLastMtime{false};
		std::vector<Event>              m_events;
	};

private:
	int                   m_pid{0};
	std::filesystem::path m_path;
	std::ofstream         m_out;
	std::chrono::steady_clock::time_point m_startTime{};
};

}  // namespace mitiru::observe
