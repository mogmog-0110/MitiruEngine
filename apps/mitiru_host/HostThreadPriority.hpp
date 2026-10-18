#pragma once

/// @file HostThreadPriority.hpp
/// @brief フレームを回すスレッドの優先度を、普通より 1 段上げる
/// @details 同じ機械でビルドなどが全コアを使っていると、普通の優先度では他のプロセスと順番に CPU を分け合い、
///          数 ms のフレームが CPU を取られるたびに十数 ms から数百 ms 止まる。窓のある実行は vsync か 60 Hz の
///          ペーシングで大半を眠っているので、上げても他を妨げない。vsync もペーシングもなく回り続ける実行
///          (--headless、--unpaced と --no-vsync) では上げない。

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace mitiru::host
{

inline void raiseFrameThreadPriority() noexcept
{
#ifdef _WIN32
	(void)::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
}

} // namespace mitiru::host
