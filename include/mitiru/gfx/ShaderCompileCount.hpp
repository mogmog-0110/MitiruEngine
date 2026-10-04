#pragma once

/// @file ShaderCompileCount.hpp
/// @brief このプロセスがキャッシュに無いシェーダーをコンパイルした回数
/// @details コンパイルは 1 回で数十 ms かかり、そのフレームを重くする。フレーム時間の跳ねを見る検査は、
///          この数が増えたフレームを跳ねとして数えない (observe/Oracle.hpp)。

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

} // namespace mitiru::gfx
