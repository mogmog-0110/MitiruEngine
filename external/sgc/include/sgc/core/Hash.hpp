#pragma once

/// @file Hash.hpp
/// @brief コンパイル時ハッシュユーティリティ
///
/// FNV-1aハッシュ関数とユーザー定義リテラルを提供する。
/// ActionMapなどで文字列キーの高速比較に使用する。
///
/// @code
/// using namespace sgc::literals;
/// constexpr auto h = "jump"_hash;
/// static_assert(h == sgc::fnv1aHash("jump"));
/// @endcode

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sgc
{

/// @brief FNV-1aオフセットベース（64bit）
constexpr std::uint64_t FNV1A_OFFSET_BASIS = 14695981039346656037ULL;

/// @brief FNV-1aプライム（64bit）
constexpr std::uint64_t FNV1A_PRIME = 1099511628211ULL;

/// @brief FNV-1aハッシュを計算する（constexpr対応）
/// @param s 入力文字列
/// @return 64bitハッシュ値
[[nodiscard]] constexpr std::uint64_t fnv1aHash(std::string_view s) noexcept
{
	std::uint64_t hash = FNV1A_OFFSET_BASIS;
	for (const char c : s)
	{
		hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
		hash *= FNV1A_PRIME;
	}
	return hash;
}

/// @brief 文字列ハッシュラッパー（unordered_map等で使用可能）
///
/// @code
/// sgc::StringHash h("player");
/// uint64_t val = h.value();
/// @endcode
struct StringHash
{
	std::uint64_t m_hash{0};  ///< ハッシュ値

	/// @brief デフォルトコンストラクタ
	constexpr StringHash() noexcept = default;

	/// @brief 文字列からハッシュを生成する
	/// @param s 入力文字列
	constexpr explicit StringHash(std::string_view s) noexcept
		: m_hash(fnv1aHash(s))
	{
	}

	/// @brief ハッシュ値から直接構築する
	/// @param hash ハッシュ値
	constexpr explicit StringHash(std::uint64_t hash) noexcept
		: m_hash(hash)
	{
	}

	/// @brief ハッシュ値を取得する
	/// @return 64bitハッシュ値
	[[nodiscard]] constexpr std::uint64_t value() const noexcept { return m_hash; }

	/// @brief uint64_tへの暗黙変換
	[[nodiscard]] constexpr operator std::uint64_t() const noexcept { return m_hash; }

	/// @brief 等値比較
	[[nodiscard]] constexpr bool operator==(const StringHash& rhs) const noexcept = default;
};

/// @brief ユーザー定義リテラル名前空間
namespace literals
{

/// @brief 文字列リテラルからハッシュを生成するユーザー定義リテラル
/// @param str 文字列
/// @param len 文字列長
/// @return 64bitハッシュ値
///
/// @code
/// using namespace sgc::literals;
/// constexpr auto h = "hello"_hash;
/// @endcode
[[nodiscard]] constexpr std::uint64_t operator""_hash(const char* str, std::size_t len) noexcept
{
	return fnv1aHash(std::string_view(str, len));
}

} // namespace literals

} // namespace sgc
