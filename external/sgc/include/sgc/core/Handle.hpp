#pragma once

/// @file Handle.hpp
/// @brief 型安全な世代管理付きハンドル
///
/// Tag型でリソース種別を区別し、世代番号でABA問題を防止する。
/// Entity.hppのパターンを汎用化した設計。
///
/// @code
/// struct TextureTag {};
/// struct SoundTag {};
/// using TextureHandle = sgc::Handle<TextureTag>;
/// using SoundHandle = sgc::Handle<SoundTag>;
/// // TextureHandle と SoundHandle は型が異なるため混同できない
/// @endcode

#include <cstdint>
#include <functional>
#include <limits>

namespace sgc
{

/// @brief 世代管理付き汎用ハンドル
///
/// Tag型パラメータにより、異なるリソース種別のハンドルを型レベルで区別する。
/// インデックスと世代番号のペアで構成され、解放後の再利用を安全に検出できる。
///
/// @tparam Tag リソース種別を識別するタグ型
template <typename Tag>
struct Handle
{
	/// @brief インデックス型
	using IndexType = std::uint32_t;

	/// @brief 世代番号型
	using GenerationType = std::uint32_t;

	/// @brief 無効なインデックス値
	static constexpr IndexType INVALID_INDEX = std::numeric_limits<IndexType>::max();

	IndexType index{INVALID_INDEX};    ///< スロットインデックス
	GenerationType generation{0};      ///< 世代番号

	/// @brief ハンドルが有効かどうか
	/// @return 有効なインデックスであればtrue
	[[nodiscard]] constexpr bool isValid() const noexcept
	{
		return index != INVALID_INDEX;
	}

	/// @brief 等値比較
	[[nodiscard]] constexpr bool operator==(const Handle&) const noexcept = default;
};

} // namespace sgc

/// @brief Handle用ハッシュ特殊化
template <typename Tag>
struct std::hash<sgc::Handle<Tag>>
{
	std::size_t operator()(const sgc::Handle<Tag>& h) const noexcept
	{
		return std::hash<std::uint64_t>{}(
			(static_cast<std::uint64_t>(h.index) << 32) | h.generation);
	}
};
