#pragma once

/// @file ZoneAccum.hpp
/// @brief Tracy を使わずに、ゾーンごとの CPU 時間を足し上げて終了時に表で出す計測 (-DMITIRU_ZONE_ACCUM のときだけ有効)
/// @details MITIRU_ZONE_NAMED と同じ場所を数える。1 フレームあたりの包む時間 (マイクロ秒) を、MITIRU_ZONE_REPORT で
///          指した TSV へ終了時に書く (無指定は stderr)。Tracy は見る側の道具を要るので、1 回の描画あたりや固定費の内訳を
///          その場で取る用。時計は rdtsc で、1 ゾーンあたり 20 ナノ秒ほどの費用が乗る。

#ifdef MITIRU_ZONE_ACCUM

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
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

namespace mitiru::debug::zoneaccum
{

struct Slot
{
	explicit Slot(const char* n);
	const char* name;
	std::atomic<std::uint64_t> cycles{0};
	std::atomic<std::uint64_t> calls{0};
	std::uint64_t baseCycles = 0;   ///< 最初の立ち上がりのフレームを外すための、ならし運転が済んだ時点の値
	std::uint64_t baseCalls = 0;
	bool isFrame = false;
	Slot* next = nullptr;
};

/// @brief 起動直後の数フレームは桁違いに遅い (シェーダーや資源の作成) ので、この回数のフレームを終えた時点を表の起点にする
inline constexpr std::uint64_t kWarmupFrames = 150;

inline std::atomic<Slot*>& head() noexcept
{
	static std::atomic<Slot*> h{nullptr};
	return h;
}

inline std::uint64_t now() noexcept
{
#ifdef _WIN32
	return __rdtsc();
#else
	return 0;
#endif
}

struct Clock0
{
	std::uint64_t tsc = 0;
	std::int64_t qpc = 0;
};

inline Clock0 clockNow() noexcept
{
	Clock0 c;
#ifdef _WIN32
	LARGE_INTEGER q{};
	QueryPerformanceCounter(&q);
	c.qpc = q.QuadPart;
	c.tsc = __rdtsc();
#endif
	return c;
}

inline const Clock0& clockStart() noexcept
{
	static const Clock0 c = clockNow();
	return c;
}

/// @brief 1 サイクルあたりのマイクロ秒
inline double microsPerCycle() noexcept
{
#ifdef _WIN32
	const Clock0 a = clockStart();
	const Clock0 b = clockNow();
	LARGE_INTEGER f{};
	QueryPerformanceFrequency(&f);
	const double us = static_cast<double>(b.qpc - a.qpc) * 1e6 / static_cast<double>(f.QuadPart);
	return (b.tsc > a.tsc) ? us / static_cast<double>(b.tsc - a.tsc) : 0.0;
#else
	return 0.0;
#endif
}

inline void report()
{
	std::vector<const Slot*> slots;
	for (const Slot* s = head().load(); s != nullptr; s = s->next) { slots.push_back(s); }
	std::uint64_t frames = 0;
	for (const Slot* s : slots)
	{
		if (s->isFrame) { frames = s->calls.load() - s->baseCalls; }
	}
	if (frames == 0) { frames = 1; }
	const double upc = microsPerCycle();
	const char* path = std::getenv("MITIRU_ZONE_REPORT");
	std::FILE* f = (path != nullptr && path[0] != '\0') ? std::fopen(path, "a") : stderr;
	if (f == nullptr) { f = stderr; }
	std::fprintf(f, "zone\tcalls_per_frame\tus_per_frame\tus_per_call\n");
	for (const Slot* s : slots)
	{
		const double calls = static_cast<double>(s->calls.load() - s->baseCalls);
		const double us = static_cast<double>(s->cycles.load() - s->baseCycles) * upc;
		std::fprintf(f, "%s\t%.3f\t%.3f\t%.4f\n", s->name, calls / static_cast<double>(frames), us / static_cast<double>(frames),
		             calls > 0 ? us / calls : 0.0);
	}
	if (f != stderr) { std::fclose(f); }
}

inline Slot::Slot(const char* n) : name(n), isFrame(std::strcmp(n, "Engine::Frame") == 0)
{
	static const bool installed = [] {
		(void)clockStart();
		std::atexit([] { report(); });
		return true;
	}();
	(void)installed;
	next = head().exchange(this);
}

/// @brief ならし運転が済んだ時点の値を、全ゾーンの起点にする
inline void markBase() noexcept
{
	for (Slot* s = head().load(); s != nullptr; s = s->next)
	{
		s->baseCycles = s->cycles.load(std::memory_order_relaxed);
		s->baseCalls = s->calls.load(std::memory_order_relaxed);
	}
}

/// @brief 呼び出し 1 回ぶんの時間を slot に足す
struct Scope
{
	explicit Scope(Slot& s) noexcept : m_slot(s), m_t0(now()) {}
	~Scope()
	{
		m_slot.cycles.fetch_add(now() - m_t0, std::memory_order_relaxed);
		const std::uint64_t calls = m_slot.calls.fetch_add(1, std::memory_order_relaxed) + 1;
		if (m_slot.isFrame && calls == kWarmupFrames) { markBase(); }
	}
	Scope(const Scope&) = delete;
	Scope& operator=(const Scope&) = delete;

private:
	Slot& m_slot;
	std::uint64_t m_t0;
};

/// @brief パスの切れ目ごとに CPU 時間を足す。mark(名前) の次の mark(または end) までが、その名前のパスの時間
class PassClock
{
public:
	void mark(const char* name) noexcept
	{
		const std::uint64_t t = now();
		close(t);
		m_slot = slotFor(name);
		m_start = t;
	}
	void markWide(const wchar_t* name)
	{
		const std::uint64_t t = now();
		close(t);
		for (auto& e : m_wslots)
		{
			if (e.first == name) { m_slot = e.second; m_start = t; return; }
		}
		std::string narrow;
		for (const wchar_t* c = name; *c != 0; ++c) { narrow.push_back(static_cast<char>(*c)); }
		m_names.push_back("pass:" + narrow);
		Slot* s = new Slot(m_names.back().c_str());
		m_wslots.emplace_back(name, s);
		m_slot = s;
		m_start = t;
	}
	void end() noexcept { close(now()); }

private:
	void close(std::uint64_t t) noexcept
	{
		if (m_slot == nullptr) { return; }
		m_slot->cycles.fetch_add(t - m_start, std::memory_order_relaxed);
		m_slot->calls.fetch_add(1, std::memory_order_relaxed);
		m_slot = nullptr;
	}

	Slot* slotFor(const char* name)
	{
		for (auto& e : m_slots)
		{
			if (e.first == name) { return e.second; }
		}
		m_names.push_back("pass:" + std::string(name));
		Slot* s = new Slot(m_names.back().c_str());
		m_slots.emplace_back(name, s);
		return s;
	}

	Slot* m_slot = nullptr;
	std::uint64_t m_start = 0;
	std::vector<std::pair<const char*, Slot*>> m_slots;
	std::vector<std::pair<const wchar_t*, Slot*>> m_wslots;
	std::deque<std::string> m_names;
};

inline PassClock& passClock()
{
	static thread_local PassClock c;
	return c;
}

} // namespace mitiru::debug::zoneaccum

#define MITIRU_ZONE_ACCUM_CAT2(a, b) a##b
#define MITIRU_ZONE_ACCUM_CAT(a, b) MITIRU_ZONE_ACCUM_CAT2(a, b)
#define MITIRU_ZONE_ACCUM_SCOPE(name)                                                              \
	static ::mitiru::debug::zoneaccum::Slot MITIRU_ZONE_ACCUM_CAT(mitiruZoneSlot_, __LINE__){name}; \
	const ::mitiru::debug::zoneaccum::Scope MITIRU_ZONE_ACCUM_CAT(mitiruZoneScope_, __LINE__){      \
		MITIRU_ZONE_ACCUM_CAT(mitiruZoneSlot_, __LINE__)}

#endif // MITIRU_ZONE_ACCUM
