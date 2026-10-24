#pragma once

/// @file BootTrace.hpp
/// @brief 起動の各段に旗を打ち、`mitiru_host --boot-trace <file>` のときだけ表にして書き出す
/// @details 旗は常に打つ (QueryPerformanceCounter 1 回と配列への書き込みだけ)。作業メモリ、プライベートバイト、
///          VRAM を旗ごとに読むのは書き出しが有効なときだけで、普通の実行の費用は旗の数 (数十個) ぶんの数十 ns に収まる。
///          時刻の基準はプロセスの作成時刻。`CreateProcess` から `main` までの OS の仕事も 1 行目で見える。

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <psapi.h>
#endif

namespace mitiru::debug::boottrace
{

inline constexpr std::size_t kMaxMarks = 192;

using VramSampler = float (*)();

/// 旗を打ったスレッドの「いま作っている段」。GPU の資源の表がこの名前で資源をまとめる
[[nodiscard]] inline const char*& currentPhase() noexcept
{
	static thread_local const char* phase = "(start)";
	return phase;
}

/// GPU の資源 1 つ分。`--boot-trace F` のときだけ `F.resources.tsv` に書く
struct ResourceRow
{
	const char*   phase = nullptr;
	std::uint64_t bytes = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t depthOrArray = 0;
	std::uint32_t mips = 0;
	std::uint32_t samples = 0;
	std::uint32_t format = 0;
	std::uint32_t flags = 0;
	std::uint32_t dimension = 0;
	std::uint32_t heap = 0;
};

struct Mark
{
	const char*   name = nullptr;
	std::int64_t  qpc = 0;
	std::uint32_t thread = 0;
	float         workingMb = -1.0f;
	float         privateMb = -1.0f;
	float         vramMb = -1.0f;
};

class Recorder
{
public:
	Recorder()
	{
#ifdef _WIN32
		LARGE_INTEGER f, c;
		QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&c);
		m_freq = f.QuadPart;
		m_qpc0 = c.QuadPart;
		FILETIME created{}, exited{}, kernel{}, user{}, now{};
		if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
		{
			GetSystemTimePreciseAsFileTime(&now);
			const auto toU64 = [](const FILETIME& t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
			const std::uint64_t d = toU64(now) - toU64(created);
			m_sinceCreateMs = static_cast<double>(d) / 10000.0;
		}
#endif
	}

	void mark(const char* name) noexcept
	{
#ifdef _WIN32
		const std::uint32_t i = m_count.fetch_add(1, std::memory_order_relaxed);
		if (i >= kMaxMarks) { return; }
		Mark& m = m_marks[i];
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		m.name = name;
		currentPhase() = name;
		m.qpc = c.QuadPart;
		m.thread = GetCurrentThreadId();
		if (m_memory.load(std::memory_order_relaxed))
		{
			PROCESS_MEMORY_COUNTERS_EX pmc{};
			pmc.cb = sizeof pmc;
			if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc))
			{
				m.workingMb = static_cast<float>(static_cast<double>(pmc.WorkingSetSize) / 1048576.0);
				m.privateMb = static_cast<float>(static_cast<double>(pmc.PrivateUsage) / 1048576.0);
			}
			if (const VramSampler s = m_vram.load(std::memory_order_acquire)) { m.vramMb = s(); }
		}
#else
		(void)name;
#endif
	}

	void enable(std::string path)
	{
		m_path = std::move(path);
		m_memory.store(true, std::memory_order_relaxed);
	}
	[[nodiscard]] bool enabled() const noexcept { return m_memory.load(std::memory_order_relaxed); }
	void setVramSampler(VramSampler s) noexcept { m_vram.store(s, std::memory_order_release); }

	void noteResource(ResourceRow row)
	{
		row.phase = currentPhase();
		const std::lock_guard<std::mutex> lock(m_resourceMutex);
		m_resources.push_back(row);
	}

	/// 有効なときだけ TSV を書く。列は経過 ms (プロセス作成から)、直前の旗との差 ms、スレッド、旗の名前、作業メモリ、プライベート、VRAM (MB)
	bool write() const
	{
		if (!enabled() || m_path.empty()) { return false; }
		const std::uint32_t n = (std::min)(m_count.load(std::memory_order_relaxed), static_cast<std::uint32_t>(kMaxMarks));
		std::vector<Mark> sorted(m_marks, m_marks + n);
		std::stable_sort(sorted.begin(), sorted.end(), [](const Mark& a, const Mark& b) { return a.qpc < b.qpc; });
		std::FILE* f = openForWrite(m_path);
		if (f == nullptr) { return false; }
		std::fputs("t_ms\td_ms\tthread\tname\tws_mb\tprivate_mb\tvram_mb\n", f);
		double prev = 0.0;
		for (const Mark& m : sorted)
		{
			const double t = m_sinceCreateMs + static_cast<double>(m.qpc - m_qpc0) * 1000.0 / static_cast<double>(m_freq);
			std::fprintf(f, "%.2f\t%.2f\t%u\t%s\t%.1f\t%.1f\t%.1f\n", t, t - prev, m.thread, m.name != nullptr ? m.name : "?",
			             m.workingMb, m.privateMb, m.vramMb);
			prev = t;
		}
		std::fclose(f);
		writeResources();
		return true;
	}

private:
	[[nodiscard]] static std::FILE* openForWrite(const std::string& path)
	{
#ifdef _WIN32
		std::FILE* f = nullptr;
		return fopen_s(&f, path.c_str(), "wb") == 0 ? f : nullptr;
#else
		return std::fopen(path.c_str(), "wb");
#endif
	}

	void writeResources() const
	{
		std::FILE* f = openForWrite(m_path + ".resources.tsv");
		if (f == nullptr) { return; }
		std::fputs("phase\tmib\twidth\theight\tdepth_or_array\tmips\tsamples\tdxgi_format\tflags\tdimension\theap\n", f);
		const std::lock_guard<std::mutex> lock(m_resourceMutex);
		for (const ResourceRow& r : m_resources)
		{
			std::fprintf(f, "%s\t%.3f\t%u\t%u\t%u\t%u\t%u\t%u\t0x%x\t%u\t%u\n", r.phase != nullptr ? r.phase : "?",
			             static_cast<double>(r.bytes) / 1048576.0, r.width, r.height, r.depthOrArray, r.mips, r.samples, r.format,
			             r.flags, r.dimension, r.heap);
		}
		std::fclose(f);
	}

	Mark                       m_marks[kMaxMarks];
	mutable std::mutex         m_resourceMutex;
	std::vector<ResourceRow>   m_resources;
	std::atomic<std::uint32_t> m_count{0};
	std::atomic<bool>          m_memory{false};
	std::atomic<VramSampler>   m_vram{nullptr};
	std::int64_t               m_freq = 1;
	std::int64_t               m_qpc0 = 0;
	double                     m_sinceCreateMs = 0.0;
	std::string                m_path;
};

[[nodiscard]] inline Recorder& recorder()
{
	static Recorder r;
	return r;
}

/// 旗を打つ。`name` は文字列リテラル (書き出すまで参照を持つ)
inline void mark(const char* name) noexcept { recorder().mark(name); }

}  // namespace mitiru::debug::boottrace
