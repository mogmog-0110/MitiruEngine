#pragma once

/// @file PoolAllocator.hpp
/// @brief 固定サイズブロックプールアロケータ
///
/// 同一サイズのオブジェクトを大量に生成・破棄する場合に最適。
/// フリーリストで空きブロックを管理し、O(1)で割り当て・解放する。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace sgc
{

/// @brief 固定サイズブロックプールアロケータ
///
/// 全ブロックが同じサイズ。フリーリストでO(1)割り当て・解放を実現する。
///
/// @code
/// sgc::PoolAllocator pool(sizeof(Particle), alignof(Particle), 1000);
///
/// auto* p = static_cast<Particle*>(pool.allocate(sizeof(Particle), alignof(Particle)));
/// new (p) Particle{...};
///
/// p->~Particle();
/// pool.deallocate(p);
/// @endcode
class PoolAllocator
{
public:
	/// @brief プールを構築する
	/// @param blockSize 1ブロックのサイズ（バイト）
	/// @param blockAlignment ブロックのアライメント
	/// @param blockCount ブロック数
	PoolAllocator(std::size_t blockSize, std::size_t blockAlignment, std::size_t blockCount)
		: m_blockSize(blockSize < sizeof(void*) ? sizeof(void*) : blockSize)
		, m_blockCount(blockCount)
		, m_freeCount(blockCount)
	{
		const auto alignedSize = alignUp(m_blockSize, blockAlignment);
		m_buffer = std::make_unique<std::byte[]>(alignedSize * blockCount);

		// フリーリストを構築する
		m_freeList = nullptr;
		for (std::size_t i = blockCount; i > 0; --i)
		{
			auto* block = reinterpret_cast<FreeBlock*>(m_buffer.get() + (i - 1) * alignedSize);
			block->next = m_freeList;
			m_freeList = block;
		}

		m_alignedBlockSize = alignedSize;
	}

	/// @brief コピー禁止
	PoolAllocator(const PoolAllocator&) = delete;
	PoolAllocator& operator=(const PoolAllocator&) = delete;

	/// @brief ムーブ可能
	PoolAllocator(PoolAllocator&&) noexcept = default;
	PoolAllocator& operator=(PoolAllocator&&) noexcept = default;

	~PoolAllocator() = default;

	/// @brief ブロックを1つ割り当てる
	/// @param size 要求サイズ（blockSize以下であること）
	/// @param alignment アライメント（無視される — 構築時に設定済み）
	/// @return 割り当てられたメモリ、空きがない場合は nullptr
	[[nodiscard]] void* allocate(std::size_t size, [[maybe_unused]] std::size_t alignment = 0)
	{
		if (size > m_blockSize || !m_freeList) return nullptr;

		auto* block = m_freeList;
		m_freeList = block->next;
		--m_freeCount;
		return block;
	}

	/// @brief ブロックをプールに返却する
	/// @param ptr 返却するメモリブロック
	void deallocate(void* ptr) noexcept
	{
		if (!ptr) return;

		auto* block = static_cast<FreeBlock*>(ptr);
		block->next = m_freeList;
		m_freeList = block;
		++m_freeCount;
	}

	/// @brief 全ブロックをリセットする
	void reset() noexcept
	{
		m_freeList = nullptr;
		m_freeCount = m_blockCount;
		for (std::size_t i = m_blockCount; i > 0; --i)
		{
			auto* block = reinterpret_cast<FreeBlock*>(m_buffer.get() + (i - 1) * m_alignedBlockSize);
			block->next = m_freeList;
			m_freeList = block;
		}
	}

	/// @brief 空きブロック数を返す
	[[nodiscard]] std::size_t freeCount() const noexcept { return m_freeCount; }

	/// @brief 総ブロック数を返す
	[[nodiscard]] std::size_t blockCount() const noexcept { return m_blockCount; }

	/// @brief 1ブロックのサイズを返す
	[[nodiscard]] std::size_t blockSize() const noexcept { return m_blockSize; }

private:
	/// @brief フリーリストのノード（未使用ブロックの先頭に重ね書きする）
	struct FreeBlock
	{
		FreeBlock* next;
	};

	/// @brief アドレスを指定アライメントに切り上げる
	[[nodiscard]] static constexpr std::size_t alignUp(std::size_t size, std::size_t alignment) noexcept
	{
		return (size + alignment - 1) & ~(alignment - 1);
	}

	std::unique_ptr<std::byte[]> m_buffer;
	FreeBlock* m_freeList = nullptr;
	std::size_t m_blockSize;
	std::size_t m_alignedBlockSize = 0;
	std::size_t m_blockCount;
	std::size_t m_freeCount;
};

} // namespace sgc
