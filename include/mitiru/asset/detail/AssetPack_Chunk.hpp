#pragma once

/// @file AssetPack_Chunk.hpp
/// @brief 復号済み (XOR 解除 + 必要なら zstd 展開) chunk ページの LRU キャッシュ。
///
/// pack を mmap しても、scramble/圧縮された chunk はそのままでは読めない。
/// 同じ chunk を毎回復号し直すコストを避けるため、chunk index をキーに
/// 最大 capacity 個まで復号済みのバイト列を保持する。AssetPack インスタンス 1 個につき 1 個。

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace mitiru::vfs::detail
{

class ChunkCache
{
public:
	explicit ChunkCache(std::size_t capacity = 64) : m_capacity(capacity) {}

	/// 見つかれば most-recently-used へ移動して返す。無ければ nullptr (miss はここでは数えず、
	/// 実際に disk から読み直す側が insert() で数える)。
	[[nodiscard]] const std::vector<uint8_t>* find(uint32_t chunkIndex)
	{
		auto it = m_index.find(chunkIndex);
		if (it == m_index.end()) { return nullptr; }
		m_order.splice(m_order.begin(), m_order, it->second.orderIt);
		++m_hits;
		return &it->second.data;
	}

	const std::vector<uint8_t>& insert(uint32_t chunkIndex, std::vector<uint8_t> data)
	{
		++m_misses;
		m_order.push_front(chunkIndex);
		auto& slot   = m_index[chunkIndex];
		slot.data    = std::move(data);
		slot.orderIt = m_order.begin();
		if (m_index.size() > m_capacity) { evictOldest(); }
		return slot.data;
	}

	[[nodiscard]] std::size_t hits() const noexcept { return m_hits; }
	[[nodiscard]] std::size_t misses() const noexcept { return m_misses; }
	[[nodiscard]] std::size_t size() const noexcept { return m_index.size(); }

private:
	struct Slot
	{
		std::vector<uint8_t>         data;
		std::list<uint32_t>::iterator orderIt;
	};

	void evictOldest()
	{
		const uint32_t oldest = m_order.back();
		m_order.pop_back();
		m_index.erase(oldest);
	}

	std::size_t                          m_capacity;
	std::size_t                          m_hits   = 0;
	std::size_t                          m_misses = 0;
	std::list<uint32_t>                  m_order;  // front = 最近使った
	std::unordered_map<uint32_t, Slot>   m_index;
};

}  // namespace mitiru::vfs::detail
