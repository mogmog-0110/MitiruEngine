#pragma once

/// @file SideStateRing.hpp
/// @brief GameMemory の外に持つ状態 (ADR 0054) を毎フレーム積む、長さが変わるフレームのリング。
/// @details GameMemoryRing と同じフレームに 1 枚ずつ積み、同じ「何フレーム前か」で引く。物理の
/// 接触の数などで 1 フレームの長さが変わるので、置き場は byte の輪にして予算で古い側から捨てる。
/// 前のフレームとの XOR と zero-run RLE の差分にし、さらに zstd で詰める。長さが変わったフレームは、
/// 前のフレームを 0 で伸ばした (または切った) ものとの差分にする (物理の状態は body の並びが先頭に
/// 同じ形で続き、接触の数だけ末尾が伸び縮みする)。keyframe は keyframeEvery フレームごとに置く。
/// 差分を復元できなくなるので、捨てるときは keyframe からの 1 世代ごと捨てる。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <mitiru/debug/TracyZones.hpp>
#include <mitiru/observe/detail/GameMemoryDelta.hpp>

namespace mitiru::observe
{

class SideStateRing
{
public:
	/// @param capacity 最大フレーム数 (GameMemoryRing と同じ値にする)
	/// @param budgetBytes 置き場の上限。超える分は古い世代から捨てる
	/// @param keyframeEvery 差分を何フレーム続けたら keyframe を置くか
	void configure(std::size_t capacity, std::size_t budgetBytes, std::uint32_t keyframeEvery = 60)
	{
		m_cap           = capacity;
		m_budget        = (std::max)(budgetBytes, std::size_t{1});
		m_keyframeEvery = (std::max)(std::uint32_t{1}, keyframeEvery);
		m_meta.assign(capacity, SlotMeta{});
		m_walk.assign(capacity, std::size_t{0});
		m_log.clear();
		m_zstd.open();
		clear();
	}

	[[nodiscard]] bool configured() const noexcept { return m_cap > 0; }

	void clear() noexcept
	{
		m_count = m_head = 0;
		m_writePos = m_used = 0;
		m_seq = 0;
		m_hasLast = false;
		m_restoreValid = false;
	}

	/// @brief 1 フレーム分を最新として積む。len が 0 なら何もしない。
	void push(const std::uint8_t* img, std::size_t len)
	{
		MITIRU_ZONE_NAMED("observe::SideStateRing::push");
		if (m_cap == 0 || img == nullptr || len == 0) { return; }
		ensureScratch(len);

		bool keyframe = true;
		std::size_t storeLen = len;
		const std::uint8_t* store = encode(img, len, storeLen, keyframe);
		bool packed = false;
		if (const std::size_t z = m_zstd.compress(store, storeLen, m_scratchZ.data(), storeLen - 1); z != 0)
		{
			store = m_scratchZ.data(); storeLen = z; packed = true;
		}

		makeRoom(storeLen);
		copyIn(m_writePos, store, storeLen);
		m_meta[m_head] = SlotMeta{m_writePos, storeLen, len, keyframe, packed};
		m_writePos = (m_writePos + storeLen) % m_log.size();
		m_used += storeLen;
		m_head = (m_head + 1) % m_cap;
		++m_count;

		m_last.assign(img, img + len);
		m_hasLast = true;
		++m_seq;
		m_restoreValid = false;
	}

	/// @brief N フレーム前を復元して返す (0 = 最新)。範囲外は nullptr。次の push / at / clear まで有効。
	[[nodiscard]] const std::uint8_t* at(std::size_t offsetFromNewest, std::size_t& lenOut) const
	{
		MITIRU_ZONE_NAMED("observe::SideStateRing::at");
		lenOut = 0;
		if (offsetFromNewest >= m_count) { return nullptr; }
		const std::size_t target = (m_head + m_cap - 1 - offsetFromNewest) % m_cap;
		lenOut = m_meta[target].rawLen;
		if (offsetFromNewest == 0) { return m_last.data(); }   // 最新は生のまま持っている
		if (m_restoreValid && target == m_restoreIdx) { return m_restore.data(); }
		if (m_restoreValid && stepFromRestored(target)) { return m_restore.data(); }
		std::size_t n = 0;
		for (std::size_t idx = target;; idx = (idx + m_cap - 1) % m_cap)
		{
			m_walk[n++] = idx;
			if (m_meta[idx].keyframe || n >= m_count) { break; }
		}
		for (std::size_t i = n; i > 0; --i)
		{
			applyAfter(m_meta[m_walk[i - 1]], i < n ? m_meta[m_walk[i]].rawLen : 0);
		}
		m_restoreIdx   = target;
		m_restoreValid = true;
		return m_restore.data();
	}

	[[nodiscard]] std::size_t size() const noexcept { return m_count; }
	[[nodiscard]] std::size_t capacity() const noexcept { return m_cap; }
	/// @brief 記録が実際に占める byte 数 (詰めた後)
	[[nodiscard]] std::size_t usedBytes() const noexcept { return m_used; }
	/// @brief 確保している byte 数 (置き場と作業領域の合計)
	[[nodiscard]] std::size_t allocatedBytes() const noexcept
	{
		return m_log.size() + m_last.size() + m_scratchD.size() + m_scratchZ.size() + m_restore.size() + m_prevExt.size()
		     + m_meta.size() * sizeof(SlotMeta) + m_walk.size() * sizeof(std::size_t);
	}

private:
	struct SlotMeta
	{
		std::size_t offset{0};
		std::size_t length{0};   ///< 置き場で占める byte 数
		std::size_t rawLen{0};   ///< 復元後の byte 数
		bool        keyframe{false};
		bool        packed{false};
	};

	void ensureScratch(std::size_t len)
	{
		// 長さが伸びたときだけ確保する。物理の接触が増える等で伸び、以後は同じ大きさで回る。
		if (m_scratchD.size() < len)
		{
			m_scratchD.resize(len); m_scratchZ.resize(len); m_restore.resize(len); m_prevExt.resize(len);
		}
	}

	const std::uint8_t* encode(const std::uint8_t* img, std::size_t len, std::size_t& storeLen, bool& keyframe)
	{
		keyframe = !m_hasLast || (m_seq % m_keyframeEvery) == 0 || m_count == 0;
		if (!keyframe)
		{
			const std::uint8_t* prev = m_last.data();
			if (m_last.size() != len)
			{
				const std::size_t keep = (std::min)(len, m_last.size());
				std::memcpy(m_prevExt.data(), m_last.data(), keep);
				std::memset(m_prevExt.data() + keep, 0, len - keep);
				prev = m_prevExt.data();
			}
			const std::size_t enc = detail::encodeXorRle(prev, img, len, m_scratchD.data(), m_scratchD.size());
			if (enc != 0 && enc < len) { storeLen = enc; return m_scratchD.data(); }
			keyframe = true;
		}
		storeLen = len;
		return img;
	}

	/// @brief storeLen を置ける空きを作る。予算まで置き場を広げ、それでも足りなければ古い世代を捨てる。
	void makeRoom(std::size_t storeLen)
	{
		if (m_used + storeLen > m_log.size() && m_log.size() < m_budget)
		{
			grow((std::min)(m_budget, (std::max)(m_used + storeLen, m_log.size() * 2)));
		}
		while (m_count > 0 && (m_count == m_cap || m_used + storeLen > m_log.size()))
		{
			std::size_t idx = (m_head + m_cap - m_count) % m_cap;
			do
			{
				m_used -= m_meta[idx].length;
				--m_count;
				idx = (idx + 1) % m_cap;
			} while (m_count > 0 && !m_meta[idx].keyframe);
		}
		if (m_count == 0) { m_writePos = 0; m_used = 0; }
		if (storeLen > m_log.size()) { grow(storeLen); }  // 1 枚が予算を超えても最新だけは持つ
	}

	/// @brief 置き場を広げ、古い順に先頭から詰め直す (wrap した中身も 1 本に並ぶので offset を振り直す)。
	void grow(std::size_t newSize)
	{
		std::vector<std::uint8_t> grown(newSize, std::uint8_t{0});
		std::size_t w = 0;
		std::size_t idx = (m_head + m_cap - m_count) % m_cap;
		for (std::size_t i = 0; i < m_count; ++i)
		{
			SlotMeta& meta = m_meta[idx];
			copyOut(meta.offset, meta.length, grown.data() + w);
			meta.offset = w;
			w += meta.length;
			idx = (idx + 1) % m_cap;
		}
		m_log.swap(grown);
		m_writePos = (m_log.empty()) ? 0 : w % m_log.size();
	}

	void copyIn(std::size_t offset, const std::uint8_t* src, std::size_t len) noexcept
	{
		const std::size_t first = (std::min)(len, m_log.size() - offset);
		std::memcpy(&m_log[offset], src, first);
		if (first < len) { std::memcpy(&m_log[0], src + first, len - first); }
	}

	void copyOut(std::size_t offset, std::size_t len, std::uint8_t* dst) const noexcept
	{
		const std::size_t first = (std::min)(len, m_log.size() - offset);
		std::memcpy(dst, &m_log[offset], first);
		if (first < len) { std::memcpy(dst + first, &m_log[0], len - first); }
	}

	/// @brief 直前に復元した枚の隣なら、差分 1 枚の適用で済ませる (シークバーをなぞる scrub の形)。
	/// XOR の差分は同じ値を当て直すと元に戻るので、1 つ古い側へも戻れる (長さが縮んだ枚は戻れない)。
	bool stepFromRestored(std::size_t target) const
	{
		const SlotMeta& cur = m_meta[m_restoreIdx];
		if (target == (m_restoreIdx + 1) % m_cap && !m_meta[target].keyframe)
		{
			applyAfter(m_meta[target], cur.rawLen);
		}
		else if (target == (m_restoreIdx + m_cap - 1) % m_cap && !cur.keyframe
		         && m_meta[target].rawLen <= cur.rawLen)
		{
			applyAfter(cur, cur.rawLen);
		}
		else { return false; }
		m_restoreIdx = target;
		return true;
	}

	/// @brief 差分の元は 1 つ前の枚の長さで、伸びた分は 0 として XOR されている。
	void applyAfter(const SlotMeta& meta, std::size_t prevLen) const
	{
		if (!meta.keyframe && meta.rawLen > prevLen)
		{
			std::memset(m_restore.data() + prevLen, 0, meta.rawLen - prevLen);
		}
		apply(meta);
	}

	void apply(const SlotMeta& meta) const
	{
		const std::uint8_t* src = nullptr;
		std::size_t srcLen = meta.length;
		copyOut(meta.offset, meta.length, m_scratchZ.data());
		src = m_scratchZ.data();
		if (meta.packed)
		{
			std::uint8_t* dst = meta.keyframe ? m_restore.data() : m_scratchD.data();
			srcLen = m_zstd.decompress(m_scratchZ.data(), meta.length, dst, meta.rawLen);
			if (meta.keyframe) { return; }
			src = m_scratchD.data();
		}
		if (meta.keyframe) { std::memcpy(m_restore.data(), src, meta.rawLen); return; }
		detail::decodeXorRleApply(src, srcLen, m_restore.data(), meta.rawLen);
	}

	std::size_t   m_cap{0}, m_count{0}, m_head{0};
	std::size_t   m_budget{0};
	std::uint32_t m_keyframeEvery{60};
	std::uint64_t m_seq{0};

	std::vector<std::uint8_t> m_log;
	std::size_t               m_writePos{0};
	std::size_t               m_used{0};
	std::vector<SlotMeta>     m_meta;
	std::vector<std::uint8_t> m_last;   ///< 直前に積んだ生の bytes (差分の元)
	bool                      m_hasLast{false};

	// push では差分と zstd の出力先、at() では zstd の展開先。push と at() は同時に走らない。
	mutable std::vector<std::uint8_t> m_scratchD;
	mutable std::vector<std::uint8_t> m_scratchZ;
	mutable std::vector<std::uint8_t> m_restore;
	std::vector<std::uint8_t>         m_prevExt;   ///< 長さが変わったフレームの差分の元 (前の枚を伸ばす・切る)
	mutable std::vector<std::size_t>  m_walk;
	mutable std::size_t               m_restoreIdx{0};
	mutable bool                      m_restoreValid{false};
	util::ZstdContext                 m_zstd;
};

}  // namespace mitiru::observe
