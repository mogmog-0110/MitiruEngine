#pragma once

/// @file HostPerfLog.hpp
/// @brief --perf-log: フレームごとの CPU 時間・確保回数・GPU のパスごとの時間を 1 行ずつ TSV に書く
/// @details CPU は 2 通り出す。cpu_ms はフレームの実時間 (GPU 待ちと、他のプロセスに CPU を取られた時間を含む)、
///          busy_ms はメインスレッドが実際に走った時間 (QueryThreadCycleTime)。同じ機械で他の仕事が
///          走っていても busy_ms はあまり揺れないので、変更の前後を比べるときはこちらを見る。
///          測っている間に計測が自分で確保して数字を乱さないよう、行は最初に取った枠へ詰め、
///          ファイルへは終わってから書く。GPU の値は数フレーム遅れて届く (Dx12GpuTimer と同じ)。
///          update_ms は busy_ms のうち game の update (固定刻みの全回) に走った時間で、残りが描画の積み込みと
///          GPU への提出、巻き戻しの記録になる。tools/perf_bench.py がこれを読む。

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>
#endif

#include <mitiru/core/Engine.hpp>
#include <mitiru/debug/CountingNew.hpp>

namespace mitiru::host
{

class HostPerfLog final : public IFrameListener
{
public:
	explicit HostPerfLog(std::size_t frames)
	{
		m_rows.reserve(frames + 2);
		m_clock0 = clockPair();
	}

	/// @brief 1 つ前の呼び出しからの実時間・走った時間・確保回数を、その時点の GPU 時間とともに 1 行にする
	void onFrame(const Engine& engine)
	{
		const auto now = std::chrono::steady_clock::now();
		const std::uint64_t allocs = debug::allocationCount();
		const std::uint64_t cycles = threadCycles();
		if (m_hasLast && m_rows.size() < m_rows.capacity())
		{
			Row r;
			r.cpuMs = std::chrono::duration<double, std::milli>(now - m_last).count();
			r.busyCycles = cycles - m_lastCycles;
			r.allocs = allocs - m_lastAllocs;
			r.updateCycles = m_updateCycles;
			r.gpu = engine.gpuPassTimes();
			m_rows.push_back(r);
		}
		if (!m_hasLast) { m_startupMs = msSinceProcessStart(); }
		m_last = now;
		m_lastAllocs = allocs;
		m_lastCycles = cycles;
		m_updateCycles = 0;
		m_hasLast = true;
	}

	void onBeforeUpdate(Engine&) override { m_updateStart = threadCycles(); }
	void onAfterUpdate(Engine&) override { m_updateCycles += threadCycles() - m_updateStart; }

	/// @brief 1 行目は「# startup_ms=<プロセス開始から最初のフレームまで>」。
	///        表の列は cpu_ms, busy_ms, update_ms, allocs, gpu_ms, 続いて Engine::gpuPassNames() の順のパス
	[[nodiscard]] bool write(const std::string& path) const
	{
		std::FILE* f = std::fopen(path.c_str(), "w");
		if (f == nullptr) { return false; }
		const auto names = Engine::gpuPassNames();
		const double cyclesPerMs = cyclesPerMillisecond();
		std::fprintf(f, "# startup_ms=%.1f\n", m_startupMs);
		std::fprintf(f, "cpu_ms\tbusy_ms\tupdate_ms\tallocs\tgpu_ms");
		for (const auto& n : names) { std::fprintf(f, "\t%s", n.c_str()); }
		std::fprintf(f, "\n");
		for (const Row& r : m_rows)
		{
			const double busyMs = cyclesPerMs > 0.0 ? static_cast<double>(r.busyCycles) / cyclesPerMs : 0.0;
			const double updateMs = cyclesPerMs > 0.0 ? static_cast<double>(r.updateCycles) / cyclesPerMs : 0.0;
			std::fprintf(f, "%.4f\t%.4f\t%.4f\t%llu\t%.4f", r.cpuMs, busyMs, updateMs,
			             static_cast<unsigned long long>(r.allocs), r.gpu.totalMs);
			for (std::size_t i = 0; i < names.size(); ++i)
			{
				std::fprintf(f, "\t%.4f", (static_cast<int>(i) < r.gpu.count) ? r.gpu.ms[i] : 0.0);
			}
			std::fprintf(f, "\n");
		}
		return std::fclose(f) == 0;
	}

	[[nodiscard]] std::size_t frames() const noexcept { return m_rows.size(); }

private:
	struct ClockPair
	{
		std::uint64_t tsc = 0;
		std::int64_t qpc = 0;
	};

	struct Row
	{
		double cpuMs = 0.0;
		std::uint64_t busyCycles = 0;
		std::uint64_t updateCycles = 0;
		std::uint64_t allocs = 0;
		Engine::GpuPassTimes gpu{};
	};

	[[nodiscard]] static double msSinceProcessStart() noexcept
	{
#ifdef _WIN32
		FILETIME created{}, exited{}, kernel{}, user{}, now{};
		if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) { return 0.0; }
		GetSystemTimePreciseAsFileTime(&now);
		const auto ticks = [](const FILETIME& t) {
			return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
		};
		return static_cast<double>(ticks(now) - ticks(created)) / 10000.0;   // 100ns 単位
#else
		return 0.0;
#endif
	}

	// QueryThreadCycleTime の単位は TSC の刻み。走らせた間の TSC と QPC の進みから、1 ms あたりの刻みを出す
	[[nodiscard]] static ClockPair clockPair() noexcept
	{
		ClockPair p;
#ifdef _WIN32
		LARGE_INTEGER q{};
		QueryPerformanceCounter(&q);
		p.qpc = q.QuadPart;
		p.tsc = __rdtsc();
#endif
		return p;
	}

	[[nodiscard]] double cyclesPerMillisecond() const noexcept
	{
#ifdef _WIN32
		const ClockPair now = clockPair();
		LARGE_INTEGER freq{};
		QueryPerformanceFrequency(&freq);
		const double ms = static_cast<double>(now.qpc - m_clock0.qpc) * 1000.0 / static_cast<double>(freq.QuadPart);
		return ms > 0.0 ? static_cast<double>(now.tsc - m_clock0.tsc) / ms : 0.0;
#else
		return 0.0;
#endif
	}

	[[nodiscard]] static std::uint64_t threadCycles() noexcept
	{
#ifdef _WIN32
		ULONG64 c = 0;
		QueryThreadCycleTime(GetCurrentThread(), &c);
		return c;
#else
		return 0;
#endif
	}

	std::vector<Row> m_rows;
	std::chrono::steady_clock::time_point m_last{};
	std::uint64_t m_lastAllocs = 0;
	std::uint64_t m_lastCycles = 0;
	std::uint64_t m_updateStart = 0;
	std::uint64_t m_updateCycles = 0;
	ClockPair m_clock0{};
	double m_startupMs = 0.0;
	bool m_hasLast = false;
};

} // namespace mitiru::host
