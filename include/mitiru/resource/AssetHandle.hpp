#pragma once

/// @file AssetHandle.hpp
/// @brief 参照カウント付きアセットハンドル
/// @details shared_ptr ベースのアセットハンドル。
///          アセットの有効性確認とIDによる識別を提供する。

#include <memory>
#include <string>

namespace mitiru::resource
{

/// @brief 参照カウント付きアセットハンドル
/// @tparam T アセット型
/// @details shared_ptr で管理されたアセットへのハンドル。
///          IDで識別し、ロード状態を確認できる。
///
/// @code
/// mitiru::resource::AssetHandle<Texture> handle("player_tex", loadedTexture);
/// if (handle.isLoaded()) {
///     auto* tex = handle.get();
/// }
/// @endcode
template <typename T>
class AssetHandle
{
public:
	/// @brief デフォルトコンストラクタ（未ロード状態）
	AssetHandle() = default;

	/// @brief コンストラクタ
	/// @param id アセットID
	/// @param asset アセットの shared_ptr
	AssetHandle(std::string id, std::shared_ptr<T> asset)
		: m_id(std::move(id))
		, m_asset(std::move(asset))
	{
	}

	/// @brief アセットへの生ポインタを取得する
	/// @return アセットへのポインタ（未ロードの場合は nullptr）
	[[nodiscard]] T* get() const noexcept
	{
		return m_asset.get();
	}

	/// @brief アロー演算子
	/// @return アセットへのポインタ
	[[nodiscard]] T* operator->() const noexcept
	{
		return m_asset.get();
	}

	/// @brief 間接参照演算子
	/// @return アセットへの参照
	[[nodiscard]] T& operator*() const noexcept
	{
		return *m_asset;
	}

	/// @brief アセットがロード済みか判定する
	/// @return ロード済みなら true
	[[nodiscard]] bool isLoaded() const noexcept
	{
		return m_asset != nullptr;
	}

	/// @brief bool変換（isLoaded()と同義）
	[[nodiscard]] explicit operator bool() const noexcept
	{
		return isLoaded();
	}

	/// @brief アセットIDを取得する
	/// @return アセットID
	[[nodiscard]] const std::string& id() const noexcept
	{
		return m_id;
	}

	/// @brief 参照カウントを取得する
	/// @return shared_ptr の参照カウント
	[[nodiscard]] long useCount() const noexcept
	{
		return m_asset.use_count();
	}

	/// @brief アセットをリセットする（未ロード状態に戻す）
	void reset() noexcept
	{
		m_asset.reset();
	}

private:
	std::string m_id;              ///< アセットID
	std::shared_ptr<T> m_asset;    ///< アセット本体
};

} // namespace mitiru::resource
