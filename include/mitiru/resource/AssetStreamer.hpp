#pragma once

/// @file AssetStreamer.hpp
/// @brief 資産の読み込みを 2 段に分け、重い前半をワーカースレッドで、登録の後半をメインスレッドで走らせる
/// @details 前半 (AssetWork) はファイルの読み込み、解析、画像の圧縮、GPU の転送の記録までをする。返した
///          AssetFinish はメインスレッドの pump / settle / settleAll だけが呼ぶので、描画側の表は排他なしで書ける。
///          終わる順はスレッドの都合で変わる。GameMemory に結果を書く処理は渡さない (リプレイが合わなくなる)。
///          headless の撮影とリプレイの照合は settleAll を毎フレームの頭で呼び、読み終わった時刻を絵に出さない。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <mitiru/core/JobSystem.hpp>

namespace mitiru::resource
{

/// @brief メインスレッドで呼ぶ後半。空なら何もしない
using AssetFinish = std::function<void()>;
/// @brief ワーカーで走る前半。例外は失敗として数え、後半は呼ばない
using AssetWork = std::function<AssetFinish()>;

struct AssetStreamerStats
{
	std::uint32_t pending = 0;     ///< 頼まれてまだ後半を終えていない数 (前半が走っている物を含む)
	std::uint64_t finished = 0;    ///< 後半まで終えた数
	std::uint64_t failed = 0;      ///< 前半が例外を投げた数
	std::uint64_t cancelled = 0;   ///< 取り消して後半を捨てた数
	double        lastPumpMs = 0;  ///< 直前の pump が後半に使った時間
	double        maxFinishMs = 0; ///< 後半 1 件にかかった最大の時間
};

class AssetStreamer
{
public:
	/// @param workers 0 なら hardware_concurrency の 1/3 (1〜4)。物理とメインスレッドの分を残す
	explicit AssetStreamer(std::size_t workers = 0) : m_jobs(resolveWorkers(workers)) {}

	AssetStreamer(const AssetStreamer&) = delete;
	AssetStreamer& operator=(const AssetStreamer&) = delete;
	AssetStreamer(AssetStreamer&&) = delete;
	AssetStreamer& operator=(AssetStreamer&&) = delete;

	/// @brief 走っている前半を待ってから壊す。後半は呼ばずに捨てる
	~AssetStreamer() { m_jobs.waitAll(); }

	/// @return 同じ key がまだ終わっていなければ false (頼み直さない)
	bool request(std::string key, AssetWork work)
	{
		if (isPending(key)) { return false; }
		Entry e;
		e.key = std::move(key);
		e.result = m_jobs.submit([fn = std::move(work)]() -> AssetFinish { return fn(); });
		m_entries.push_back(std::move(e));
		return true;
	}

	[[nodiscard]] bool isPending(std::string_view key) const noexcept
	{
		return std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.key == key; });
	}

	[[nodiscard]] std::size_t pendingCount() const noexcept { return m_entries.size(); }

	/// @brief 前半を終えた物の後半を頼んだ順に呼ぶ。budgetMs を使い切ったら残りは次の回。0 でも 1 件は進める
	/// @return 後半を呼んだ数
	std::size_t pump(double budgetMs)
	{
		const auto start = Clock::now();
		std::size_t done = 0;
		for (std::size_t i = 0; i < m_entries.size();)
		{
			if (done > 0 && elapsedMs(start) >= budgetMs) { break; }
			if (!isReady(m_entries[i])) { ++i; continue; }
			finishAt(i);
			++done;
		}
		m_stats.lastPumpMs = elapsedMs(start);
		return done;
	}

	/// @brief key の前半を待って後半を呼ぶ。描く直前に結果が要るとき (headless の決定的な描画) に使う
	/// @return key を頼んでいなければ false
	bool settle(std::string_view key)
	{
		for (std::size_t i = 0; i < m_entries.size(); ++i)
		{
			if (m_entries[i].key != key) { continue; }
			m_entries[i].result.wait();
			finishAt(i);
			return true;
		}
		return false;
	}

	/// @brief 全部の前半を待ち、後半を頼んだ順に呼ぶ。後半が新しく頼んだ物も終えてから返る
	/// @return 後半を呼んだ数
	std::size_t settleAll()
	{
		std::size_t done = 0;
		while (!m_entries.empty())
		{
			m_entries.front().result.wait();
			finishAt(0);
			++done;
		}
		return done;
	}

	/// @brief 前半は止められないので、終わったときに後半を捨てる。手放した資産の読み込みが後から登録されないようにする
	/// @return key を頼んでいれば true
	bool cancel(std::string_view key)
	{
		for (auto& e : m_entries)
		{
			if (e.key == key) { e.cancelled = true; return true; }
		}
		return false;
	}

	/// @brief 頼んだ物を全部取り消す。装置を壊す前に呼び、settleAll で前半が終わるのを待つ
	void cancelAll() noexcept
	{
		for (auto& e : m_entries) { e.cancelled = true; }
	}

	[[nodiscard]] AssetStreamerStats stats() const noexcept
	{
		AssetStreamerStats s = m_stats;
		s.pending = static_cast<std::uint32_t>(m_entries.size());
		return s;
	}

	[[nodiscard]] std::size_t workerCount() const noexcept { return m_jobs.workerCount(); }

private:
	using Clock = std::chrono::steady_clock;

	struct Entry
	{
		std::string              key;
		std::future<AssetFinish> result;
		bool                     cancelled = false;
	};

	[[nodiscard]] static bool isReady(const Entry& e)
	{
		return e.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	[[nodiscard]] static double elapsedMs(Clock::time_point since) noexcept
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
	}

	/// @brief 表から外してから後半を呼ぶ。後半が request しても添字がずれない
	void finishAt(std::size_t i)
	{
		Entry e = std::move(m_entries[i]);
		m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
		AssetFinish finish;
		try
		{
			finish = e.result.get();
		}
		catch (const std::exception&)
		{
			++m_stats.failed;
			return;
		}
		if (e.cancelled)
		{
			++m_stats.cancelled;
			return;
		}
		const auto start = Clock::now();
		if (finish) { finish(); }
		m_stats.maxFinishMs = std::max(m_stats.maxFinishMs, elapsedMs(start));
		++m_stats.finished;
	}

	[[nodiscard]] static std::size_t resolveWorkers(std::size_t requested) noexcept
	{
		if (requested > 0) { return requested; }
		const std::size_t hw = std::thread::hardware_concurrency();
		return std::clamp<std::size_t>(hw / 3, 1, 4);
	}

	std::vector<Entry> m_entries;
	AssetStreamerStats m_stats;
	JobSystem          m_jobs;   ///< 最後に壊す。走っている前半が終わるのを待つ
};

} // namespace mitiru::resource
