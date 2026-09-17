#pragma once

/// @file AsyncPngWriter.hpp
/// @brief `--capture-dir` の PNG 書き出しをバックグラウンドスレッドへ逃がす (G7)。
/// @details `savePixelsToPng` は zlib 圧縮を伴うため軽くない。host の capture ループが
///          これを同期で待つと host frame 自体が重くなる。書き込みをキュー+専用スレッドへ
///          逃がし、呼び出し側は `std::vector<uint8_t>` の所有権を渡すだけにする。

#include <mitiru/render/SaveScreenshotPng.hpp>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace mitiru::render
{

/// @brief RGBA8 フレームを 1 本のワーカースレッドへ渡して非同期に PNG 保存するキュー。
/// @details enqueue() は即座に返る (呼び出し元スレッドをブロックしない)。破棄時は
///          残りキューを最後まで書き切ってから join する (撮影を尻切れにしない)。
class AsyncPngWriter
{
public:
	AsyncPngWriter() : m_worker([this] { workerLoop(); }) {}

	AsyncPngWriter(const AsyncPngWriter&) = delete;
	AsyncPngWriter& operator=(const AsyncPngWriter&) = delete;

	~AsyncPngWriter()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_cv.notify_one();
		if (m_worker.joinable()) { m_worker.join(); }
	}

	/// @brief フレームをキューへ積む。`rgba` の所有権はこの呼び出しで writer 側へ移る。
	void enqueue(std::string path, std::vector<std::uint8_t> rgba, int width, int height)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queue.push(Job{std::move(path), std::move(rgba), width, height});
		}
		m_cv.notify_one();
	}

	/// @brief 直近の書き込み失敗があったか (host は初回のみ報告して spam を避ける想定)。
	[[nodiscard]] bool hadError() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_hadError;
	}

	/// @brief 最後に失敗したパス (無ければ空文字列)。
	[[nodiscard]] std::string lastErrorPath() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_lastErrorPath;
	}

	/// @brief 未処理キュー数 (診断用: 撮影が書き込み速度に追いつけているか)。
	[[nodiscard]] std::size_t pending() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

private:
	struct Job
	{
		std::string path;
		std::vector<std::uint8_t> rgba;
		int width = 0;
		int height = 0;
	};

	void workerLoop()
	{
		for (;;)
		{
			Job job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
				if (m_queue.empty())
				{
					if (m_stopping) { return; }
					continue;
				}
				job = std::move(m_queue.front());
				m_queue.pop();
			}
			const bool ok = savePixelsToPng(job.rgba.data(), job.width, job.height, job.path);
			if (!ok)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_hadError = true;
				m_lastErrorPath = job.path;
			}
		}
	}

	mutable std::mutex m_mutex;
	std::condition_variable m_cv;
	std::queue<Job> m_queue;
	bool m_stopping = false;
	bool m_hadError = false;
	std::string m_lastErrorPath;
	std::thread m_worker;  ///< 他メンバの構築完了後に起動する必要があるため最後に宣言する
};

}  // namespace mitiru::render
