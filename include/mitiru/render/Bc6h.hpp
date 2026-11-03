#pragma once

/// @file Bc6h.hpp
/// @brief BC6H (符号なし、UF16) の 4x4 ブロックの符号化と復号
/// @details 符号化は mode 11 (1 つの部分集合、10 bit の 2 端点、4 bit の添字) だけを書く。
///          復号も mode 11 だけで、他の mode のブロックは false を返し 0 で埋める (GPU は全 mode を復号する)。
///          画素は半精度のビット列 (0〜0x7BFF) で受け渡す。値の空間が対数に近いので、誤差は相対で測る。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mitiru::render::bc6h
{

inline constexpr std::uint16_t kHalfMax = 0x7BFF;   // 65504

/// 添字 (4 bit) の重み。BC6H / BC7 の 4 bit の表と同じ
inline constexpr std::uint8_t kWeights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

namespace detail
{

/// 10 bit の端点を 16 bit へ広げる (UF16、mode 11)
[[nodiscard]] constexpr std::int32_t unquantize10(std::uint32_t q) noexcept
{
	if (q == 0) { return 0; }
	if (q >= 1023) { return 0xFFFF; }
	return static_cast<std::int32_t>(((q << 16) + 0x8000u) >> 10);
}

/// 補間した 16 bit の値を半精度のビット列にする (UF16)
[[nodiscard]] constexpr std::uint16_t finishUnquantize(std::int32_t v) noexcept
{
	return static_cast<std::uint16_t>((v * 31) >> 6);
}

/// 端点 q を復号したときの半精度のビット列
[[nodiscard]] constexpr std::uint16_t decodedEndpoint(std::uint32_t q) noexcept
{
	return finishUnquantize(unquantize10(q));
}

/// 半精度のビット列 h に最も近い復号値を持つ 10 bit の端点
[[nodiscard]] constexpr std::uint32_t quantizeEndpoint(std::int32_t h) noexcept
{
	if (h <= 0) { return 0; }
	// 1〜1022 の q は 31 q + 15 に復号される
	const std::int32_t q = (h - 15 + 15) / 31;
	const std::uint32_t lo = static_cast<std::uint32_t>(std::clamp(q, 0, 1023));
	std::uint32_t best = lo;
	std::int32_t bestErr = 0x7FFFFFFF;
	for (std::uint32_t c = (lo > 0 ? lo - 1 : 0); c <= std::min(lo + 1, 1023u); ++c)
	{
		const std::int32_t err = std::abs(static_cast<std::int32_t>(decodedEndpoint(c)) - h);
		if (err < bestErr)
		{
			bestErr = err;
			best = c;
		}
	}
	return best;
}

struct BitWriter
{
	std::uint8_t* out;
	std::size_t pos = 0;

	void put(std::uint32_t value, int bits) noexcept
	{
		for (int i = 0; i < bits; ++i, ++pos)
		{
			if ((value >> i) & 1u) { out[pos >> 3] |= static_cast<std::uint8_t>(1u << (pos & 7)); }
		}
	}
};

struct BitReader
{
	const std::uint8_t* in;
	std::size_t pos = 0;

	[[nodiscard]] std::uint32_t get(int bits) noexcept
	{
		std::uint32_t v = 0;
		for (int i = 0; i < bits; ++i, ++pos) { v |= static_cast<std::uint32_t>((in[pos >> 3] >> (pos & 7)) & 1u) << i; }
		return v;
	}
};

/// 復号した端点 e0, e1 と添字から 1 画素の値を出す
[[nodiscard]] constexpr std::uint16_t interpolate(std::int32_t e0, std::int32_t e1, std::uint32_t index) noexcept
{
	const std::int32_t w = kWeights4[index];
	return finishUnquantize((e0 * (64 - w) + e1 * w + 32) >> 6);
}

/// 画素 p (3 成分) に最も近い添字。候補 16 個の 2 乗誤差で選ぶ
[[nodiscard]] inline std::uint32_t bestIndex(const std::int32_t (&e0)[3], const std::int32_t (&e1)[3], const std::uint16_t* p) noexcept
{
	std::uint32_t best = 0;
	std::int64_t bestErr = INT64_MAX;
	for (std::uint32_t idx = 0; idx < 16; ++idx)
	{
		std::int64_t err = 0;
		for (int c = 0; c < 3; ++c)
		{
			const std::int64_t d = static_cast<std::int64_t>(interpolate(e0[c], e1[c], idx)) - p[c];
			err += d * d;
		}
		if (err < bestErr)
		{
			bestErr = err;
			best = idx;
		}
	}
	return best;
}

} // namespace detail

/// @brief 16 画素 (行優先、各 3 成分の半精度ビット列) を mode 11 の 1 ブロック (16 byte) にする
/// @details 端点は、最小と最大の箱から始め、添字を決めては最小 2 乗で端点を引き直すことを 3 回くり返す。
///          画素 0 の添字の最上位 bit は 0 でなければならない (anchor)。1 なら端点を入れ替え添字を反転する
inline void encodeBlockMode11(const std::uint16_t (&px)[48], std::uint8_t (&out)[16]) noexcept
{
	float e0[3], e1[3];
	for (int c = 0; c < 3; ++c)
	{
		std::uint16_t lo = 0xFFFF, hi = 0;
		for (int i = 0; i < 16; ++i)
		{
			lo = std::min(lo, px[i * 3 + c]);
			hi = std::max(hi, px[i * 3 + c]);
		}
		e0[c] = lo;
		e1[c] = hi;
	}
	std::uint32_t q0[3] = {}, q1[3] = {};
	std::uint32_t index[16] = {};
	for (int pass = 0; pass < 3; ++pass)
	{
		std::int32_t d0[3], d1[3];
		for (int c = 0; c < 3; ++c)
		{
			q0[c] = detail::quantizeEndpoint(static_cast<std::int32_t>(e0[c] + 0.5f));
			q1[c] = detail::quantizeEndpoint(static_cast<std::int32_t>(e1[c] + 0.5f));
			d0[c] = detail::unquantize10(q0[c]);
			d1[c] = detail::unquantize10(q1[c]);
		}
		for (int i = 0; i < 16; ++i) { index[i] = detail::bestIndex(d0, d1, &px[i * 3]); }
		if (pass == 2) { break; }
		// 添字を固定して端点を最小 2 乗で引き直す
		float a = 0.0f, b = 0.0f, d = 0.0f;
		float r0[3] = {}, r1[3] = {};
		for (int i = 0; i < 16; ++i)
		{
			const float t = kWeights4[index[i]] / 64.0f;
			const float s = 1.0f - t;
			a += s * s;
			b += s * t;
			d += t * t;
			for (int c = 0; c < 3; ++c)
			{
				r0[c] += s * px[i * 3 + c];
				r1[c] += t * px[i * 3 + c];
			}
		}
		const float det = a * d - b * b;
		if (std::abs(det) < 1e-6f) { break; }
		for (int c = 0; c < 3; ++c)
		{
			e0[c] = std::clamp((d * r0[c] - b * r1[c]) / det, 0.0f, static_cast<float>(kHalfMax));
			e1[c] = std::clamp((a * r1[c] - b * r0[c]) / det, 0.0f, static_cast<float>(kHalfMax));
		}
	}
	if (index[0] >= 8)
	{
		for (int c = 0; c < 3; ++c) { std::swap(q0[c], q1[c]); }
		for (std::uint32_t& i : index) { i = 15 - i; }
	}
	std::memset(out, 0, sizeof(out));
	detail::BitWriter w{out};
	w.put(0x03, 5);
	for (int c = 0; c < 3; ++c) { w.put(q0[c], 10); }
	for (int c = 0; c < 3; ++c) { w.put(q1[c], 10); }
	w.put(index[0], 3);
	for (int i = 1; i < 16; ++i) { w.put(index[i], 4); }
}

/// @brief 1 ブロックを 16 画素 (各 3 成分の半精度ビット列) へ戻す。mode 11 以外は false で 0 を埋める
[[nodiscard]] inline bool decodeBlock(const std::uint8_t (&in)[16], std::uint16_t (&px)[48]) noexcept
{
	std::memset(px, 0, sizeof(px));
	detail::BitReader r{in};
	if (r.get(5) != 0x03) { return false; }
	std::int32_t e0[3], e1[3];
	for (int c = 0; c < 3; ++c) { e0[c] = detail::unquantize10(r.get(10)); }
	for (int c = 0; c < 3; ++c) { e1[c] = detail::unquantize10(r.get(10)); }
	for (int i = 0; i < 16; ++i)
	{
		const std::uint32_t idx = r.get(i == 0 ? 3 : 4);
		for (int c = 0; c < 3; ++c) { px[i * 3 + c] = detail::interpolate(e0[c], e1[c], idx); }
	}
	return true;
}

} // namespace mitiru::render::bc6h
