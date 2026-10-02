#pragma once

/// @file PreciseWait.hpp
/// @brief フレームの刻みに合わせて待つ。OS のタイマー刻みへの切り上げを避ける

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mitiru::util
{

/// @brief deadline まで待つ。sleep_until は Windows では 15.6 ms 刻みに切り上がるので、
///        高分解能の待機タイマーで手前まで眠り、残りの短い間だけ回して待つ
inline void waitUntil(std::chrono::steady_clock::time_point deadline)
{
	using clock = std::chrono::steady_clock;
	constexpr auto spinMargin = std::chrono::microseconds(500);
	const auto now = clock::now();
	if (now >= deadline) { return; }
	const auto sleepFor = deadline - now - spinMargin;
#ifdef _WIN32
	if (sleepFor > clock::duration::zero())
	{
		HANDLE timer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
		                                        TIMER_ALL_ACCESS);
		if (timer != nullptr)
		{
			LARGE_INTEGER due{};
			due.QuadPart = -static_cast<LONGLONG>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(sleepFor).count() / 100);
			if (::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
			{
				::WaitForSingleObject(timer, INFINITE);
			}
			::CloseHandle(timer);
		}
		else
		{
			std::this_thread::sleep_for(sleepFor);  // 高分解能タイマーの無い古い Windows
		}
	}
#else
	if (sleepFor > clock::duration::zero()) { std::this_thread::sleep_for(sleepFor); }
#endif
	while (clock::now() < deadline) { std::this_thread::yield(); }
}

} // namespace mitiru::util
