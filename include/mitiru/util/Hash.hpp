#pragma once

/// @file Hash.hpp
/// @brief ハッシュユーティリティ
/// @details FNV-1a、CRC32、xxHash（簡易版）、ハッシュ結合、
///          コンパイル時文字列ハッシュを提供する。全て header-only、外部依存なし。
///
/// @code
/// using namespace mitiru::util;
///
/// // FNV-1a
/// auto h1 = Hash::fnv1a("hello");
///
/// // CRC32
/// auto h2 = Hash::crc32("world", 5);
///
/// // コンパイル時ハッシュ
/// constexpr auto id = "player_health"_hash;
///
/// // ハッシュ結合
/// std::size_t combined = Hash::hashCombine(0, 42);
/// combined = Hash::hashCombine(combined, 99);
/// @endcode

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mitiru::util
{

/// @brief ハッシュユーティリティ（全て静的メソッド）
class Hash
{
public:
	Hash() = delete;

	// ── FNV-1a ──

	/// @brief FNV-1aハッシュ（バイト列）
	/// @param data データポインタ
	/// @param size データサイズ（バイト）
	/// @return 64ビットハッシュ値
	[[nodiscard]] static constexpr std::uint64_t fnv1a(const void* data,
	                                                   std::size_t size) noexcept
	{
		constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
		constexpr std::uint64_t kPrime = 1099511628211ULL;

		if (data == nullptr) return kOffsetBasis;

		auto bytes = static_cast<const std::uint8_t*>(data);
		std::uint64_t hash = kOffsetBasis;

		for (std::size_t i = 0; i < size; ++i)
		{
			hash ^= static_cast<std::uint64_t>(bytes[i]);
			hash *= kPrime;
		}
		return hash;
	}

	/// @brief FNV-1aハッシュ（文字列）
	/// @param str 入力文字列
	/// @return 64ビットハッシュ値
	[[nodiscard]] static constexpr std::uint64_t fnv1a(std::string_view str) noexcept
	{
		constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
		constexpr std::uint64_t kPrime = 1099511628211ULL;

		std::uint64_t hash = kOffsetBasis;
		for (char c : str)
		{
			hash ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(c));
			hash *= kPrime;
		}
		return hash;
	}

	// ── CRC32 ──

	/// @brief CRC32ハッシュ（バイト列）
	/// @param data データポインタ
	/// @param size データサイズ（バイト）
	/// @return 32ビットCRC値
	[[nodiscard]] static constexpr std::uint32_t crc32(const void* data,
	                                                   std::size_t size) noexcept
	{
		if (data == nullptr) return 0;

		auto bytes = static_cast<const std::uint8_t*>(data);
		std::uint32_t crc = 0xFFFFFFFFu;

		for (std::size_t i = 0; i < size; ++i)
		{
			crc ^= static_cast<std::uint32_t>(bytes[i]);
			for (int bit = 0; bit < 8; ++bit)
			{
				if (crc & 1u)
				{
					crc = (crc >> 1u) ^ 0xEDB88320u;
				}
				else
				{
					crc >>= 1u;
				}
			}
		}
		return ~crc;
	}

	/// @brief CRC32ハッシュ（文字列）
	[[nodiscard]] static constexpr std::uint32_t crc32(std::string_view str) noexcept
	{
		std::uint32_t crc = 0xFFFFFFFFu;
		for (char c : str)
		{
			crc ^= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c));
			for (int bit = 0; bit < 8; ++bit)
			{
				if (crc & 1u)
				{
					crc = (crc >> 1u) ^ 0xEDB88320u;
				}
				else
				{
					crc >>= 1u;
				}
			}
		}
		return ~crc;
	}

	// ── xxHash（簡易版） ──

	/// @brief xxHash風の高速非暗号ハッシュ（簡易実装）
	/// @param data データポインタ
	/// @param size データサイズ（バイト）
	/// @return 64ビットハッシュ値
	/// @note 本実装はxxHash64のアルゴリズムを簡略化したもの。
	///       完全な互換性は保証しない。高速な非暗号ハッシュとして使用する。
	[[nodiscard]] static constexpr std::uint64_t xxhash(const void* data,
	                                                    std::size_t size) noexcept
	{
		constexpr std::uint64_t kPrime1 = 11400714785074694791ULL;
		constexpr std::uint64_t kPrime2 = 14029467366897019727ULL;
		constexpr std::uint64_t kPrime3 = 1609587929392839161ULL;
		constexpr std::uint64_t kPrime4 = 9650029242287828579ULL;
		constexpr std::uint64_t kPrime5 = 2870177450012600261ULL;

		if (data == nullptr) return 0;

		auto bytes = static_cast<const std::uint8_t*>(data);
		const std::size_t totalSize = size;
		std::uint64_t hash = 0;

		if (size >= 32)
		{
			std::uint64_t v1 = kPrime1 + kPrime2;
			std::uint64_t v2 = kPrime2;
			std::uint64_t v3 = 0;
			std::uint64_t v4 = 0 - kPrime1;

			std::size_t remaining = size;
			while (remaining >= 32)
			{
				v1 = xxhashRound(v1, read64(bytes));
				bytes += 8;
				v2 = xxhashRound(v2, read64(bytes));
				bytes += 8;
				v3 = xxhashRound(v3, read64(bytes));
				bytes += 8;
				v4 = xxhashRound(v4, read64(bytes));
				bytes += 8;
				remaining -= 32;
			}

			hash = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
			hash = mergeRound(hash, v1);
			hash = mergeRound(hash, v2);
			hash = mergeRound(hash, v3);
			hash = mergeRound(hash, v4);

			size = remaining;
		}
		else
		{
			hash = kPrime5;
		}

		hash += static_cast<std::uint64_t>(totalSize);

		// 残りバイトの処理
		while (size >= 8)
		{
			std::uint64_t k1 = xxhashRound(0, read64(bytes));
			bytes += 8;
			hash ^= k1;
			hash = rotl64(hash, 27) * kPrime1 + kPrime4;
			size -= 8;
		}

		while (size >= 4)
		{
			hash ^= static_cast<std::uint64_t>(read32(bytes)) * kPrime1;
			bytes += 4;
			hash = rotl64(hash, 23) * kPrime2 + kPrime3;
			size -= 4;
		}

		while (size > 0)
		{
			hash ^= static_cast<std::uint64_t>(*bytes) * kPrime5;
			++bytes;
			hash = rotl64(hash, 11) * kPrime1;
			--size;
		}

		// ファイナライズ
		hash ^= hash >> 33;
		hash *= kPrime2;
		hash ^= hash >> 29;
		hash *= kPrime3;
		hash ^= hash >> 32;

		return hash;
	}

	// ── ハッシュ結合 ──

	/// @brief 2つのハッシュ値を結合する
	/// @param seed 結合先のハッシュ値
	/// @param value 結合するハッシュ値
	/// @return 結合されたハッシュ値
	/// @note boost::hash_combineと同等のアルゴリズム
	[[nodiscard]] static constexpr std::size_t hashCombine(std::size_t seed,
	                                                       std::size_t value) noexcept
	{
		seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		return seed;
	}

	// ── コンパイル時文字列ハッシュ ──

	/// @brief コンパイル時文字列ハッシュ型
	/// @details constexprコンテキストで文字列をハッシュIDに変換する。
	///
	/// @code
	/// constexpr auto id = StringHash::compute("player_health");
	/// switch (hash) {
	///     case StringHash::compute("player_health"): ...
	/// }
	/// @endcode
	struct StringHash
	{
		/// @brief コンパイル時FNV-1aハッシュ
		[[nodiscard]] static constexpr std::uint64_t compute(const char* str) noexcept
		{
			constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
			constexpr std::uint64_t kPrime = 1099511628211ULL;

			std::uint64_t hash = kOffsetBasis;
			while (*str != '\0')
			{
				hash ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(*str));
				hash *= kPrime;
				++str;
			}
			return hash;
		}

		/// @brief std::string_view版
		[[nodiscard]] static constexpr std::uint64_t compute(std::string_view str) noexcept
		{
			return fnv1a(str);
		}
	};

private:
	// ── xxHash内部ヘルパー ──

	[[nodiscard]] static constexpr std::uint64_t rotl64(std::uint64_t x, int r) noexcept
	{
		return (x << r) | (x >> (64 - r));
	}

	[[nodiscard]] static constexpr std::uint64_t xxhashRound(std::uint64_t acc,
	                                                          std::uint64_t input) noexcept
	{
		constexpr std::uint64_t kPrime2 = 14029467366897019727ULL;
		constexpr std::uint64_t kPrime1 = 11400714785074694791ULL;
		acc += input * kPrime2;
		acc = rotl64(acc, 31);
		acc *= kPrime1;
		return acc;
	}

	[[nodiscard]] static constexpr std::uint64_t mergeRound(std::uint64_t acc,
	                                                         std::uint64_t val) noexcept
	{
		constexpr std::uint64_t kPrime1 = 11400714785074694791ULL;
		constexpr std::uint64_t kPrime4 = 9650029242287828579ULL;
		val = xxhashRound(0, val);
		acc ^= val;
		acc = acc * kPrime1 + kPrime4;
		return acc;
	}

	/// @brief リトルエンディアンで8バイト読み取り
	[[nodiscard]] static constexpr std::uint64_t read64(const std::uint8_t* p) noexcept
	{
		return static_cast<std::uint64_t>(p[0])
		     | (static_cast<std::uint64_t>(p[1]) << 8)
		     | (static_cast<std::uint64_t>(p[2]) << 16)
		     | (static_cast<std::uint64_t>(p[3]) << 24)
		     | (static_cast<std::uint64_t>(p[4]) << 32)
		     | (static_cast<std::uint64_t>(p[5]) << 40)
		     | (static_cast<std::uint64_t>(p[6]) << 48)
		     | (static_cast<std::uint64_t>(p[7]) << 56);
	}

	/// @brief リトルエンディアンで4バイト読み取り
	[[nodiscard]] static constexpr std::uint32_t read32(const std::uint8_t* p) noexcept
	{
		return static_cast<std::uint32_t>(p[0])
		     | (static_cast<std::uint32_t>(p[1]) << 8)
		     | (static_cast<std::uint32_t>(p[2]) << 16)
		     | (static_cast<std::uint32_t>(p[3]) << 24);
	}
};

/// @brief ユーザー定義リテラル: コンパイル時文字列ハッシュ
/// @code
/// using namespace mitiru::util::literals;
/// constexpr auto id = "player_health"_hash;
/// @endcode
namespace literals
{

[[nodiscard]] consteval std::uint64_t operator""_hash(const char* str, std::size_t len) noexcept
{
	constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
	constexpr std::uint64_t kPrime = 1099511628211ULL;

	std::uint64_t hash = kOffsetBasis;
	for (std::size_t i = 0; i < len; ++i)
	{
		hash ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(str[i]));
		hash *= kPrime;
	}
	return hash;
}

} // namespace literals

} // namespace mitiru::util
