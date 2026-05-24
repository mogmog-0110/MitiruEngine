#pragma once

/// @file ResourceManager.hpp
/// @brief 汎用リソースマネージャ
///
/// ハンドルベースのリソース管理。同期/非同期ロード対応。
/// パス→ハンドルの逆引きで重複読み込みを防止する。
///
/// @code
/// struct TextureLoader {
///     sgc::Result<Texture> load(const std::string& path) {
///         return Texture::fromFile(path);
///     }
/// };
///
/// sgc::ResourceManager<Texture> textures(pool);
/// auto h = textures.load("player.png", TextureLoader{});
/// auto* tex = textures.get(h);
/// @endcode

#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/core/HandleMap.hpp"
#include "sgc/core/ThreadPool.hpp"
#include "sgc/resource/ResourceHandle.hpp"
#include "sgc/types/Result.hpp"

namespace sgc
{

/// @brief ResourceLoaderコンセプト — load(path) -> Result<T> を持つ型
template <typename L, typename T>
concept ResourceLoader = requires(L loader, const std::string& path) {
	{ loader.load(path) } -> std::same_as<Result<T>>;
};

/// @brief 汎用リソースマネージャ
/// @tparam T リソース型
template <typename T>
class ResourceManager
{
public:
	/// @brief リソースマネージャを構築する
	/// @param pool スレッドプール参照（非同期ロード用）
	explicit ResourceManager(ThreadPool& pool)
		: m_pool(&pool)
	{
	}

	/// @brief リソースを同期ロードする
	/// @tparam Loader ResourceLoaderコンセプトを満たすローダー型
	/// @param path リソースパス
	/// @param loader ローダーインスタンス
	/// @return リソースハンドル
	template <typename Loader>
		requires ResourceLoader<Loader, T>
	[[nodiscard]] ResourceHandle load(const std::string& path, Loader loader)
	{
		// 重複チェック
		auto existingHandle = findByPath(path);
		if (existingHandle.has_value()) return existingHandle.value();

		auto result = loader.load(path);
		if (result)
		{
			auto handle = m_resources.insert(
				ResourceEntry{std::move(result.value()), ResourceState::Loaded, path});
			m_pathToHandle[path] = handle;
			return handle;
		}

		// エラー時もハンドルを発行（状態はError）
		auto handle = m_resources.insert(
			ResourceEntry{T{}, ResourceState::Error, path});
		m_pathToHandle[path] = handle;
		return handle;
	}

	/// @brief リソースを非同期ロードする
	/// @tparam Loader ResourceLoaderコンセプトを満たすローダー型
	/// @param path リソースパス
	/// @param loader ローダーインスタンス
	/// @return リソースハンドル（ロード完了まで状態はLoading）
	template <typename Loader>
		requires ResourceLoader<Loader, T>
	[[nodiscard]] ResourceHandle loadAsync(const std::string& path, Loader loader)
	{
		// 重複チェック
		auto existingHandle = findByPath(path);
		if (existingHandle.has_value()) return existingHandle.value();

		// Loading状態でハンドルを発行
		auto handle = m_resources.insert(
			ResourceEntry{T{}, ResourceState::Loading, path});
		m_pathToHandle[path] = handle;

		// 非同期でロード
		auto future = m_pool->submit(
			[this, handle, path, loader = std::move(loader)]() mutable {
				auto result = loader.load(path);
				std::scoped_lock lock(m_mutex);
				auto* entry = m_resources.get(handle);
				if (entry)
				{
					if (result)
					{
						entry->resource = std::move(result.value());
						entry->state = ResourceState::Loaded;
					}
					else
					{
						entry->state = ResourceState::Error;
					}
				}
			}
		);
		m_pendingFutures.push_back(std::move(future));

		return handle;
	}

	/// @brief ハンドルからリソースを取得する
	/// @param handle リソースハンドル
	/// @return リソースへのポインタ（未ロードまたは無効ならnullptr）
	[[nodiscard]] T* get(ResourceHandle handle)
	{
		auto* entry = m_resources.get(handle);
		if (!entry || entry->state != ResourceState::Loaded) return nullptr;
		return &entry->resource;
	}

	/// @brief ハンドルからリソースを取得する（const版）
	/// @param handle リソースハンドル
	/// @return リソースへのconstポインタ
	[[nodiscard]] const T* get(ResourceHandle handle) const
	{
		const auto* entry = m_resources.get(handle);
		if (!entry || entry->state != ResourceState::Loaded) return nullptr;
		return &entry->resource;
	}

	/// @brief リソースの状態を取得する
	/// @param handle リソースハンドル
	/// @return リソース状態
	[[nodiscard]] ResourceState getState(ResourceHandle handle) const
	{
		const auto* entry = m_resources.get(handle);
		if (!entry) return ResourceState::Unloaded;
		return entry->state;
	}

	/// @brief リソースを解放する
	/// @param handle リソースハンドル
	void release(ResourceHandle handle)
	{
		auto* entry = m_resources.get(handle);
		if (entry)
		{
			m_pathToHandle.erase(entry->path);
		}
		m_resources.remove(handle);
	}

	/// @brief パスからハンドルを検索する
	/// @param path リソースパス
	/// @return ハンドル（見つからなければnullopt）
	[[nodiscard]] std::optional<ResourceHandle> findByPath(const std::string& path) const
	{
		const auto it = m_pathToHandle.find(path);
		if (it != m_pathToHandle.end() && m_resources.isValid(it->second))
		{
			return it->second;
		}
		return std::nullopt;
	}

	/// @brief 管理中のリソース数
	/// @return リソース数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_resources.size();
	}

	/// @brief 完了した非同期タスクをクリーンアップする
	void collectGarbage()
	{
		m_pendingFutures.erase(
			std::remove_if(m_pendingFutures.begin(), m_pendingFutures.end(),
				[](const std::future<void>& f) {
					return f.wait_for(std::chrono::seconds(0))
						== std::future_status::ready;
				}),
			m_pendingFutures.end());
	}

private:
	/// @brief リソースエントリ
	struct ResourceEntry
	{
		T resource{};               ///< リソース本体
		ResourceState state{ResourceState::Unloaded}; ///< 状態
		std::string path;           ///< リソースパス
	};

	HandleMap<ResourceTag, ResourceEntry> m_resources;           ///< リソースストレージ
	std::unordered_map<std::string, ResourceHandle> m_pathToHandle; ///< パス→ハンドル逆引き
	ThreadPool* m_pool{nullptr};                                    ///< スレッドプール
	std::vector<std::future<void>> m_pendingFutures;               ///< 保留中の非同期タスク
	mutable std::mutex m_mutex;                                    ///< 非同期アクセス用ミューテックス
};

} // namespace sgc
