#pragma once

/// @file AssetHandle.hpp
/// @brief 軽量アセット参照ハンドル
///
/// ID＋世代番号による安全なアセット参照。
/// ダングリング参照をisValid()で検出できる。
///
/// @code
/// sgc::asset::AssetHandle<Texture> handle{0, 1};
/// if (handle.isValid())
/// {
///     // アセットにアクセス
/// }
///
/// // 無効なハンドル
/// auto null = sgc::asset::AssetHandle<Texture>::null();
/// assert(!null.isValid());
/// @endcode

#include <cstdint>
#include <functional>
#include <limits>

namespace sgc::asset
{

/// @brief 軽量アセット参照ハンドル
///
/// インデックスと世代番号のペアでアセットを参照する。
/// アセットが解放された後の不正アクセスを世代番号で検出する。
///
/// @tparam T アセットの型（型安全性のためのタグとして機能）
template <typename T>
struct AssetHandle
{
	/// @brief インデックス型
	using IndexType = std::uint32_t;

	/// @brief 世代番号型
	using GenerationType = std::uint32_t;

	/// @brief 無効なインデックス値
	static constexpr IndexType INVALID_INDEX = std::numeric_limits<IndexType>::max();

	IndexType index{INVALID_INDEX};       ///< スロットインデックス
	GenerationType generation{0};         ///< 世代番号

	/// @brief ハンドルが有効か判定する
	/// @return 有効なインデックスであればtrue
	[[nodiscard]] constexpr bool isValid() const noexcept
	{
		return index != INVALID_INDEX;
	}

	/// @brief bool変換（有効性チェック）
	[[nodiscard]] constexpr explicit operator bool() const noexcept
	{
		return isValid();
	}

	/// @brief 無効なハンドルを生成する
	/// @return 無効なハンドル定数
	[[nodiscard]] static constexpr AssetHandle null() noexcept
	{
		return AssetHandle{};
	}

	/// @brief 等値比較
	[[nodiscard]] constexpr bool operator==(const AssetHandle&) const noexcept = default;
};

} // namespace sgc::asset

/// @brief AssetHandle用ハッシュ特殊化
template <typename T>
struct std::hash<sgc::asset::AssetHandle<T>>
{
	std::size_t operator()(const sgc::asset::AssetHandle<T>& h) const noexcept
	{
		return std::hash<std::uint64_t>{}(
			(static_cast<std::uint64_t>(h.index) << 32) | h.generation);
	}
};
