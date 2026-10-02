#pragma once

/// @file GameMemoryDelta.hpp
/// @brief GameMemoryRing の圧縮に使う 2 段の符号化 (XOR 差分の zero-run RLE と、その後段の zstd)。
/// @details RLE は前フレームと同じ区間を飛ばすだけなので速いが、keyframe と差分の中身は生の
/// まま残る。zstd はそこを詰める (util/ZstdContext.hpp)。

#include <cstddef>
#include <cstdint>

#include <mitiru/util/ZstdContext.hpp>

// AVX2 は MSVC の x86/x64 でのみ使う。実行時に CPU の対応を確認し、未対応なら scalar 実装へ切り替える。
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define MITIRU_GMRING_AVX2 1
#include <immintrin.h>
#include <intrin.h>
#endif

namespace mitiru::observe::detail
{

/// @brief LEB128 形式の varint を 1 個書く。容量不足なら false を返す
inline bool putVarint(std::uint32_t v, std::uint8_t* out, std::size_t cap, std::size_t& w)
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

inline std::uint32_t getVarint(const std::uint8_t* in, std::size_t& r) noexcept
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
inline bool cpuHasAvx2() noexcept
{
	static const bool has = [] {
		int info[4] = {0, 0, 0, 0};
		__cpuidex(info, 7, 0);
		return (info[1] & (1 << 5)) != 0;  // EBX bit5 = AVX2
	}();
	return has;
}

inline std::size_t zeroRunLenAvx2(const std::uint8_t* prev, const std::uint8_t* cur,
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

inline std::size_t literalRunLenAvx2(const std::uint8_t* prev, const std::uint8_t* cur,
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

inline void xorCopyAvx2(std::uint8_t* out, const std::uint8_t* prev, const std::uint8_t* cur,
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

inline std::size_t zeroRunLen(const std::uint8_t* prev, const std::uint8_t* cur,
                              std::size_t pos, std::size_t n) noexcept
{
#if defined(MITIRU_GMRING_AVX2)
	if (cpuHasAvx2()) { return zeroRunLenAvx2(prev, cur, pos, n); }
#endif
	std::size_t p = pos;
	while (p < n && prev[p] == cur[p]) { ++p; }
	return p - pos;
}

inline std::size_t literalRunLen(const std::uint8_t* prev, const std::uint8_t* cur,
                                 std::size_t pos, std::size_t n) noexcept
{
#if defined(MITIRU_GMRING_AVX2)
	if (cpuHasAvx2()) { return literalRunLenAvx2(prev, cur, pos, n); }
#endif
	std::size_t p = pos;
	while (p < n && prev[p] != cur[p]) { ++p; }
	return p - pos;
}

inline void xorCopy(std::uint8_t* out, const std::uint8_t* prev, const std::uint8_t* cur,
                    std::size_t pos, std::size_t len) noexcept
{
#if defined(MITIRU_GMRING_AVX2)
	if (cpuHasAvx2()) { xorCopyAvx2(out, prev, cur, pos, len); return; }
#endif
	for (std::size_t i = 0; i < len; ++i) { out[i] = static_cast<std::uint8_t>(cur[pos + i] ^ prev[pos + i]); }
}

/// @brief prev から cur への XOR 差分を、skip、litLen、lit bytes の zero-run RLE 形式で @p out に書く。
///        容量不足なら 0 を返す
inline std::size_t encodeXorRle(const std::uint8_t* prev, const std::uint8_t* cur,
                                std::size_t n, std::uint8_t* out, std::size_t outCap)
{
	std::size_t pos = 0, w = 0;
	while (pos < n)
	{
		const std::size_t zeroLen = zeroRunLen(prev, cur, pos, n);
		pos += zeroLen;
		const std::size_t litLen = literalRunLen(prev, cur, pos, n);
		if (!putVarint(static_cast<std::uint32_t>(zeroLen), out, outCap, w)) { return 0; }
		if (!putVarint(static_cast<std::uint32_t>(litLen), out, outCap, w)) { return 0; }
		if (litLen > 0)
		{
			if (w + litLen > outCap) { return 0; }
			xorCopy(out + w, prev, cur, pos, litLen);
			w += litLen;
		}
		pos += litLen;
	}
	return w;
}

/// @brief zero-run RLE 差分を、直前のフレームを保持する @p buf に XOR で適用する
inline void decodeXorRleApply(const std::uint8_t* enc, std::size_t encLen,
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

}  // namespace mitiru::observe::detail
