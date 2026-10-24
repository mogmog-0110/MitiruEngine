#pragma once

// MITIRU_DAWN_PROFILE=<出力先の TSV> のときだけ、区間ごとの CPU 時間を足し上げて終了時に書く診断。Tracy の見る側の道具なしで、
// 1 描画あたりや 1 フレームの内訳をその場で取る (rdtsc 1 回 20 ナノ秒ほど)。指定が無いときは区間ごとに分岐 1 つ。

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <intrin.h>
#include <windows.h>

namespace mitiru::render::dawn_detail::prof
{

struct Slot
{
	const char* name = nullptr;
	std::atomic<std::uint64_t> ticks{0};
	std::atomic<std::uint64_t> calls{0};
	Slot* next = nullptr;
};

inline bool enabled() noexcept
{
	static const bool on = std::getenv("MITIRU_DAWN_PROFILE") != nullptr;
	return on;
}

inline std::atomic<Slot*>& head() noexcept
{
	static std::atomic<Slot*> h{nullptr};
	return h;
}

inline std::atomic<std::uint64_t>& frames() noexcept
{
	static std::atomic<std::uint64_t> f{0};
	return f;
}

struct Report
{
	std::int64_t qpc0 = 0;
	std::uint64_t tsc0 = 0;
	Report()
	{
		LARGE_INTEGER q{};
		QueryPerformanceCounter(&q);
		qpc0 = q.QuadPart;
		tsc0 = __rdtsc();
	}
	~Report()
	{
		if (!enabled()) { return; }
		LARGE_INTEGER q{}, freq{};
		QueryPerformanceCounter(&q);
		QueryPerformanceFrequency(&freq);
		const double seconds = static_cast<double>(q.QuadPart - qpc0) / static_cast<double>(freq.QuadPart);
		const double usPerTick = seconds * 1e6 / static_cast<double>(__rdtsc() - tsc0);
		const double n = static_cast<double>(std::max<std::uint64_t>(frames().load(), 1));
		std::FILE* f = std::fopen(std::getenv("MITIRU_DAWN_PROFILE"), "w");
		if (f == nullptr) { return; }
		std::fprintf(f, "zone\tus_per_frame\tcalls_per_frame\tframes=%.0f\n", n);
		for (const Slot* s = head().load(); s != nullptr; s = s->next)
		{
			std::fprintf(f, "%s\t%.2f\t%.1f\n", s->name, static_cast<double>(s->ticks.load()) * usPerTick / n, static_cast<double>(s->calls.load()) / n);
		}
		std::fclose(f);
	}
};

inline Report& report() noexcept
{
	static Report r;
	return r;
}

inline void registerSlot(Slot& s, const char* name) noexcept
{
	(void)report();
	s.name = name;
	Slot* h = head().load();
	do { s.next = h; } while (!head().compare_exchange_weak(h, &s));
}

class Scope
{
public:
	Scope(Slot& slot, const char* name) noexcept : m_slot(enabled() ? &slot : nullptr)
	{
		if (m_slot == nullptr) { return; }
		if (m_slot->name == nullptr) { registerSlot(slot, name); }
		m_start = __rdtsc();
	}
	~Scope()
	{
		if (m_slot == nullptr) { return; }
		m_slot->ticks += __rdtsc() - m_start;
		++m_slot->calls;
	}
	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;

private:
	Slot* m_slot;
	std::uint64_t m_start = 0;
};

inline void countAdd(Slot& slot, const char* name, std::uint64_t n) noexcept
{
	if (slot.name == nullptr) { registerSlot(slot, name); }
	slot.calls += n;
}

inline void frameDone() noexcept
{
	if (enabled()) { ++frames(); }
}

} // namespace mitiru::render::dawn_detail::prof

#define MITIRU_DAWN_CAT2(a, b) a##b
#define MITIRU_DAWN_CAT(a, b) MITIRU_DAWN_CAT2(a, b)
#define MITIRU_DAWN_ZONE(name) \
	static ::mitiru::render::dawn_detail::prof::Slot MITIRU_DAWN_CAT(mitiruDawnSlot, __LINE__); \
	const ::mitiru::render::dawn_detail::prof::Scope MITIRU_DAWN_CAT(mitiruDawnScope, __LINE__)(MITIRU_DAWN_CAT(mitiruDawnSlot, __LINE__), name)

// 区間でなく回数を数える (calls_per_frame の欄だけが意味を持つ)
#define MITIRU_DAWN_COUNT(name, n) \
	do \
	{ \
		static ::mitiru::render::dawn_detail::prof::Slot MITIRU_DAWN_CAT(mitiruDawnCount, __LINE__); \
		if (::mitiru::render::dawn_detail::prof::enabled()) \
		{ \
			::mitiru::render::dawn_detail::prof::countAdd(MITIRU_DAWN_CAT(mitiruDawnCount, __LINE__), name, static_cast<std::uint64_t>(n)); \
		} \
	} while (false)
