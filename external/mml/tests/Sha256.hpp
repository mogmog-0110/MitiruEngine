#pragma once
/// @file Sha256.hpp
/// @brief 検査用の SHA-256 (PCM のハッシュ比較にだけ使う。ライブラリ本体には含めない)

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mitiru_mml_test
{

/// @brief SHA-256 を計算する
/// @details 外部ライブラリを増やさないための最小実装。入力は一括で渡す。
class Sha256
{
public:
	/// @brief バイト列のハッシュを小文字 16 進 64 文字で返す
	[[nodiscard]] static std::string hex(const std::uint8_t* data, std::size_t size)
	{
		std::array<std::uint32_t, 8> h = {
			0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

		std::vector<std::uint8_t> msg(data, data + size);
		msg.push_back(0x80);
		while (msg.size() % 64 != 56)
		{
			msg.push_back(0);
		}
		const std::uint64_t bitLen = static_cast<std::uint64_t>(size) * 8u;
		for (int i = 7; i >= 0; --i)
		{
			msg.push_back(static_cast<std::uint8_t>(bitLen >> (i * 8)));
		}

		for (std::size_t off = 0; off < msg.size(); off += 64)
		{
			processBlock(h, msg.data() + off);
		}

		static constexpr char DIGITS[] = "0123456789abcdef";
		std::string out;
		out.reserve(64);
		for (std::uint32_t word : h)
		{
			for (int shift = 28; shift >= 0; shift -= 4)
			{
				out.push_back(DIGITS[(word >> shift) & 0xF]);
			}
		}
		return out;
	}

	/// @brief 16bit PCM をリトルエンディアンのバイト列として並べたハッシュを返す
	[[nodiscard]] static std::string ofPcm(const std::vector<std::int16_t>& pcm)
	{
		std::vector<std::uint8_t> bytes;
		bytes.reserve(pcm.size() * 2);
		for (const std::int16_t s : pcm)
		{
			const auto u = static_cast<std::uint16_t>(s);
			bytes.push_back(static_cast<std::uint8_t>(u & 0xFF));
			bytes.push_back(static_cast<std::uint8_t>(u >> 8));
		}
		return hex(bytes.data(), bytes.size());
	}

private:
	static std::uint32_t rotr(std::uint32_t x, int n) noexcept
	{
		return (x >> n) | (x << (32 - n));
	}

	static void processBlock(std::array<std::uint32_t, 8>& h, const std::uint8_t* p)
	{
		static constexpr std::uint32_t K[64] = {
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

		std::uint32_t w[64];
		for (int i = 0; i < 16; ++i)
		{
			w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24)
				| (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16)
				| (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8)
				| static_cast<std::uint32_t>(p[i * 4 + 3]);
		}
		for (int i = 16; i < 64; ++i)
		{
			const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
		std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; ++i)
		{
			const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			const std::uint32_t ch = (e & f) ^ (~e & g);
			const std::uint32_t t1 = hh + s1 + ch + K[i] + w[i];
			const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const std::uint32_t t2 = s0 + maj;
			hh = g; g = f; f = e; e = d + t1;
			d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d;
		h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
};

} // namespace mitiru_mml_test
