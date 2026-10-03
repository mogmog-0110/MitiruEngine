#pragma once

/// @file CountingNew.hpp
/// @brief 実行ファイルの operator new を、確保の回数を数える版に差し替える
/// @details フレームあたりの確保回数を測るためのもの (mitiru_host の --perf-log とテスト)。
///          差し替えはリンク単位で効くので、実行ファイルの 1 つの翻訳単位でだけ
///          MITIRU_DEFINE_COUNTING_NEW を展開する。数えるのはその実行ファイルに入ったコード
///          (ヘッダーだけのエンジンを含む) の確保で、ゲーム DLL が自分の new で取る分は入らない。
///          数えるのは relaxed の加算 1 回なので、常に差し替えたままでよい。

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace mitiru::debug
{

inline std::atomic<std::uint64_t> g_allocationCount{0};

/// @brief 差し替えた operator new が通った回数 (差し替えていなければ 0 のまま)
[[nodiscard]] inline std::uint64_t allocationCount() noexcept
{
	return g_allocationCount.load(std::memory_order_relaxed);
}

namespace detail
{

[[nodiscard]] inline void* alignedMalloc(std::size_t size, std::size_t align) noexcept
{
#ifdef _WIN32
	return _aligned_malloc(size, align);
#else
	return std::aligned_alloc(align, (size + align - 1) / align * align);
#endif
}

inline void alignedFree(void* p) noexcept
{
#ifdef _WIN32
	_aligned_free(p);
#else
	std::free(p);
#endif
}

[[nodiscard]] inline void* countedAlloc(std::size_t size, std::size_t align)
{
	g_allocationCount.fetch_add(1, std::memory_order_relaxed);
	if (size == 0) { size = 1; }
	for (;;)
	{
		void* p = (align == 0) ? std::malloc(size) : alignedMalloc(size, align);
		if (p != nullptr) { return p; }
		const std::new_handler handler = std::get_new_handler();
		if (handler == nullptr) { return nullptr; }
		handler();
	}
}

} // namespace detail
} // namespace mitiru::debug

// 既定の operator new[] / nothrow 版 / sized delete は、MSVC ではこの 4 つへ流れる。
// 位置合わせつきの new は位置合わせつきの delete と組なので、align が小さくても専用の確保を使う
#define MITIRU_DEFINE_COUNTING_NEW                                                                            \
	void* operator new(std::size_t size)                                                                      \
	{                                                                                                         \
		if (void* p = ::mitiru::debug::detail::countedAlloc(size, 0)) { return p; }                            \
		throw std::bad_alloc();                                                                               \
	}                                                                                                         \
	void* operator new(std::size_t size, std::align_val_t al)                                                 \
	{                                                                                                         \
		if (void* p = ::mitiru::debug::detail::countedAlloc(size, static_cast<std::size_t>(al))) { return p; } \
		throw std::bad_alloc();                                                                               \
	}                                                                                                         \
	void operator delete(void* p) noexcept { std::free(p); }                                                  \
	void operator delete(void* p, std::align_val_t) noexcept { ::mitiru::debug::detail::alignedFree(p); }
