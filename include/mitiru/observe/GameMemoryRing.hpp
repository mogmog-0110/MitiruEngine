#pragma once

/// @file GameMemoryRing.hpp
/// @brief host が所有する固定容量の GameMemory バイト列リングバッファの宣言

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <mitiru/debug/TracyZones.hpp>

// AVX2 は MSVC の x86/x64 でのみ使う。実行時に CPU の対応を確認し、未対応なら scalar 実装へ切り替える。
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define MITIRU_GMRING_AVX2 1
#include <immintrin.h>
#include <intrin.h>
#endif

namespace mitiru::observe
{

class GameMemoryRing
{
public:
	/// @brief GameMemory のサイズ確定後に呼ぶ。同じ設定なら何もしない。
	/// @param frameSize GameMemory のバイト数。ModuleApi.memorySize と一致させる
	/// @param capacity 最大フレーム数。既定は 300 フレーム
	/// @param budgetBytes 0 なら非圧縮。予算を超える場合だけ XOR と RLE で圧縮する
	/// @param keyframeEvery 圧縮時の keyframe 間隔。既定は 60 フレームで、keyframe が evict される
	///        前に必ず 1 つは残る不変条件を保つよう capacity 以下へ自動 clamp する
	void configure(std::uint32_t frameSize, std::size_t capacity = 300,
	               std::size_t budgetBytes = 0, std::uint32_t keyframeEvery = 60)
	{
		if (frameSize == 0 || capacity == 0)
		{
			resetToEmpty();
			return;
		}
		// 予算内なら memcpy ring のままにして、圧縮によるフレーム全体の走査を避ける。
		const std::uint64_t rawWorstCase = static_cast<std::uint64_t>(frameSize) * capacity;
		if (budgetBytes == 0 || rawWorstCase < budgetBytes) { configureRaw(frameSize, capacity); }
		else { configureCompressed(frameSize, capacity, budgetBytes, keyframeEvery); }
	}

	/// @brief 1 フレーム分の GameMemory バイト列を記録する。容量を超えたら最古のフレームを破棄する
	/// @details size が frameSize と異なる場合と、configure 前の呼び出しは何もしない。
	void push(const void* mem, std::uint32_t size)
	{
		MITIRU_ZONE_NAMED("observe::GameMemoryRing::push");
		if (mem == nullptr || size == 0 || size != m_frameSize || m_cap == 0) { return; }
		if (m_compressed) { pushCompressed(static_cast<const std::uint8_t*>(mem)); }
		else { pushRaw(static_cast<const std::uint8_t*>(mem)); }
	}

	/// @brief N フレーム前の GameMemory バイト列を返す
	/// @param offsetFromNewest 0 は最新、1 は 1 フレーム前
	/// @return 範囲内なら frameSize バイトの先頭、範囲外なら nullptr
	/// @note 返り値は次の push、configure、clear、at のいずれかを呼ぶまで有効
	[[nodiscard]] const std::uint8_t* at(std::size_t offsetFromNewest) const noexcept
	{
		MITIRU_ZONE_NAMED("observe::GameMemoryRing::at");
		if (offsetFromNewest >= m_count) { return nullptr; }
		return m_compressed ? atCompressed(offsetFromNewest) : atRaw(offsetFromNewest);
	}

	[[nodiscard]] std::size_t   size() const noexcept { return m_count; }
	[[nodiscard]] std::size_t   capacity() const noexcept { return m_cap; }
	[[nodiscard]] std::uint32_t frameSize() const noexcept { return m_frameSize; }
	[[nodiscard]] bool          empty() const noexcept { return m_count == 0; }
	[[nodiscard]] bool          isCompressed() const noexcept { return m_compressed; }

	/// @brief 実際に使っているバイト数を返す。非圧縮時は常に capacity × frameSize
	[[nodiscard]] std::size_t usedBytes() const noexcept
	{
		return m_compressed ? m_logUsedBytes
		                     : static_cast<std::size_t>(m_frameSize) * m_cap;
	}

	/// @brief 全フレームを破棄する。rewind の確定時と reload でレイアウトが変わるときに呼ぶ
	void clear() noexcept
	{
		m_count = m_head = 0;
		if (m_compressed)
		{
			m_logWritePos  = 0;
			m_logUsedBytes = 0;
			m_pushSeq      = 0;
			m_hasLastRaw   = false;
			m_lastValid    = false;
		}
	}

private:
	// ── 非圧縮 mode ──
	void configureRaw(std::uint32_t frameSize, std::size_t capacity)
	{
		if (!m_compressed && frameSize == m_frameSize && capacity == m_cap) { return; }  // 既に同形
		freeCompressedBuffers();
		m_compressed = false;
		m_frameSize  = frameSize;
		m_cap        = capacity;
		m_buf.assign(static_cast<std::size_t>(frameSize) * capacity, std::uint8_t{0});
		m_count = m_head = 0;
	}

	void pushRaw(const std::uint8_t* mem)
	{
		std::uint8_t* slot = &m_buf[m_head * m_frameSize];
		std::memcpy(slot, mem, m_frameSize);
		m_head = (m_head + 1) % m_cap;
		if (m_count < m_cap) { ++m_count; }
	}

	[[nodiscard]] const std::uint8_t* atRaw(std::size_t offsetFromNewest) const noexcept
	{
		const std::size_t idx = (m_head + m_cap - 1 - offsetFromNewest) % m_cap;
		return &m_buf[idx * m_frameSize];
	}

	// ── 圧縮 mode: XOR と zero-run RLE の差分、定期 keyframe ──
	struct SlotMeta
	{
		std::size_t   offset{0};     ///< m_log 内の開始位置 (wrap 可)
		std::uint32_t length{0};     ///< 格納バイト数 (keyframe なら常に frameSize)
		bool          keyframe{false};
	};

	void configureCompressed(std::uint32_t frameSize, std::size_t capacity,
	                          std::size_t budgetBytes, std::uint32_t keyframeEvery)
	{
		std::size_t effectiveCapacity = capacity;
		const std::uint64_t rawWorstCase =
			static_cast<std::uint64_t>(capacity) * static_cast<std::uint64_t>(frameSize);
		if (rawWorstCase > static_cast<std::uint64_t>(budgetBytes))
		{
			effectiveCapacity = (std::max)(static_cast<std::size_t>(1),
			                               budgetBytes / static_cast<std::size_t>(frameSize));
			std::fprintf(stderr,
			             "[mitiru] rewind ring: %zu frames -> %zu frames (budget %.1f MB)\n",
			             capacity, effectiveCapacity,
			             static_cast<double>(budgetBytes) / (1024.0 * 1024.0));
		}
		// keyframe を1枚も持てないと復元できないので frameSize 未満へは下げられない。
		// budgetBytes がそれより小さいと実際の確保量が要求予算を超える。呼び出し元は
		// budgetBytes だけを見て安心しがちなので、超過したことを気づけるよう警告を出す。
		const std::size_t logBytes = (std::max)(budgetBytes, static_cast<std::size_t>(frameSize));
		if (logBytes > budgetBytes)
		{
			std::fprintf(stderr,
			             "[mitiru] rewind ring: budget %zu bytes は 1 frame (%u bytes) 未満のため"
			             " %zu bytes へ切り上げて確保する (予算超過)\n",
			             budgetBytes, frameSize, logBytes);
		}
		const std::uint32_t kfEvery = (std::max)(std::uint32_t{1},
			(std::min)(keyframeEvery, static_cast<std::uint32_t>(effectiveCapacity)));

		if (m_compressed && frameSize == m_frameSize && effectiveCapacity == m_cap &&
		    logBytes == m_logBytes && kfEvery == m_keyframeEvery)
		{
			return;  // 既に同形
		}

		m_buf.clear();  // raw buffer は不要
		m_compressed    = true;
		m_frameSize     = frameSize;
		m_cap           = effectiveCapacity;
		m_logBytes      = logBytes;
		m_keyframeEvery = kfEvery;

		m_log.assign(m_logBytes, std::uint8_t{0});
		m_meta.assign(m_cap, SlotMeta{});
		m_walkScratch.assign(m_cap, std::size_t{0});
		m_lastRaw.assign(frameSize, std::uint8_t{0});
		// zero-run RLE の最悪時に備え、frameSize の約 3 倍を確保する。収まらない差分は生の keyframe として保存する。
		const std::size_t codecCap = static_cast<std::size_t>(frameSize) * 3 + 64;
		m_encodeScratch.assign(codecCap, std::uint8_t{0});
		m_readScratch.assign(codecCap, std::uint8_t{0});
		m_restoreScratch.assign(frameSize, std::uint8_t{0});

		m_count = m_head = 0;
		m_logWritePos = m_logUsedBytes = 0;
		m_pushSeq    = 0;
		m_hasLastRaw = m_lastValid = false;
	}

	void freeCompressedBuffers()
	{
		m_log.clear();
		m_meta.clear();
		m_walkScratch.clear();
		m_lastRaw.clear();
		m_encodeScratch.clear();
		m_readScratch.clear();
		m_restoreScratch.clear();
		m_logBytes = m_logWritePos = m_logUsedBytes = 0;
		m_pushSeq = 0;
		m_hasLastRaw = m_lastValid = false;
	}

	void copyFromLog(std::size_t offset, std::size_t length, std::uint8_t* dst) const noexcept
	{
		const std::size_t first = (std::min)(length, m_logBytes - offset);
		std::memcpy(dst, &m_log[offset], first);
		if (first < length) { std::memcpy(dst + first, &m_log[0], length - first); }
	}

	void copyIntoLog(std::size_t offset, const std::uint8_t* src, std::size_t length) noexcept
	{
		const std::size_t first = (std::min)(length, m_logBytes - offset);
		std::memcpy(&m_log[offset], src, first);
		if (first < length) { std::memcpy(&m_log[0], src + first, length - first); }
	}

	/// @brief LEB128 形式の varint を 1 個書く。容量不足なら false を返す
	static bool putVarint(std::uint32_t v, std::uint8_t* out, std::size_t cap, std::size_t& w)
	{
		for (;;)
		{
			if (w >= cap) { return false; }
			const std::uint8_t b = static_cast<std::uint8_t>(v & 0x7Fu);
			v >>= 7;
			if (v != 0) { out[w++] = static_cast<std::uint8_t>(b | 0x80u); }
			else { out[w++] = b; return true; }
		}
	}

	static std::uint32_t getVarint(const std::uint8_t* in, std::size_t& r) noexcept
	{
		std::uint32_t v = 0;
		int shift = 0;
		for (;;)
		{
			const std::uint8_t b = in[r++];
			v |= static_cast<std::uint32_t>(b & 0x7Fu) << shift;
			if ((b & 0x80u) == 0) { break; }
			shift += 7;
		}
		return v;
	}

#if defined(MITIRU_GMRING_AVX2)
	/// @brief CPU の AVX2 対応を初回だけ確認する
	static bool cpuHasAvx2() noexcept
	{
		static const bool has = [] {
			int info[4] = {0, 0, 0, 0};
			__cpuidex(info, 7, 0);
			return (info[1] & (1 << 5)) != 0;  // EBX bit5 = AVX2
		}();
		return has;
	}

	static std::size_t zeroRunLenAvx2(const std::uint8_t* prev, const std::uint8_t* cur,
	                                   std::size_t pos, std::size_t n) noexcept
	{
		std::size_t p = pos;
		while (p + 32 <= n)
		{
			const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prev + p));
			const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur + p));
			const unsigned mask = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, b)));
			if (mask != 0xFFFFFFFFu)
			{
				unsigned long firstDiff;
				_BitScanForward(&firstDiff, ~mask);
				return (p + firstDiff) - pos;
			}
			p += 32;
		}
		while (p < n && prev[p] == cur[p]) { ++p; }
		return p - pos;
	}

	static std::size_t literalRunLenAvx2(const std::uint8_t* prev, const std::uint8_t* cur,
	                                      std::size_t pos, std::size_t n) noexcept
	{
		std::size_t p = pos;
		while (p + 32 <= n)
		{
			const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prev + p));
			const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur + p));
			const unsigned mask = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, b)));
			if (mask != 0)
			{
				unsigned long firstSame;
				_BitScanForward(&firstSame, mask);
				return (p + firstSame) - pos;
			}
			p += 32;
		}
		while (p < n && prev[p] != cur[p]) { ++p; }
		return p - pos;
	}

	static void xorCopyAvx2(std::uint8_t* out, const std::uint8_t* prev, const std::uint8_t* cur,
	                         std::size_t pos, std::size_t len) noexcept
	{
		std::size_t i = 0;
		for (; i + 32 <= len; i += 32)
		{
			const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prev + pos + i));
			const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur + pos + i));
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i), _mm256_xor_si256(a, b));
		}
		for (; i < len; ++i) { out[i] = static_cast<std::uint8_t>(cur[pos + i] ^ prev[pos + i]); }
	}
#endif

	static std::size_t zeroRunLen(const std::uint8_t* prev, const std::uint8_t* cur,
	                               std::size_t pos, std::size_t n) noexcept
	{
		std::size_t p = pos;
		while (p < n && prev[p] == cur[p]) { ++p; }
		return p - pos;
	}

	static std::size_t literalRunLen(const std::uint8_t* prev, const std::uint8_t* cur,
	                                  std::size_t pos, std::size_t n) noexcept
	{
		std::size_t p = pos;
		while (p < n && prev[p] != cur[p]) { ++p; }
		return p - pos;
	}

	/// @brief prev から cur への XOR 差分を、skip、litLen、lit bytes の zero-run RLE 形式で @p out に書く。容量不足なら 0 を返す
	static std::size_t encodeXorRle(const std::uint8_t* prev, const std::uint8_t* cur,
	                                 std::size_t n, std::uint8_t* out, std::size_t outCap)
	{
#if defined(MITIRU_GMRING_AVX2)
		const bool avx2 = cpuHasAvx2();
#endif
		std::size_t pos = 0, w = 0;
		while (pos < n)
		{
#if defined(MITIRU_GMRING_AVX2)
			const std::size_t zeroLen = avx2 ? zeroRunLenAvx2(prev, cur, pos, n) : zeroRunLen(prev, cur, pos, n);
#else
			const std::size_t zeroLen = zeroRunLen(prev, cur, pos, n);
#endif
			pos += zeroLen;
#if defined(MITIRU_GMRING_AVX2)
			const std::size_t litLen = avx2 ? literalRunLenAvx2(prev, cur, pos, n) : literalRunLen(prev, cur, pos, n);
#else
			const std::size_t litLen = literalRunLen(prev, cur, pos, n);
#endif
			if (!putVarint(static_cast<std::uint32_t>(zeroLen), out, outCap, w)) { return 0; }
			if (!putVarint(static_cast<std::uint32_t>(litLen), out, outCap, w)) { return 0; }
			if (litLen > 0)
			{
				if (w + litLen > outCap) { return 0; }
#if defined(MITIRU_GMRING_AVX2)
				if (avx2) { xorCopyAvx2(out + w, prev, cur, pos, litLen); }
				else { for (std::size_t i = 0; i < litLen; ++i) { out[w + i] = static_cast<std::uint8_t>(cur[pos + i] ^ prev[pos + i]); } }
#else
				for (std::size_t i = 0; i < litLen; ++i) { out[w + i] = static_cast<std::uint8_t>(cur[pos + i] ^ prev[pos + i]); }
#endif
				w += litLen;
			}
			pos += litLen;
		}
		return w;
	}

	/// @brief zero-run RLE 差分を、直前のフレームを保持する @p buf に XOR で適用する
	static void decodeXorRleApply(const std::uint8_t* enc, std::size_t encLen,
	                               std::uint8_t* buf, std::size_t n) noexcept
	{
		std::size_t pos = 0, r = 0;
		while (pos < n && r < encLen)
		{
			const std::uint32_t skip = getVarint(enc, r);
			pos += skip;
			const std::uint32_t lit = getVarint(enc, r);
			for (std::uint32_t i = 0; i < lit; ++i) { buf[pos + i] ^= enc[r + i]; }
			r   += lit;
			pos += lit;
		}
	}

	void pushCompressed(const std::uint8_t* mem)
	{
		const bool wantKeyframe = !m_hasLastRaw || (m_pushSeq % m_keyframeEvery == 0);
		std::size_t          storeLen;
		bool                 isKeyframe;
		const std::uint8_t*  storeData;

		if (wantKeyframe)
		{
			storeData = mem; storeLen = m_frameSize; isKeyframe = true;
		}
		else
		{
			const std::size_t encLen = encodeXorRle(m_lastRaw.data(), mem, m_frameSize,
			                                         m_encodeScratch.data(), m_encodeScratch.size());
			if (encLen == 0 || encLen >= m_frameSize)
			{
				storeData = mem; storeLen = m_frameSize; isKeyframe = true;
			}
			else
			{
				storeData = m_encodeScratch.data(); storeLen = encLen; isKeyframe = false;
			}
		}

		// keyframe だけを捨てると後続の差分を復元できないため、予算に収まるまで世代単位で破棄する。
		while (m_count > 0 && (m_count == m_cap || m_logUsedBytes + storeLen > m_logBytes))
		{
			std::size_t idx = (m_head + m_cap - m_count) % m_cap;
			do
			{
				m_logUsedBytes -= m_meta[idx].length;
				--m_count;
				idx = (idx + 1) % m_cap;
			} while (m_count > 0 && !m_meta[idx].keyframe);
		}

		copyIntoLog(m_logWritePos, storeData, storeLen);
		m_meta[m_head] = SlotMeta{m_logWritePos, static_cast<std::uint32_t>(storeLen), isKeyframe};
		m_logWritePos   = (m_logWritePos + storeLen) % m_logBytes;
		m_logUsedBytes += storeLen;
		m_head = (m_head + 1) % m_cap;
		++m_count;

		std::memcpy(m_lastRaw.data(), mem, m_frameSize);
		m_hasLastRaw = true;
		++m_pushSeq;
		m_lastValid = false;  // ring 内容が変わったので前回の復元キャッシュは無効
	}

	/// @brief 1 slot を @p dst に適用する。keyframe なら上書きし、差分なら XOR で畳み込む
	void applySlot(const SlotMeta& meta, std::uint8_t* dst) const noexcept
	{
		if (meta.keyframe)
		{
			copyFromLog(meta.offset, m_frameSize, dst);
		}
		else
		{
			copyFromLog(meta.offset, meta.length, m_readScratch.data());
			decodeXorRleApply(m_readScratch.data(), meta.length, dst, m_frameSize);
		}
	}

	[[nodiscard]] const std::uint8_t* atCompressed(std::size_t offsetFromNewest) const noexcept
	{
		const std::size_t targetIdx = (m_head + m_cap - 1 - offsetFromNewest) % m_cap;

		// 直前の at() より 1 つ新しいフレームなら、1 slot の適用だけで復元する。
		if (m_lastValid && targetIdx == m_lastIdx) { return m_restoreScratch.data(); }
		if (m_lastValid && targetIdx == (m_lastIdx + 1) % m_cap)
		{
			applySlot(m_meta[targetIdx], m_restoreScratch.data());
			m_lastIdx = targetIdx;
			return m_restoreScratch.data();
		}
		// XOR 差分は同じ値を再適用すると元に戻るため、差分フレーム間では 1 slot 前へ戻れる。keyframe には使わない。
		if (m_lastValid && !m_meta[m_lastIdx].keyframe && targetIdx == (m_lastIdx + m_cap - 1) % m_cap)
		{
			applySlot(m_meta[m_lastIdx], m_restoreScratch.data());
			m_lastIdx = targetIdx;
			return m_restoreScratch.data();
		}

		// keyframe まで遡り、対象フレームへ向けて差分を適用する。作業領域は configure 時に確保する。
		std::size_t n = 0;
		std::size_t idx = targetIdx;
		for (;;)
		{
			m_walkScratch[n++] = idx;
			if (m_meta[idx].keyframe || n >= m_cap) { break; }
			idx = (idx + m_cap - 1) % m_cap;
		}
		copyFromLog(m_meta[m_walkScratch[n - 1]].offset, m_frameSize, m_restoreScratch.data());
		for (std::size_t i = n - 1; i > 0; --i)
		{
			applySlot(m_meta[m_walkScratch[i - 1]], m_restoreScratch.data());
		}

		m_lastIdx   = targetIdx;
		m_lastValid = true;
		return m_restoreScratch.data();
	}

	void resetToEmpty()
	{
		m_buf.clear();
		freeCompressedBuffers();
		m_compressed = false;
		m_frameSize  = 0;
		m_cap = m_count = m_head = 0;
	}

	// ── 共通の状態 ──
	std::uint32_t m_frameSize{0};
	std::size_t   m_cap{0}, m_count{0}, m_head{0};
	bool          m_compressed{false};

	// ── 非圧縮 mode の状態 ──
	std::vector<std::uint8_t> m_buf;  ///< capacity*frameSize の contiguous ring

	// ── 圧縮 mode の状態 ──
	std::vector<std::uint8_t> m_log;            ///< 可変長スロットを詰める circular byte log
	std::size_t               m_logBytes{0};
	std::size_t               m_logWritePos{0};
	std::size_t               m_logUsedBytes{0};
	std::vector<SlotMeta>     m_meta;            ///< frame index ring と同じ並びの offset/length 表
	std::vector<std::uint8_t> m_lastRaw;         ///< 直前 push の生バイト列 (delta 元)
	bool                      m_hasLastRaw{false};
	std::uint32_t             m_keyframeEvery{60};
	std::uint64_t             m_pushSeq{0};
	std::vector<std::uint8_t> m_encodeScratch;           ///< push 時の RLE encode 作業領域
	mutable std::vector<std::uint8_t> m_readScratch;     ///< at() 時の 1 slot 展開先
	mutable std::vector<std::uint8_t> m_restoreScratch;  ///< at() の復元結果 (返り値の実体)
	mutable std::vector<std::size_t>  m_walkScratch;     ///< at() の keyframe 遡り経路
	mutable std::size_t      m_lastIdx{0};
	mutable bool             m_lastValid{false};
};

}  // namespace mitiru::observe
