#pragma once

/// @file ZstdContext.hpp
/// @brief フレームごとに呼ぶ zstd の圧縮・展開 (巻き戻しリング、.mtrr の入力)。
/// @details util/Compression.hpp は呼ぶたびに vector を返す。こちらは呼び手の領域へ書き、文脈も
/// 使い回すので、毎フレームの経路で確保しない。zstd が無い構成 (external/zstd 不在) では
/// available() が false になり、compress / decompress は 0 を返す。

#include <cstddef>
#include <cstdint>
#include <memory>

#if defined(MITIRU_HAS_ZSTD)
#include <zstd.h>
#endif

namespace mitiru::util
{

/// @brief 圧縮・展開の文脈を 1 度だけ作り、呼ぶたびに確保しない。
class ZstdContext
{
public:
	/// @brief 使えるか (zstd を含む構成か、文脈の確保に成功したか)
	[[nodiscard]] bool available() const noexcept
	{
#if defined(MITIRU_HAS_ZSTD)
		return m_cctx != nullptr && m_dctx != nullptr;
#else
		return false;
#endif
	}

	void open()
	{
#if defined(MITIRU_HAS_ZSTD)
		if (!m_cctx) { m_cctx.reset(ZSTD_createCCtx()); }
		if (!m_dctx) { m_dctx.reset(ZSTD_createDCtx()); }
#endif
	}

	void close() noexcept
	{
#if defined(MITIRU_HAS_ZSTD)
		m_cctx.reset();
		m_dctx.reset();
#endif
	}

	/// @return 書いた byte 数。dstCap に収まらない (= 縮まない) ときは 0
	[[nodiscard]] std::size_t compress(const std::uint8_t* src, std::size_t len,
	                                   std::uint8_t* dst, std::size_t dstCap) const noexcept
	{
#if defined(MITIRU_HAS_ZSTD)
		if (!available()) { return 0; }
		const std::size_t n = ZSTD_compressCCtx(m_cctx.get(), dst, dstCap, src, len, kLevel);
		return ZSTD_isError(n) ? 0 : n;
#else
		(void)src; (void)len; (void)dst; (void)dstCap;
		return 0;
#endif
	}

	/// @return 展開した byte 数。壊れていれば 0
	[[nodiscard]] std::size_t decompress(const std::uint8_t* src, std::size_t len,
	                                     std::uint8_t* dst, std::size_t dstCap) const noexcept
	{
#if defined(MITIRU_HAS_ZSTD)
		if (!available()) { return 0; }
		const std::size_t n = ZSTD_decompressDCtx(m_dctx.get(), dst, dstCap, src, len);
		return ZSTD_isError(n) ? 0 : n;
#else
		(void)src; (void)len; (void)dst; (void)dstCap;
		return 0;
#endif
	}

private:
#if defined(MITIRU_HAS_ZSTD)
	/// 毎フレーム通るので最速の段にする。ゲームの状態や入力の差分は段を上げてもほとんど縮まない。
	static constexpr int kLevel = 1;
	struct CctxFree { void operator()(ZSTD_CCtx* c) const noexcept { ZSTD_freeCCtx(c); } };
	struct DctxFree { void operator()(ZSTD_DCtx* d) const noexcept { ZSTD_freeDCtx(d); } };
	std::unique_ptr<ZSTD_CCtx, CctxFree> m_cctx;
	std::unique_ptr<ZSTD_DCtx, DctxFree> m_dctx;
#endif
};

}  // namespace mitiru::util
