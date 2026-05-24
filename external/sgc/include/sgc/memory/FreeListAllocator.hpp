#pragma once

/// @file FreeListAllocator.hpp
/// @brief フリーリストアロケータ（可変サイズブロック対応）
///
/// 可変サイズのメモリブロックを割り当て・解放する。
/// 解放時に隣接する空きブロックを結合（コアレス）する。
/// FirstFit/BestFit 検索戦略を選択可能。
///
/// @code
/// sgc::FreeListAllocator alloc(4096, sgc::FitPolicy::FirstFit);
///
/// auto* a = alloc.create<int>(42);
/// auto* b = alloc.create<float>(3.14f);
///
/// alloc.deallocate(a);
/// alloc.deallocate(b);
/// @endcode

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace sgc
{

/// @brief フリーリストの検索戦略
enum class FitPolicy
{
	FirstFit,  ///< 最初に見つかった十分なブロックを使用
	BestFit    ///< 最も小さい十分なブロックを使用（断片化を抑制）
};

/// @brief フリーリストアロケータ
///
/// 可変サイズのメモリブロックを管理する。
/// 解放されたブロックはフリーリストに戻され、隣接ブロックと結合される。
class FreeListAllocator
{
public:
	/// @brief フリーリストアロケータを構築する
	/// @param sizeBytes バッファサイズ（バイト）
	/// @param policy 検索戦略
	explicit FreeListAllocator(std::size_t sizeBytes, FitPolicy policy = FitPolicy::FirstFit)
		: m_buffer(std::make_unique<std::byte[]>(sizeBytes))
		, m_capacity(sizeBytes)
		, m_used(0)
		, m_policy(policy)
	{
		// 初期状態: バッファ全体が1つの空きブロック
		m_freeList = reinterpret_cast<FreeBlock*>(m_buffer.get());
		m_freeList->size = sizeBytes;
		m_freeList->next = nullptr;
	}

	/// @brief コピー禁止
	FreeListAllocator(const FreeListAllocator&) = delete;
	FreeListAllocator& operator=(const FreeListAllocator&) = delete;

	/// @brief ムーブ禁止（フリーリストポインタが無効になるため）
	FreeListAllocator(FreeListAllocator&&) = delete;
	FreeListAllocator& operator=(FreeListAllocator&&) = delete;

	~FreeListAllocator() = default;

	/// @brief アライメントを考慮してメモリを割り当てる
	/// @param size 要求サイズ（バイト）
	/// @param alignment アライメント要件
	/// @return 割り当てられたメモリへのポインタ、失敗時は nullptr
	[[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t))
	{
		const std::size_t totalSize = size + sizeof(AllocationHeader);

		FreeBlock* prev = nullptr;
		FreeBlock* best = nullptr;
		FreeBlock* bestPrev = nullptr;

		// 検索戦略に基づいてブロックを探す
		for (FreeBlock* curr = m_freeList; curr != nullptr; curr = curr->next)
		{
			if (curr->size >= totalSize)
			{
				if (m_policy == FitPolicy::FirstFit)
				{
					best = curr;
					bestPrev = prev;
					break;
				}
				else
				{
					if (!best || curr->size < best->size)
					{
						best = curr;
						bestPrev = prev;
					}
				}
			}
			prev = curr;
		}

		if (!best) return nullptr;

		// ブロックを分割するか判定
		const std::size_t remaining = best->size - totalSize;
		if (remaining >= MIN_BLOCK_SIZE)
		{
			// 残りを新しい空きブロックとして作成
			auto* newBlock = reinterpret_cast<FreeBlock*>(
				reinterpret_cast<std::byte*>(best) + totalSize);
			newBlock->size = remaining;
			newBlock->next = best->next;

			// フリーリストを更新
			if (bestPrev)
			{
				bestPrev->next = newBlock;
			}
			else
			{
				m_freeList = newBlock;
			}

			best->size = totalSize;
		}
		else
		{
			// ブロック全体を使用
			if (bestPrev)
			{
				bestPrev->next = best->next;
			}
			else
			{
				m_freeList = best->next;
			}
		}

		// ヘッダーを設定
		auto* header = reinterpret_cast<AllocationHeader*>(best);
		header->size = best->size;

		m_used += header->size;

		return reinterpret_cast<std::byte*>(header) + sizeof(AllocationHeader);
	}

	/// @brief メモリを解放する
	/// @param ptr allocate()で取得したポインタ
	void deallocate(void* ptr)
	{
		if (!ptr) return;

		auto* header = reinterpret_cast<AllocationHeader*>(
			static_cast<std::byte*>(ptr) - sizeof(AllocationHeader));

		m_used -= header->size;

		// 解放ブロックを作成
		auto* block = reinterpret_cast<FreeBlock*>(header);
		block->size = header->size;

		// アドレス順にフリーリストに挿入
		FreeBlock* prev = nullptr;
		FreeBlock* curr = m_freeList;
		while (curr != nullptr && curr < block)
		{
			prev = curr;
			curr = curr->next;
		}

		block->next = curr;
		if (prev)
		{
			prev->next = block;
		}
		else
		{
			m_freeList = block;
		}

		// 隣接ブロックを結合（コアレス）
		coalesce(prev, block);
		coalesce(block, block->next);
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

	/// @brief 全メモリをリセットする
	void reset()
	{
		m_freeList = reinterpret_cast<FreeBlock*>(m_buffer.get());
		m_freeList->size = m_capacity;
		m_freeList->next = nullptr;
		m_used = 0;
	}

	/// @brief 使用済みバイト数を返す
	[[nodiscard]] std::size_t used() const noexcept { return m_used; }

	/// @brief 総容量を返す
	[[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

private:
	/// @brief 空きブロック（フリーリストのノード）
	struct FreeBlock
	{
		std::size_t size;   ///< ブロックサイズ（ヘッダー含む）
		FreeBlock* next;    ///< 次の空きブロック
	};

	/// @brief 割り当てヘッダー
	struct AllocationHeader
	{
		std::size_t size;  ///< 割り当てサイズ（ヘッダー含む）
	};

	/// @brief 分割可能な最小ブロックサイズ
	static constexpr std::size_t MIN_BLOCK_SIZE = sizeof(FreeBlock);

	/// @brief 隣接する空きブロックを結合する
	/// @param a 前のブロック
	/// @param b 後のブロック
	void coalesce(FreeBlock* a, FreeBlock* b)
	{
		if (!a || !b) return;

		auto* aEnd = reinterpret_cast<std::byte*>(a) + a->size;
		if (aEnd == reinterpret_cast<std::byte*>(b))
		{
			a->size += b->size;
			a->next = b->next;
		}
	}

	std::unique_ptr<std::byte[]> m_buffer;  ///< メモリバッファ
	std::size_t m_capacity;                 ///< 総容量
	std::size_t m_used;                     ///< 使用済みバイト数
	FitPolicy m_policy;                     ///< 検索戦略
	FreeBlock* m_freeList{nullptr};         ///< フリーリストの先頭
};

} // namespace sgc
