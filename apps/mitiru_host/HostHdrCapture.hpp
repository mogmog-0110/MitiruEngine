#pragma once

/// @file HostHdrCapture.hpp
/// @brief `--capture-hdr` で、`--capture-dir` の PNG と同じ連番の線形 HDR を PFM（float32 RGB）で書く（ADR 0148）。

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mitiru::host
{

/// @brief rgb は行 0 が上で、要素数は width * height * 3。PFM に合わせて下の行から書き、負のスケールで little endian を表す。
[[nodiscard]] inline bool writePfm(const std::string& path, const std::vector<float>& rgb, int width, int height)
{
	if (width <= 0 || height <= 0 || rgb.size() != static_cast<std::size_t>(width) * height * 3u) { return false; }
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) { return false; }
	bool ok = std::fprintf(f, "PF\n%d %d\n-1.0\n", width, height) > 0;
	const std::size_t rowFloats = static_cast<std::size_t>(width) * 3u;
	for (int y = height - 1; ok && y >= 0; --y)
	{
		ok = std::fwrite(rgb.data() + static_cast<std::size_t>(y) * rowFloats, sizeof(float), rowFloats, f) == rowFloats;
	}
	return (std::fclose(f) == 0) && ok;
}

/// @brief PFM の書き込みを 1 本のワーカースレッドへ移すキュー (AsyncPngWriter と同じ形)。HDR の 1 コマは 11 MB あり、
///        積み過ぎてメモリを食わないよう、kMaxPending 枚を超えたら enqueue が待つ。破棄時は残りを書き切ってから join する。
class AsyncPfmWriter
{
public:
	static constexpr std::size_t kMaxPending = 4;

	AsyncPfmWriter() : m_worker([this] { workerLoop(); }) {}
	AsyncPfmWriter(const AsyncPfmWriter&) = delete;
	AsyncPfmWriter& operator=(const AsyncPfmWriter&) = delete;

	~AsyncPfmWriter()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_cv.notify_all();
		if (m_worker.joinable()) { m_worker.join(); }
	}

	void enqueue(std::string path, std::vector<float> rgb, int width, int height)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_space.wait(lock, [this] { return m_queue.size() < kMaxPending; });
		m_queue.push(Job{std::move(path), std::move(rgb), width, height});
		m_cv.notify_all();
	}

	[[nodiscard]] bool hadError() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_hadError;
	}

private:
	struct Job
	{
		std::string path;
		std::vector<float> rgb;
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
				if (m_queue.empty()) { return; }   // m_stopping で、積んだ分は書き切った
				job = std::move(m_queue.front());
				m_queue.pop();
				m_space.notify_all();
			}
			if (!writePfm(job.path, job.rgb, job.width, job.height))
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_hadError = true;
			}
		}
	}

	mutable std::mutex m_mutex;
	std::condition_variable m_cv;
	std::condition_variable m_space;
	std::queue<Job> m_queue;
	bool m_stopping = false;
	bool m_hadError = false;
	std::thread m_worker;   // 他メンバの構築が終わってから起動するので最後に宣言する
};

}  // namespace mitiru::host
