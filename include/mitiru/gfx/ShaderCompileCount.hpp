#pragma once

/// @file ShaderCompileCount.hpp
/// @brief このプロセスがキャッシュに無いシェーダーをコンパイルした回数と、描画・計算のパイプラインを作った回数
/// @details コンパイルは 1 回で数十 ms かかり、そのフレームを重くする。フレーム時間の跳ねを見る検査は、
///          この数が増えたフレームを跳ねとして数えない (observe/Oracle.hpp)。
///          パイプラインの作成は、最初の使用でフレームが止まる原因になる (perf_bench の「最初の使用」の列)。
///          DX12 の作成は 89 か所に散るので、数えるのは Dx12PipelineCreateCounter.hpp が device の関数表で行う。

#include <atomic>
#include <cstdint>

namespace mitiru::gfx
{

[[nodiscard]] inline std::atomic<std::uint64_t>& shaderCompileCounter() noexcept
{
	static std::atomic<std::uint64_t> n{0};
	return n;
}

inline void noteShaderCompiled() noexcept
{
	shaderCompileCounter().fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] inline std::uint64_t shaderCompileCount() noexcept
{
	return shaderCompileCounter().load(std::memory_order_relaxed);
}

[[nodiscard]] inline std::atomic<std::uint64_t>& pipelineCreateCounter() noexcept
{
	static std::atomic<std::uint64_t> n{0};
	return n;
}

inline void notePipelineCreated() noexcept
{
	pipelineCreateCounter().fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] inline std::uint64_t pipelineCreateCount() noexcept
{
	return pipelineCreateCounter().load(std::memory_order_relaxed);
}

/// @brief パイプラインの作成にかかった時間の合計 (ナノ秒)。同期で作る DX12 だけが入る (Dawn は作業スレッドで作るので呼び出しの時間が実態を表さない)
[[nodiscard]] inline std::atomic<std::uint64_t>& pipelineCreateNanosCounter() noexcept
{
	static std::atomic<std::uint64_t> n{0};
	return n;
}

inline void notePipelineCreateNanos(std::uint64_t nanos) noexcept
{
	pipelineCreateNanosCounter().fetch_add(nanos, std::memory_order_relaxed);
}

[[nodiscard]] inline std::uint64_t pipelineCreateNanos() noexcept
{
	return pipelineCreateNanosCounter().load(std::memory_order_relaxed);
}

/// @brief スキンの変形の出力を作った回数 (DX12)。段が替わっても作り直さないことを試験が見る
[[nodiscard]] inline std::atomic<std::uint64_t>& skinTargetCreateCounter() noexcept
{
	static std::atomic<std::uint64_t> n{0};
	return n;
}

inline void noteSkinTargetCreated() noexcept
{
	skinTargetCreateCounter().fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] inline std::uint64_t skinTargetCreateCount() noexcept
{
	return skinTargetCreateCounter().load(std::memory_order_relaxed);
}

} // namespace mitiru::gfx
