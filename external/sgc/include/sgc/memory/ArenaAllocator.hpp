#pragma once

/// @file ArenaAllocator.hpp
/// @brief リニアアリーナアロケータ
///
/// 固定サイズのバッファから線形にメモリを割り当てる。
/// 個別のdeallocateは不可。reset()で一括解放する。
/// フレーム単位の一時メモリに最適。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace sgc
{

/// @brief リニアアリーナアロケータ
///
/// 事前に確保したバッファから順番にメモリを切り出す。
/// 個別解放はできないが、reset()で全体を一括リセットできる。
///
/// @code
/// sgc::ArenaAllocator arena(4096);  // 4KBのアリーナ
///
/// auto* pos = arena.create<Vec3f>(1.0f, 2.0f, 3.0f);
/// auto* vel = arena.create<Vec3f>(0.0f, 0.0f, 0.0f);
///
/// arena.reset();  // 全メモリを一括リセット
/// @endcode
class ArenaAllocator
{
public:
	/// @brief アリーナを構築する
	/// @param sizeBytes バッファサイズ（バイト）
	explicit ArenaAllocator(std::size_t sizeBytes)
		: m_buffer(std::make_unique<std::byte[]>(sizeBytes))
		, m_capacity(sizeBytes)
		, m_offset(0)
	{
	}

	/// @brief コピー禁止
	ArenaAllocator(const ArenaAllocator&) = delete;
	ArenaAllocator& operator=(const ArenaAllocator&) = delete;

	/// @brief ムーブ可能
	ArenaAllocator(ArenaAllocator&&) noexcept = default;
	ArenaAllocator& operator=(ArenaAllocator&&) noexcept = default;

	~ArenaAllocator() = default;

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

	/// @brief 個別解放は行わない（アリーナアロケータの特性）
	void deallocate(void*) noexcept
	{
		// アリーナアロケータは個別解放しない
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

	/// @brief 全メモリをリセットする（全オブジェクトが無効になる）
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

	std::unique_ptr<std::byte[]> m_buffer;
	std::size_t m_capacity;
	std::size_t m_offset;
};

} // namespace sgc
