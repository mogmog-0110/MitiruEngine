#pragma once

/// @file GameMemoryDelta.hpp
/// @brief GameMemoryRing の圧縮に使う 2 段の符号化 (XOR 差分の zero-run RLE と、その後段の zstd)。
/// @details RLE は前フレームと同じ区間を飛ばすだけなので速いが、keyframe と差分の中身は生の
/// まま残る。zstd はそこを詰める (util/ZstdContext.hpp)。

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <mitiru/util/ZstdContext.hpp>

// AVX2 は MSVC の x86/x64 でのみ使う。実行時に CPU の対応を確認し、未対応なら scalar 実装へ切り替える。
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define MITIRU_GMRING_AVX2 1
#include <immintrin.h>
#include <intrin.h>
#endif

/// 語単位の符号化 (encodeXorRleWords 系) がある印。bench/RingLab.hpp が見る。
#define MITIRU_GMRING_WORDS 1

namespace mitiru::observe::detail
{

/// @brief リングが持つ bytes がこの量を超えてから、新しい slot に zstd を掛ける。小さいリングは差分だけで足りて、
///        フレームごとの zstd (数十〜数百 us、keyframe は更に重い) を払わない。大きいリングは zstd で量を抑える。
inline constexpr std::size_t kZstdKickInBytes = 16u * 1024u * 1024u;

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
/// @brief CPU の AVX2 対応。run ごとに呼ぶので、関数内 static の初期化の判定を毎回通らないよう、起動時に 1 度だけ確かめる
///        namespace 直下の変数にしてある (初期化の前に呼ばれても false で、遅い経路を通るだけで結果は同じ)。
inline bool detectAvx2() noexcept
{
	int info[4] = {0, 0, 0, 0};
	__cpuidex(info, 7, 0);
	return (info[1] & (1 << 5)) != 0;  // EBX bit5 = AVX2
}

inline const bool kCpuHasAvx2 = detectAvx2();

inline bool cpuHasAvx2() noexcept { return kCpuHasAvx2; }

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

/// 8 byte の語が丸ごと等しい所の手前までを返す (語の境目は pos から数える)。語の途中の等しい byte は literal に含める。
inline std::size_t literalWordsLenAvx2(const std::uint8_t* prev, const std::uint8_t* cur,
                                       std::size_t pos, std::size_t n, bool& found) noexcept
{
	found = false;
	std::size_t p = pos;
	while (p + 32 <= n)
	{
		const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prev + p));
		const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur + p));
		const unsigned mask = static_cast<unsigned>(_mm256_movemask_pd(_mm256_castsi256_pd(_mm256_cmpeq_epi64(a, b))));
		if (mask != 0)
		{
			unsigned long firstSame;
			_BitScanForward(&firstSame, mask);
			found = true;
			return (p + 8 * firstSame) - pos;
		}
		p += 32;
	}
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
	for (; i + 8 <= len; i += 8)
	{
		std::uint64_t a, b;
		std::memcpy(&a, prev + pos + i, 8);
		std::memcpy(&b, cur + pos + i, 8);
		a ^= b;
		std::memcpy(out + i, &a, 8);
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

/// @brief zeroRunLen の直後に呼ぶ。8 byte の語が丸ごと等しくなる所までを literal にする。float が少しずつ動く状態のように
///        語の中で等しい byte が混ざる所を、1 本の literal にまとめて、run の数と header の byte を減らす。
inline std::size_t literalWordsLen(const std::uint8_t* prev, const std::uint8_t* cur,
                                   std::size_t pos, std::size_t n) noexcept
{
	std::size_t p = pos;
#if defined(MITIRU_GMRING_AVX2)
	if (cpuHasAvx2())
	{
		bool found = false;
		p += literalWordsLenAvx2(prev, cur, pos, n, found);
		if (found) { return p - pos; }
	}
#endif
	while (p + 8 <= n)
	{
		std::uint64_t a, b;
		std::memcpy(&a, prev + p, 8);
		std::memcpy(&b, cur + p, 8);
		if (a == b) { return p - pos; }
		p += 8;
	}
	while (p < n && prev[p] != cur[p]) { ++p; }
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

namespace impl
{

/// @tparam SyncPrev true なら、差分を書いた区間の cur を prev へ写す。成功すれば prev は cur と同じになり、
///         フレーム全体の写しを別に取らなくてよい。0 を返したときは prev が途中までしか写っていない。
template<bool SyncPrev, bool WordLiterals, class Prev>
inline std::size_t encodeXorRleImpl(Prev* prev, const std::uint8_t* cur, std::size_t n, std::uint8_t* out, std::size_t outCap)
{
	std::size_t pos = 0, w = 0;
	while (pos < n)
	{
		const std::size_t zeroLen = zeroRunLen(prev, cur, pos, n);
		pos += zeroLen;
		std::size_t litLen;
		if constexpr (WordLiterals) { litLen = literalWordsLen(prev, cur, pos, n); }
		else { litLen = literalRunLen(prev, cur, pos, n); }
		if (!putVarint(static_cast<std::uint32_t>(zeroLen), out, outCap, w)) { return 0; }
		if (!putVarint(static_cast<std::uint32_t>(litLen), out, outCap, w)) { return 0; }
		if (litLen > 0)
		{
			if (w + litLen > outCap) { return 0; }
			xorCopy(out + w, prev, cur, pos, litLen);
			if constexpr (SyncPrev) { std::memcpy(prev + pos, cur + pos, litLen); }
			w += litLen;
		}
		pos += litLen;
	}
	return w;
}

}  // namespace impl

/// @brief prev から cur への XOR 差分を、skip、litLen、lit bytes の zero-run RLE 形式で @p out に書く。
///        容量不足なら 0 を返す
inline std::size_t encodeXorRle(const std::uint8_t* prev, const std::uint8_t* cur,
                                std::size_t n, std::uint8_t* out, std::size_t outCap)
{
	return impl::encodeXorRleImpl<false, false>(prev, cur, n, out, outCap);
}

/// @brief encodeXorRle と同じ bytes を書き、続けて prev を cur と同じにする (差分の区間だけ写す)。
///        0 を返したときは prev が cur と同じとは限らないので、呼び手が全体を写す。
inline std::size_t encodeXorRleSync(std::uint8_t* prev, const std::uint8_t* cur,
                                    std::size_t n, std::uint8_t* out, std::size_t outCap)
{
	return impl::encodeXorRleImpl<true, false>(prev, cur, n, out, outCap);
}

/// @brief encodeXorRleSync の literal を 8 byte の語単位でまとめる版。形は同じで decodeXorRleApply が読める。
///        literal に等しい byte が混ざる分だけ出力が少し増えるが、run の数が減る。リングの記録用。
inline std::size_t encodeXorRleWordsSync(std::uint8_t* prev, const std::uint8_t* cur,
                                         std::size_t n, std::uint8_t* out, std::size_t outCap)
{
	return impl::encodeXorRleImpl<true, true>(prev, cur, n, out, outCap);
}

/// @brief encodeXorRleWordsSync の、prev を更新しない版 (長さが違う前のフレームを伸ばした物と比べるとき)。
inline std::size_t encodeXorRleWords(const std::uint8_t* prev, const std::uint8_t* cur,
                                     std::size_t n, std::uint8_t* out, std::size_t outCap)
{
	return impl::encodeXorRleImpl<false, true>(prev, cur, n, out, outCap);
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

/// @brief varint を 1 個読む。末尾を越えるか 32bit に収まらなければ false
inline bool getVarintChecked(const std::uint8_t* in, std::size_t len, std::size_t& r, std::uint32_t& v) noexcept
{
	v = 0;
	for (int shift = 0; shift < 35; shift += 7)
	{
		if (r >= len) { return false; }
		const std::uint8_t b = in[r++];
		v |= static_cast<std::uint32_t>(b & 0x7Fu) << shift;
		if ((b & 0x80u) == 0) { return shift < 28 || b < 0x10u; }
	}
	return false;
}

/// @brief decodeXorRleApply の、回線から来た差分用。範囲の外を指す差分なら途中で止めて false
inline bool decodeXorRleApplyChecked(const std::uint8_t* enc, std::size_t encLen,
                                     std::uint8_t* buf, std::size_t n) noexcept
{
	std::size_t pos = 0, r = 0;
	while (r < encLen)
	{
		std::uint32_t skip = 0, lit = 0;
		if (!getVarintChecked(enc, encLen, r, skip) || skip > n - pos) { return false; }
		pos += skip;
		if (!getVarintChecked(enc, encLen, r, lit) || lit > n - pos || lit > encLen - r) { return false; }
		for (std::uint32_t i = 0; i < lit; ++i) { buf[pos + i] ^= enc[r + i]; }
		r   += lit;
		pos += lit;
	}
	return true;
}

}  // namespace mitiru::observe::detail
