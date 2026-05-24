#pragma once

/// @file AllocatorConcepts.hpp
/// @brief アロケータに関するconceptsとユーティリティ型

#include <concepts>
#include <cstddef>

namespace sgc
{

/// @brief アロケータの基本要件を満たす型
///
/// allocate()でメモリを確保し、deallocate()で解放できる型にマッチする。
template <typename A>
concept Allocator = requires(A alloc, std::size_t size, std::size_t alignment, void* ptr)
{
	{ alloc.allocate(size, alignment) } -> std::same_as<void*>;
	{ alloc.deallocate(ptr) };
};

/// @brief リセット可能なアロケータ
///
/// reset()で確保済みメモリを一括解放できるアロケータにマッチする。
/// アリーナアロケータ等で使用。
template <typename A>
concept ResettableAllocator = Allocator<A> && requires(A alloc)
{
	{ alloc.reset() };
};

} // namespace sgc
