#pragma once

/// @file StlAllocatorAdapter.hpp
/// @brief sgcアロケータをSTLコンテナで使用するためのアダプター
///
/// sgcのカスタムアロケータ（ArenaAllocator, PoolAllocator, FreeListAllocator等）を
/// std::allocator互換にラップし、std::vector, std::list等のSTLコンテナで使用可能にする。
///
/// @note StackAllocatorはdeallocateをサポートしないため非対応。
///
/// @code
/// sgc::ArenaAllocator arena(4096);
/// auto stlAlloc = sgc::makeStlAllocator<int>(arena);
/// std::vector<int, decltype(stlAlloc)> vec(stlAlloc);
/// vec.push_back(42);
/// @endcode

#include <cstddef>
#include <limits>
#include <memory>
#include <new>

#include "sgc/memory/AllocatorConcepts.hpp"

namespace sgc
{

/// @brief sgcアロケータをstd::allocator互換にするアダプター
/// @tparam T 要素型
/// @tparam AllocatorType sgc::Allocatorコンセプトを満たすアロケータ型
///
/// STLコンテナのAllocatorテンプレート引数に渡すことで、
/// sgcのカスタムアロケータでメモリ管理できる。
///
/// @code
/// sgc::FreeListAllocator freelist(8192);
/// sgc::StlAllocatorAdapter<double, sgc::FreeListAllocator> alloc(freelist);
/// std::vector<double, decltype(alloc)> data(alloc);
/// data.push_back(3.14);
/// @endcode
template <typename T, Allocator AllocatorType>
class StlAllocatorAdapter
{
public:
	using value_type = T;

	/// @brief コンストラクタ
	/// @param alloc sgcアロケータへの参照（所有権は移動しない）
	explicit StlAllocatorAdapter(AllocatorType& alloc) noexcept
		: m_alloc(&alloc)
	{
	}

	/// @brief rebind用コピーコンストラクタ
	/// @tparam U 変換元の要素型
	/// @param other 変換元アダプター
	template <typename U>
	StlAllocatorAdapter(const StlAllocatorAdapter<U, AllocatorType>& other) noexcept
		: m_alloc(other.getAllocator())
	{
	}

	/// @brief メモリを割り当てる
	/// @param n 要素数
	/// @return 割り当てられたメモリへのポインタ
	/// @throw std::bad_alloc 割り当て失敗時
	[[nodiscard]] T* allocate(std::size_t n)
	{
		auto* ptr = m_alloc->allocate(n * sizeof(T), alignof(T));
		if (!ptr)
		{
			throw std::bad_alloc{};
		}
		return static_cast<T*>(ptr);
	}

	/// @brief メモリを解放する
	/// @param p 解放するメモリへのポインタ
	/// @param n 要素数（未使用 — sgcアロケータはサイズ不要）
	void deallocate(T* p, [[maybe_unused]] std::size_t n) noexcept
	{
		m_alloc->deallocate(static_cast<void*>(p));
	}

	/// @brief rebind構造体
	/// @tparam U 変換先の要素型
	template <typename U>
	struct rebind
	{
		using other = StlAllocatorAdapter<U, AllocatorType>;
	};

	/// @brief 等価比較（同一アロケータインスタンスを指す場合のみ等価）
	/// @param rhs 比較対象
	/// @return 同一アロケータを指すならtrue
	[[nodiscard]] bool operator==(const StlAllocatorAdapter& rhs) const noexcept
	{
		return m_alloc == rhs.m_alloc;
	}

	/// @brief 内部アロケータポインタを取得する（rebind用）
	/// @return アロケータへのポインタ
	[[nodiscard]] AllocatorType* getAllocator() const noexcept { return m_alloc; }

private:
	AllocatorType* m_alloc;  ///< sgcアロケータへのポインタ
};

/// @brief StlAllocatorAdapterを簡潔に生成するヘルパー関数
/// @tparam T 要素型
/// @tparam A sgcアロケータ型（自動推論）
/// @param alloc sgcアロケータへの参照
/// @return StlAllocatorAdapter<T, A>
///
/// @code
/// sgc::ArenaAllocator arena(4096);
/// auto alloc = sgc::makeStlAllocator<int>(arena);
/// std::vector<int, decltype(alloc)> vec(alloc);
/// @endcode
template <typename T, Allocator A>
[[nodiscard]] StlAllocatorAdapter<T, A> makeStlAllocator(A& alloc) noexcept
{
	return StlAllocatorAdapter<T, A>(alloc);
}

} // namespace sgc
