#pragma once

/// @file Hash.hpp
/// @brief ハッシュユーティリティ
/// @details FNV-1a、CRC32、xxHash (XXH64)、ハッシュ結合、コンパイル時文字列ハッシュを提供する。
///          xxHash の本体は external/xxhash を xxhash_impl (静的ライブラリ) でコンパイルしたもの。
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

#ifndef XXH_NAMESPACE
#define XXH_NAMESPACE mitiru_
#endif
#include <xxhash/xxhash.h>

namespace mitiru::util
{

/// @brief ハッシュユーティリティ（全て静的メソッド）
class Hash
{
public:
	Hash() = delete;

	// ── FNV-1a ──

	/// @brief FNV-1a ハッシュ（バイト列）
	/// @param data データポインタ
	/// @param size データサイズ（バイト）
	/// @return 64 ビットハッシュ値
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

	/// @brief FNV-1a ハッシュ（文字列）
	/// @param str 入力文字列
	/// @return 64 ビットハッシュ値
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

	/// @brief CRC32 ハッシュ（バイト列）
	/// @param data データポインタ
	/// @param size データサイズ（バイト）
	/// @return 32 ビット CRC 値
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

	/// @brief CRC32 ハッシュ（文字列）
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

	// ── xxHash ──

	/// @brief XXH64 (seed 0)
	[[nodiscard]] static std::uint64_t xxhash(const void* data, std::size_t size) noexcept
	{
		return XXH64(data, size, 0);
	}

	// ── ハッシュ結合 ──

	/// @brief 2 つのハッシュ値を結合する
	/// @param seed 結合先のハッシュ値
	/// @param value 結合するハッシュ値
	/// @return 結合されたハッシュ値
	/// @note boost::hash_combine と同等のアルゴリズム
	[[nodiscard]] static constexpr std::size_t hashCombine(std::size_t seed,
	                                                       std::size_t value) noexcept
	{
		seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		return seed;
	}

	// ── コンパイル時文字列ハッシュ ──

	/// @brief コンパイル時文字列ハッシュ型
	/// @details constexpr コンテキストで文字列をハッシュ ID に変換する。
	///
	/// @code
	/// constexpr auto id = StringHash::compute("player_health");
	/// switch (hash) {
	///     case StringHash::compute("player_health"): ...
	/// }
	/// @endcode
	struct StringHash
	{
		/// @brief コンパイル時 FNV-1a ハッシュ
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

		/// @brief std::string_view 版
		[[nodiscard]] static constexpr std::uint64_t compute(std::string_view str) noexcept
		{
			return fnv1a(str);
		}
	};

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
