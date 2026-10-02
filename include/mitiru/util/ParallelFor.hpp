#pragma once

/// @file ParallelFor.hpp
/// @brief [0, count) を CPU コア数ぶんのスレッドで分けて回す (オフライン変換用)
/// @details 入れ子で呼ばれた内側は直列に回す。外側がすでに全コアを使っているので、
///          内側でさらにスレッドを立てるとコア数の 2 乗まで膨らむ。
///          std::execution::par は Emscripten / TBB 無しの libstdc++ で使えないので使わない。
///          毎フレームの処理には使わないこと (呼ぶたびにスレッドを作る)。

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace mitiru::util
{

namespace detail
{

[[nodiscard]] inline bool& insideParallelFor() noexcept
{
	thread_local bool inside = false;
	return inside;
}

} // namespace detail

template <class Fn>
void parallelFor(std::size_t count, Fn&& fn)
{
	const std::size_t cores = std::max(1u, std::thread::hardware_concurrency());
	const std::size_t workers = detail::insideParallelFor() ? 1 : std::min(count, cores);
	std::atomic<std::size_t> next{0};
	const auto run = [&] {
		const bool outer = detail::insideParallelFor();
		detail::insideParallelFor() = true;
		for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) { fn(i); }
		detail::insideParallelFor() = outer;
	};
	std::vector<std::thread> pool;
	for (std::size_t k = 1; k < workers; ++k) { pool.emplace_back(run); }
	run();
	for (std::thread& t : pool) { t.join(); }
}

} // namespace mitiru::util
