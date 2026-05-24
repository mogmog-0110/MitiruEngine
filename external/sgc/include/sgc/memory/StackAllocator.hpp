#pragma once

/// @file StackAllocator.hpp
/// @brief LIFOスタックアロケータ
///
/// スタック（LIFO）方式でメモリを割り当てる。
/// マーカーを使って特定の位置まで巻き戻すことができる。
/// フレーム内の一時データやスコープベースのメモリ管理に最適。
///
/// @code
/// sgc::StackAllocator stack(4096);
///
/// auto marker = stack.getMarker();
/// auto* pos = stack.create<Vec3f>(1.0f, 2.0f, 3.0f);
/// auto* vel = stack.create<Vec3f>(0.0f, 0.0f, 0.0f);
///
/// stack.freeToMarker(marker);  // pos, vel を一括解放
/// @endcode

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace sgc
{

/// @brief LIFOスタックアロケータ
///
/// 固定サイズのバッファからスタック方式でメモリを割り当てる。
/// getMarker() / freeToMarker() でスコープベースの解放が可能。
class StackAllocator
{
public:
	/// @brief スタック位置マーカー型
	using Marker = std::size_t;

	/// @brief スタックアロケータを構築する
	/// @param sizeBytes バッファサイズ（バイト）
	explicit StackAllocator(std::size_t sizeBytes)
		: m_buffer(std::make_unique<std::byte[]>(sizeBytes))
		, m_capacity(sizeBytes)
		, m_offset(0)
	{
	}

	/// @brief コピー禁止
	StackAllocator(const StackAllocator&) = delete;
	StackAllocator& operator=(const StackAllocator&) = delete;

	/// @brief ムーブ可能
	StackAllocator(StackAllocator&&) noexcept = default;
	StackAllocator& operator=(StackAllocator&&) noexcept = default;

	~StackAllocator() = default;

	/// @brief アライメントを考慮してメモリを割り当てる
	/// @param size 要求サイズ（バイト）
	/// @param alignment アライメント要件
	/// @return 割り当てられたメモリへのポインタ、失敗時は nullptr
	[[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t))
	{
		const auto aligned = alignUp(m_offset, alignment);
		if (aligned + size > m_capacity) return nullptr;

		auto* ptr = m_buffer.get() + aligned;
		m_offset = aligned + size;
		return ptr;
	}

	/// @brief オブジェクトを構築して返す
	/// @tparam T 構築する型
	/// @tparam Args コンストラクタ引数の型
	/// @param args コンストラクタ引数
	/// @return 構築されたオブジェクトへのポインタ、失敗時は nullptr
	template <typename T, typename... Args>
	[[nodiscard]] T* create(Args&&... args)
	{
		auto* mem = allocate(sizeof(T), alignof(T));
		if (!mem) return nullptr;
		return new (mem) T(std::forward<Args>(args)...);
	}

	/// @brief 現在のスタック位置マーカーを取得する
	/// @return 現在のマーカー
	[[nodiscard]] Marker getMarker() const noexcept
	{
		return m_offset;
	}

	/// @brief マーカーの位置までスタックを巻き戻す
	///
	/// マーカー以降に割り当てたメモリが全て無効になる。
	///
	/// @param marker getMarker()で取得したマーカー
	void freeToMarker(Marker marker) noexcept
	{
		if (marker <= m_offset)
		{
			m_offset = marker;
		}
	}

	/// @brief 全メモリをリセットする
	void reset() noexcept
	{
		m_offset = 0;
	}

	/// @brief 使用済みバイト数を返す
	[[nodiscard]] std::size_t used() const noexcept { return m_offset; }

	/// @brief 残り利用可能バイト数を返す
	[[nodiscard]] std::size_t remaining() const noexcept { return m_capacity - m_offset; }

	/// @brief 総容量を返す
	[[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

private:
	/// @brief アドレスを指定アライメントに切り上げる
	[[nodiscard]] static constexpr std::size_t alignUp(std::size_t offset, std::size_t alignment) noexcept
	{
		return (offset + alignment - 1) & ~(alignment - 1);
	}

	std::unique_ptr<std::byte[]> m_buffer;  ///< メモリバッファ
	std::size_t m_capacity;                 ///< 総容量
	std::size_t m_offset;                   ///< 現在のスタックトップ
};

} // namespace sgc
