#pragma once

/// @file WorkerRegistry.hpp
/// @brief DLL が持つバックグラウンドのスレッドを、host が DLL を外す前に止めるための登録簿。
/// @details スレッドが走っている間、MSVC のランタイムは DLL を載せたままにする。host の FreeLibrary は参照を 1 つ減らすだけで、
///          状態を引き継ぐホットリロードでは旧 DLL の on_shutdown も mitiru_module_unload も呼ばれないので、DLL もスレッドも
///          入れ替えのたびに 1 組ずつ残っていた。そこで、スレッドを持つ部品 (FluidWorkers など) は作るときにここへ止め方を登録する。
///          host は ModuleHost::unload() の FreeLibrary の直前に、DLL が出す `mitiru_module_stop_workers` (MITIRU_GAME 系のマクロが
///          自動で出す) を呼び、その中が stopRegisteredWorkers() になる。
///
///          登録簿は DLL ごとに別の実体になる (inline 関数の static)。host の exe と DLL がそれぞれのスレッドだけを止める。
///          GameMemory には何も書かない。

#include <cstddef>
#include <mutex>
#include <vector>

namespace mitiru::module
{

/// @brief 止める関数。ctx は登録のときに渡した値。host のスレッドから呼ばれ、何度呼ばれても安全であること
using WorkerStopFn = void (*)(void* ctx);

namespace detail
{

struct WorkerStopEntry
{
	void* ctx;
	WorkerStopFn fn;
	bool once;   ///< true なら 1 回呼んだら登録を外す (DLL の固定を解くなど、2 度目が要らない物)
};

struct WorkerRegistry
{
	std::mutex mutex;
	std::vector<WorkerStopEntry> entries;
};

/// 解放しない。プロセスの終了時に static のデストラクターの順序で、登録簿が先に消えて後から外す側が触れないことを避けるため
[[nodiscard]] inline WorkerRegistry& workerRegistry()
{
	static WorkerRegistry* const registry = new WorkerRegistry();
	return *registry;
}

} // namespace detail

/// @brief 止め方を登録する。同じ (ctx, fn) の組を 2 度登録しても 1 つにまとめ、once は後の指定にする。
///        登録も解除も止める呼びも、host の主スレッドからだけ呼ぶこと (止める関数が壊す物を、別のスレッドが同時に外さない前提)
inline void registerWorkerStop(void* ctx, WorkerStopFn fn, bool once = false)
{
	if (fn == nullptr) { return; }
	auto& r = detail::workerRegistry();
	const std::lock_guard<std::mutex> lock(r.mutex);
	for (detail::WorkerStopEntry& e : r.entries)
	{
		if (e.ctx == ctx && e.fn == fn) { e.once = once; return; }
	}
	r.entries.push_back({ctx, fn, once});
}

/// @brief 登録を外す。登録されていなければ何もしない。登録した物を壊す前に呼ぶ
inline void unregisterWorkerStop(void* ctx, WorkerStopFn fn) noexcept
{
	auto& r = detail::workerRegistry();
	const std::lock_guard<std::mutex> lock(r.mutex);
	for (std::size_t i = 0; i < r.entries.size(); ++i)
	{
		if (r.entries[i].ctx == ctx && r.entries[i].fn == fn)
		{
			r.entries.erase(r.entries.begin() + static_cast<std::ptrdiff_t>(i));
			return;
		}
	}
}

/// @brief 登録された止め方を、登録と逆の順に全部呼ぶ。呼ぶ間は鍵を持たないので、止める側が登録を外してもよい
inline void stopRegisteredWorkers() noexcept
{
	auto& r = detail::workerRegistry();
	std::vector<detail::WorkerStopEntry> snapshot;
	{
		const std::lock_guard<std::mutex> lock(r.mutex);
		snapshot = r.entries;
		for (std::size_t i = r.entries.size(); i > 0; --i)
		{
			if (r.entries[i - 1].once) { r.entries.erase(r.entries.begin() + static_cast<std::ptrdiff_t>(i - 1)); }
		}
	}
	for (std::size_t i = snapshot.size(); i > 0; --i)
	{
		try { snapshot[i - 1].fn(snapshot[i - 1].ctx); }
		catch (...) {}
	}
}

/// @brief 登録の数 (試験用)
[[nodiscard]] inline std::size_t registeredWorkerStopCount()
{
	auto& r = detail::workerRegistry();
	const std::lock_guard<std::mutex> lock(r.mutex);
	return r.entries.size();
}

} // namespace mitiru::module
