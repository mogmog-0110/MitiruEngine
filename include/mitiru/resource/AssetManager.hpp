#pragma once

/// @file AssetManager.hpp
/// @brief 中央アセット管理
/// @details 型消去を用いてアセットのロード・キャッシュ・アンロードを
///          一元管理するマネージャー。

#include <any>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/resource/AssetHandle.hpp>
#include <mitiru/resource/IAssetLoader.hpp>
#include <sgc/core/TypeId.hpp>

namespace mitiru::resource
{

/// @brief 中央アセットマネージャー
/// @details 型消去を使い、異なるアセット型を統一的に管理する。
///          ローダーの登録、アセットのロード・取得・アンロードを提供する。
///
/// @code
/// mitiru::resource::AssetManager manager;
/// manager.registerLoader<TextureLoader>(TextureLoader{});
/// auto handle = manager.load<Texture>("player", "textures/player.png");
/// @endcode
class AssetManager
{
public:
	/// @brief ローダーを登録する
	/// @tparam LoaderT AssetLoader コンセプトを満たす型
	/// @param loader ローダーインスタンス
	template <typename LoaderT>
		requires AssetLoader<LoaderT>
	void registerLoader(LoaderT loader)
	{
		using AssetType = typename LoaderT::AssetType;
		const auto typeId = sgc::typeId<AssetType>();
		m_loaders[typeId] = std::make_unique<TypedAssetLoader<LoaderT>>(
			std::move(loader));
	}

	/// @brief アセットをロードする
	/// @tparam T アセット型
	/// @param id アセットID
	/// @param path ファイルパス
	/// @return アセットハンドル
	template <typename T>
	[[nodiscard]] AssetHandle<T> load(const std::string& id, std::string_view path)
	{
		/// キャッシュ確認
		const auto cacheIt = m_cache.find(id);
		if (cacheIt != m_cache.end())
		{
			try
			{
				auto asset = std::any_cast<std::shared_ptr<T>>(cacheIt->second);
				return AssetHandle<T>{id, asset};
			}
			catch (const std::bad_any_cast&)
			{
				return AssetHandle<T>{id, nullptr};
			}
		}

		/// ローダー検索
		const auto typeId = sgc::typeId<T>();
		const auto loaderIt = m_loaders.find(typeId);
		if (loaderIt == m_loaders.end())
		{
			return AssetHandle<T>{id, nullptr};
		}

		/// 型消去経由でロード
		auto result = loaderIt->second->loadAny(path);
		try
		{
			auto asset = std::any_cast<std::shared_ptr<T>>(result);
			m_cache[id] = result;
			return AssetHandle<T>{id, asset};
		}
		catch (const std::bad_any_cast&)
		{
			return AssetHandle<T>{id, nullptr};
		}
	}

	/// @brief アセットを直接登録する（ローダーを経由せず）
	/// @tparam T アセット型
	/// @param id アセットID
	/// @param asset アセットの shared_ptr
	/// @return アセットハンドル
	template <typename T>
	AssetHandle<T> store(const std::string& id, std::shared_ptr<T> asset)
	{
		m_cache[id] = asset;
		return AssetHandle<T>{id, std::move(asset)};
	}

	/// @brief キャッシュからアセットを取得する
	/// @tparam T アセット型
	/// @param id アセットID
	/// @return アセットハンドル（未ロードの場合は空ハンドル）
	template <typename T>
	[[nodiscard]] AssetHandle<T> get(const std::string& id) const
	{
		const auto it = m_cache.find(id);
		if (it == m_cache.end())
		{
			return AssetHandle<T>{id, nullptr};
		}

		try
		{
			auto asset = std::any_cast<std::shared_ptr<T>>(it->second);
			return AssetHandle<T>{id, asset};
		}
		catch (const std::bad_any_cast&)
		{
			return AssetHandle<T>{id, nullptr};
		}
	}

	/// @brief アセットをアンロードする
	/// @param id アセットID
	void unload(const std::string& id)
	{
		m_cache.erase(id);
	}

	/// @brief 指定IDのアセットがロード済みか判定する
	/// @param id アセットID
	/// @return ロード済みなら true
	[[nodiscard]] bool isLoaded(const std::string& id) const
	{
		return m_cache.find(id) != m_cache.end();
	}

	/// @brief ロード済みアセットID一覧を取得する
	/// @return アセットIDのベクタ
	[[nodiscard]] std::vector<std::string> loadedIds() const
	{
		std::vector<std::string> ids;
		ids.reserve(m_cache.size());
		for (const auto& [id, asset] : m_cache)
		{
			ids.push_back(id);
		}
		return ids;
	}

	/// @brief 全アセットをアンロードする
	void unloadAll()
	{
		m_cache.clear();
	}

	/// @brief キャッシュサイズを取得する
	/// @return キャッシュ内のアセット数
	[[nodiscard]] std::size_t cacheSize() const noexcept
	{
		return m_cache.size();
	}

private:
	/// @brief 型ID → ローダー
	std::unordered_map<sgc::TypeIdValue, std::unique_ptr<IAssetLoaderBase>> m_loaders;

	/// @brief アセットID → アセット（型消去）
	std::unordered_map<std::string, std::any> m_cache;
};

} // namespace mitiru::resource
