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
#include <mitiru/observe/detail/GameMemoryDelta.hpp>

namespace mitiru::observe
{

class GameMemoryRing
{
public:
	/// @brief GameMemory のサイズ確定後に呼ぶ。同じ設定なら何もしない。
	/// @param frameSize GameMemory のバイト数。ModuleApi.memorySize と一致させる
	/// @param capacity 最大フレーム数。既定は 300 フレーム
	/// @param budgetBytes 記録に使ってよい上限。0 なら上限なしで常に生で持つ。圧縮時も capacity 枚を
	///        目標に持ち、予算を超える分だけ古い側から捨てる
	/// @param keyframeEvery 圧縮時の keyframe 間隔。既定は 60 フレームで、capacity 以下へ自動 clamp する
	/// @param rawLimitBytes 生で持つのはここまで。capacity × frameSize がこれと予算の小さい方を超えると、
	///        XOR と RLE の差分 (zstd を含む構成ではさらに zstd) で圧縮する。既定は予算と同じ扱い
	void configure(std::uint32_t frameSize, std::size_t capacity = 300,
	               std::size_t budgetBytes = 0, std::uint32_t keyframeEvery = 60,
	               std::size_t rawLimitBytes = SIZE_MAX)
	{
		m_fellBackToRaw = false;
		if (frameSize == 0 || capacity == 0)
		{
			resetToEmpty();
			return;
		}
		// 小さい状態は memcpy ring のままにして、圧縮によるフレーム全体の走査を避ける。
		const std::uint64_t rawWorstCase = static_cast<std::uint64_t>(frameSize) * capacity;
		const bool fitsBudget = budgetBytes == 0 || rawWorstCase < budgetBytes;
		if (fitsBudget && (budgetBytes == 0 || rawWorstCase <= rawLimitBytes))
		{
			configureRaw(frameSize, capacity);
			return;
		}
		configureCompressed(frameSize, capacity, budgetBytes, keyframeEvery);
		m_rawFallbackAllowed = fitsBudget;
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

	/// @brief 圧縮しても半分も縮まなかったので生へ切り替えたか
	[[nodiscard]] bool fellBackToRaw() const noexcept { return m_fellBackToRaw; }

	/// @brief 記録済みフレームが実際に占めるバイト数。非圧縮時は常に capacity × frameSize
	[[nodiscard]] std::size_t usedBytes() const noexcept
	{
		return m_compressed ? m_logUsedBytes
		                     : static_cast<std::size_t>(m_frameSize) * m_cap;
	}

	/// @brief 確保しているバイト数 (記録の置き場と作業領域の合計)
	[[nodiscard]] std::size_t allocatedBytes() const noexcept
	{
		return m_buf.size() + m_log.size() + m_lastRaw.size() + m_scratchA.size()
		     + m_scratchB.size() + m_restoreScratch.size()
		     + m_meta.size() * sizeof(SlotMeta) + m_walkScratch.size() * sizeof(std::size_t);
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

	// ── 圧縮 mode: XOR と zero-run RLE の差分、定期 keyframe、後段の zstd ──
	struct SlotMeta
	{
		std::size_t   offset{0};     ///< m_log 内の開始位置 (wrap 可)
		std::uint32_t length{0};     ///< 格納バイト数
		bool          keyframe{false};
		bool          packed{false};  ///< zstd で詰めてある (縮まなかった slot は素のまま置く)
	};

	void configureCompressed(std::uint32_t frameSize, std::size_t capacity,
	                          std::size_t budgetBytes, std::uint32_t keyframeEvery)
	{
		// keyframe を 1 枚も持てないと復元できないので frameSize 未満へは下げられない。
		// budgetBytes がそれより小さいと実際の確保量が要求予算を超える。呼び出し元は
		// budgetBytes だけを見て安心しがちなので、超過したことを気づけるよう警告を出す。
		const std::size_t logBudget = (std::max)(budgetBytes, static_cast<std::size_t>(frameSize));
		if (logBudget > budgetBytes)
		{
			std::fprintf(stderr,
			             "[mitiru] rewind ring: budget %zu bytes は 1 frame (%u bytes) 未満のため"
			             " %zu bytes へ切り上げて確保する (予算超過)\n",
			             budgetBytes, frameSize, logBudget);
		}
		const std::uint32_t kfEvery = (std::max)(std::uint32_t{1},
			(std::min)(keyframeEvery, static_cast<std::uint32_t>((std::min)(capacity, std::size_t{UINT32_MAX}))));

		if (m_compressed && frameSize == m_frameSize && capacity == m_cap &&
		    logBudget == m_logBudget && kfEvery == m_keyframeEvery)
		{
			return;  // 既に同形
		}

		m_buf.clear();  // raw buffer は不要
		m_compressed    = true;
		m_frameSize     = frameSize;
		m_cap           = capacity;
		m_logBudget     = logBudget;
		m_keyframeEvery = kfEvery;

		// 置き場は差分が実際に要る分だけ伸ばす。予算いっぱいを先に確保すると、圧縮で縮んでも
		// 確保量は生で持つのと変わらない。
		m_log.assign((std::min)(logBudget, static_cast<std::size_t>(frameSize) * 2), std::uint8_t{0});
		m_meta.assign(m_cap, SlotMeta{});
		m_walkScratch.assign(m_cap, std::size_t{0});
		m_lastRaw.assign(frameSize, std::uint8_t{0});
		// frameSize 以上に膨らむ差分は keyframe として持つので、作業領域も frameSize で足りる。
		m_scratchA.assign(frameSize, std::uint8_t{0});
		m_zstd.open();
		if (m_zstd.available()) { m_scratchB.assign(frameSize, std::uint8_t{0}); }
		m_restoreScratch.assign(frameSize, std::uint8_t{0});

		m_count = m_head = 0;
		m_logWritePos = m_logUsedBytes = 0;
		m_pushSeq    = 0;
		m_hasLastRaw = m_lastValid = false;
		m_probeRawBytes = m_probeStoredBytes = 0;
		m_rawFallbackAllowed = m_fellBackToRaw = false;
	}

	void freeCompressedBuffers()
	{
		m_log.clear();
		m_meta.clear();
		m_walkScratch.clear();
		m_lastRaw.clear();
		m_scratchA.clear();
		m_scratchB.clear();
		m_zstd.close();
		m_restoreScratch.clear();
		m_logBudget = m_logWritePos = m_logUsedBytes = 0;
		m_pushSeq = 0;
		m_hasLastRaw = m_lastValid = false;
	}

	void copyFromLog(std::size_t offset, std::size_t length, std::uint8_t* dst) const noexcept
	{
		const std::size_t logBytes = m_log.size();
		const std::size_t first = (std::min)(length, logBytes - offset);
		std::memcpy(dst, &m_log[offset], first);
		if (first < length) { std::memcpy(dst + first, &m_log[0], length - first); }
	}

	void copyIntoLog(std::size_t offset, const std::uint8_t* src, std::size_t length) noexcept
	{
		const std::size_t logBytes = m_log.size();
		const std::size_t first = (std::min)(length, logBytes - offset);
		std::memcpy(&m_log[offset], src, first);
		if (first < length) { std::memcpy(&m_log[0], src + first, length - first); }
	}

	/// @brief 置き場を広げ、古い順に先頭から詰め直す。wrap した中身も 1 本に並ぶので offset を振り直す。
	void growLog(std::size_t needBytes)
	{
		const std::size_t newSize = (std::min)(m_logBudget, (std::max)(needBytes, m_log.size() * 2));
		std::vector<std::uint8_t> grown(newSize, std::uint8_t{0});
		std::size_t w   = 0;
		std::size_t idx = (m_head + m_cap - m_count) % m_cap;
		for (std::size_t i = 0; i < m_count; ++i)
		{
			SlotMeta& meta = m_meta[idx];
			copyFromLog(meta.offset, meta.length, grown.data() + w);
			meta.offset = w;
			w += meta.length;
			idx = (idx + 1) % m_cap;
		}
		m_log.swap(grown);
		m_logWritePos = w % m_log.size();
	}

	/// @brief 差分を作る。縮まなければ keyframe として生で持つ。
	/// @return 置く中身。keyframe なら isKeyframe = true
	const std::uint8_t* encodeFrame(const std::uint8_t* mem, std::size_t& storeLen, bool& isKeyframe)
	{
		const bool wantKeyframe = !m_hasLastRaw || (m_pushSeq % m_keyframeEvery == 0);
		if (!wantKeyframe)
		{
			const std::size_t encLen = detail::encodeXorRle(m_lastRaw.data(), mem, m_frameSize,
			                                                m_scratchA.data(), m_scratchA.size());
			if (encLen != 0 && encLen < m_frameSize)
			{
				storeLen = encLen; isKeyframe = false;
				return m_scratchA.data();
			}
		}
		storeLen = m_frameSize; isKeyframe = true;
		return mem;
	}

	void pushCompressed(const std::uint8_t* mem)
	{
		std::size_t storeLen   = 0;
		bool        isKeyframe = false;
		const std::uint8_t* storeData = encodeFrame(mem, storeLen, isKeyframe);
		// 1 byte でも縮まなければ素のまま置く (展開の手間だけが増えるため)。
		bool packed = false;
		if (const std::size_t z = m_zstd.compress(storeData, storeLen, m_scratchB.data(), storeLen - 1); z != 0)
		{
			storeData = m_scratchB.data(); storeLen = z; packed = true;
		}

		if (m_logUsedBytes + storeLen > m_log.size() && m_log.size() < m_logBudget)
		{
			growLog(m_logUsedBytes + storeLen);
		}
		// keyframe だけを捨てると後続の差分を復元できないため、予算に収まるまで世代単位で破棄する。
		while (m_count > 0 && (m_count == m_cap || m_logUsedBytes + storeLen > m_log.size()))
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
		m_meta[m_head] = SlotMeta{m_logWritePos, static_cast<std::uint32_t>(storeLen), isKeyframe, packed};
		m_logWritePos   = (m_logWritePos + storeLen) % m_log.size();
		m_logUsedBytes += storeLen;
		m_head = (m_head + 1) % m_cap;
		++m_count;

		std::memcpy(m_lastRaw.data(), mem, m_frameSize);
		m_hasLastRaw = true;
		++m_pushSeq;
		m_lastValid = false;  // ring 内容が変わったので前回の復元キャッシュは無効
		probeCompression(storeLen);
	}

	/// @brief 最初の keyframe 1 周期ぶんで縮み方を見る。半分も縮まない状態 (全部が毎フレーム動く大量の
	///        粒子など) は圧縮の手間に見合わないので、予算に収まるなら生の ring へ切り替える。
	///        切り替えで捨てるのは最初の 1 周期ぶんの記録だけ。
	void probeCompression(std::size_t storedLen)
	{
		if (!m_rawFallbackAllowed) { return; }
		m_probeRawBytes    += m_frameSize;
		m_probeStoredBytes += storedLen;
		if (m_pushSeq < m_keyframeEvery) { return; }
		m_rawFallbackAllowed = false;
		if (m_probeStoredBytes * 2 <= m_probeRawBytes) { return; }
		std::fprintf(stderr,
		             "[mitiru] rewind ring: 差分が %.0f%% までしか縮まないので生で持つ (%zu frames x %u bytes)\n",
		             100.0 * static_cast<double>(m_probeStoredBytes) / static_cast<double>(m_probeRawBytes),
		             m_cap, m_frameSize);
		configureRaw(m_frameSize, m_cap);
		m_fellBackToRaw = true;
	}

	/// @brief 1 slot を @p dst に適用する。keyframe なら上書きし、差分なら XOR で畳み込む
	void applySlot(const SlotMeta& meta, std::uint8_t* dst) const noexcept
	{
		if (!meta.packed)
		{
			if (meta.keyframe) { copyFromLog(meta.offset, m_frameSize, dst); return; }
			copyFromLog(meta.offset, meta.length, m_scratchA.data());
			detail::decodeXorRleApply(m_scratchA.data(), meta.length, dst, m_frameSize);
			return;
		}
		copyFromLog(meta.offset, meta.length, m_scratchB.data());
		if (meta.keyframe)
		{
			(void)m_zstd.decompress(m_scratchB.data(), meta.length, dst, m_frameSize);
			return;
		}
		const std::size_t rleLen = m_zstd.decompress(m_scratchB.data(), meta.length,
		                                             m_scratchA.data(), m_scratchA.size());
		detail::decodeXorRleApply(m_scratchA.data(), rleLen, dst, m_frameSize);
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
		for (std::size_t i = n; i > 0; --i)
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
	std::vector<std::uint8_t> m_log;            ///< 可変長スロットを詰める circular byte log (予算まで伸びる)
	std::size_t               m_logBudget{0};   ///< m_log が伸びてよい上限
	std::size_t               m_logWritePos{0};
	std::size_t               m_logUsedBytes{0};
	std::vector<SlotMeta>     m_meta;            ///< frame index ring と同じ並びの offset/length 表
	std::vector<std::uint8_t> m_lastRaw;         ///< 直前 push の生バイト列 (delta 元)
	bool                      m_hasLastRaw{false};
	std::uint32_t             m_keyframeEvery{60};
	std::uint64_t             m_pushSeq{0};
	std::uint64_t             m_probeRawBytes{0};
	std::uint64_t             m_probeStoredBytes{0};
	bool                      m_rawFallbackAllowed{false};
	bool                      m_fellBackToRaw{false};
	/// push では RLE と zstd の出力先、at() では zstd と RLE の展開先。push と at() は同時に走らないので共有する。
	mutable std::vector<std::uint8_t> m_scratchA;
	mutable std::vector<std::uint8_t> m_scratchB;
	util::ZstdContext                 m_zstd;
	mutable std::vector<std::uint8_t> m_restoreScratch;  ///< at() の復元結果 (返り値の実体)
	mutable std::vector<std::size_t>  m_walkScratch;     ///< at() の keyframe 遡り経路
	mutable std::size_t      m_lastIdx{0};
	mutable bool             m_lastValid{false};
};

}  // namespace mitiru::observe
