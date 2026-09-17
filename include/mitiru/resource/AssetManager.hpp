#pragma once

/// @file AssetManager.hpp
/// @brief アセットのロード、キャッシュ、アンロードをまとめる AssetManager の宣言
/// @details 同期 load、AsyncAssetLoader、StreamingManager、HotReloadManager は、同じスロットと依存グラフを更新する。

#include <algorithm>
#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <mitiru/resource/AssetHandle.hpp>
#include <mitiru/resource/IAssetLoader.hpp>
#include <sgc/core/TypeId.hpp>

namespace mitiru::resource
{

class AssetManager
{
public:
	template <typename LoaderT>
		requires AssetLoader<LoaderT>
	void registerLoader(LoaderT loader)
	{
		using AssetType = typename LoaderT::AssetType;
		const auto typeId = sgc::typeId<AssetType>();
		const std::lock_guard lock(m_mutex);
		m_loaders[typeId] = std::make_unique<TypedAssetLoader<LoaderT>>(
			std::move(loader));
	}

	/// @details load<T>() の中で別のアセットをロードすると、外側の ID から内側の ID への依存を自動で記録する。
	template <typename T>
	[[nodiscard]] AssetHandle<T> load(const std::string& id, std::string_view path)
	{
		recordDependencyFromStack(id);

		auto existing = findSlot<T>(id);
		if (existing.has_value())
		{
			if (!*existing)
			{
				return AssetHandle<T>{id, std::shared_ptr<T>{}};
			}
			return AssetHandle<T>::fromSlot(id, *existing);
		}

		const auto typeId = sgc::typeId<T>();
		std::shared_ptr<detail::AssetSlot<T>> slot;
		bool shouldLoad = false;
		{
			const std::lock_guard lock(m_mutex);
			// findSlot() のロック解放後ここまでの間に別スレッドが同じ id で load() を
			// 先に通り抜けている可能性があるため、スロット作成前にもう一度 m_slots を見る。
			// 見ずに作ると両スレッドが別々の slot を作って m_slots を取り合い、片方の
			// AssetHandle が二度とマップへ反映されない孤立スロットを指すことになる。
			auto slotIt = m_slots.find(id);
			if (slotIt != m_slots.end())
			{
				try
				{
					slot = std::any_cast<std::shared_ptr<detail::AssetSlot<T>>>(slotIt->second);
				}
				catch (const std::bad_any_cast&)
				{
					return AssetHandle<T>{id, std::shared_ptr<T>{}};
				}
			}

			if (!slot)
			{
				if (m_loaders.find(typeId) == m_loaders.end())
				{
					return AssetHandle<T>{id, std::shared_ptr<T>{}};
				}
				slot = std::make_shared<detail::AssetSlot<T>>();
				slot->state = AssetState::Loading;
				m_slots[id] = slot;

				const std::string pathStr(path);
				m_reloaders[id] = [this, id, pathStr]() { performLoad<T>(id, pathStr); };
				shouldLoad = true;
			}
		}

		if (shouldLoad)
		{
			const std::string pathStr(path);
			LoadStackGuard guard(loadStack(), id);
			performLoad<T>(id, pathStr);
		}

		return AssetHandle<T>::fromSlot(id, slot);
	}

	/// @note 既存のハンドルとはスロットを共有せず、新しいアセットとして置き換える。
	template <typename T>
	AssetHandle<T> store(const std::string& id, std::shared_ptr<T> asset)
	{
		auto slot = std::make_shared<detail::AssetSlot<T>>();
		slot->asset = asset;
		slot->state = asset ? AssetState::Ready : AssetState::Unloaded;

		const std::lock_guard lock(m_mutex);
		m_slots[id] = slot;
		return AssetHandle<T>::fromSlot(id, slot);
	}

	/// @param asset 新しい中身。nullptr は失敗として扱う
	/// @details ワーカースレッドから呼べる。onChanged は pump() のキューに積み、この場では呼ばない。
	template <typename T>
	void setContent(const std::string& id, std::shared_ptr<T> asset)
	{
		std::shared_ptr<detail::AssetSlot<T>> slot;
		{
			const std::lock_guard lock(m_mutex);
			auto it = m_slots.find(id);
			if (it == m_slots.end())
			{
				slot = std::make_shared<detail::AssetSlot<T>>();
				m_slots[id] = slot;
			}
			else
			{
				try
				{
					slot = std::any_cast<std::shared_ptr<detail::AssetSlot<T>>>(it->second);
				}
				catch (const std::bad_any_cast&)
				{
					return;
				}
			}
		}

		{
			const std::lock_guard lock(slot->mutex);
			slot->asset = asset;
			slot->state = asset ? AssetState::Ready : AssetState::Failed;
		}

		enqueueNotification<T>(id, slot);
		if (asset)
		{
			propagateReloadToDependents(id);
		}
	}

	template <typename T>
	[[nodiscard]] AssetHandle<T> get(const std::string& id) const
	{
		auto slot = findSlot<T>(id);
		if (!slot.has_value() || !*slot)
		{
			return AssetHandle<T>{id, std::shared_ptr<T>{}};
		}
		return AssetHandle<T>::fromSlot(id, *slot);
	}

	/// @param id load<T>() で読み込み済みのアセット ID
	/// @details 依存元も連鎖して再ロードする。循環した時点で止める。
	bool reload(const std::string& id)
	{
		std::function<void()> reloader;
		{
			const std::lock_guard lock(m_mutex);
			auto it = m_reloaders.find(id);
			if (it == m_reloaders.end())
			{
				return false;
			}
			reloader = it->second;
		}

		auto& visited = reloadVisited();
		visited.clear();
		visited.insert(id);
		// reloader() が例外を投げても visited を必ず空にする。ここで clear() し損なうと、
		// この後 (reload() を経由しない) load()/setContent() が propagateReloadToDependents()
		// を呼んだ際に、visited がこの id を含んだままなので該当 dependent が誤って skip される。
		struct VisitedClearGuard
		{
			std::unordered_set<std::string>& v;
			~VisitedClearGuard() { v.clear(); }
		} clearOnExit{visited};
		reloader();
		return true;
	}

	/// @brief id から dependsOnId への依存を記録する
	/// @details 循環が生じる依存は記録しない。
	void declareDependency(const std::string& id, const std::string& dependsOnId)
	{
		if (id.empty() || dependsOnId.empty() || id == dependsOnId)
		{
			return;
		}

		const std::lock_guard lock(m_mutex);
		if (dependsOnTransitively(dependsOnId, id))
		{
			return;
		}

		auto& deps = m_dependencies[id];
		if (std::find(deps.begin(), deps.end(), dependsOnId) == deps.end())
		{
			deps.push_back(dependsOnId);
			m_dependents[dependsOnId].push_back(id);
		}
	}

	/// @brief 指定した ID の直接の依存先を返す
	[[nodiscard]] std::vector<std::string> dependenciesOf(const std::string& id) const
	{
		const std::lock_guard lock(m_mutex);
		auto it = m_dependencies.find(id);
		return (it != m_dependencies.end()) ? it->second : std::vector<std::string>{};
	}

	/// @brief 指定した ID に直接依存する ID を返す
	[[nodiscard]] std::vector<std::string> dependentsOf(const std::string& id) const
	{
		const std::lock_guard lock(m_mutex);
		auto it = m_dependents.find(id);
		return (it != m_dependents.end()) ? it->second : std::vector<std::string>{};
	}

	/// @brief 保留中の onChanged を呼び出し元のスレッドで実行する
	/// @details load、reload、setContent は通知をキューに積み、コールバックは呼ばない。
	void pump()
	{
		std::vector<std::function<void()>> jobs;
		{
			const std::lock_guard lock(m_pendingMutex);
			jobs.swap(m_pendingNotifications);
		}
		for (auto& job : jobs)
		{
			job();
		}
	}

	void unload(const std::string& id)
	{
		const std::lock_guard lock(m_mutex);
		m_slots.erase(id);
		m_reloaders.erase(id);
		m_dependencies.erase(id);
		m_dependents.erase(id);
		for (auto& [otherId, deps] : m_dependencies)
		{
			deps.erase(std::remove(deps.begin(), deps.end(), id), deps.end());
		}
		for (auto& [otherId, deps] : m_dependents)
		{
			deps.erase(std::remove(deps.begin(), deps.end(), id), deps.end());
		}
	}

	[[nodiscard]] bool isLoaded(const std::string& id) const
	{
		const std::lock_guard lock(m_mutex);
		return m_slots.find(id) != m_slots.end();
	}

	[[nodiscard]] std::vector<std::string> loadedIds() const
	{
		const std::lock_guard lock(m_mutex);
		std::vector<std::string> ids;
		ids.reserve(m_slots.size());
		for (const auto& [id, slot] : m_slots)
		{
			ids.push_back(id);
		}
		return ids;
	}

	void unloadAll()
	{
		const std::lock_guard lock(m_mutex);
		m_slots.clear();
		m_reloaders.clear();
		m_dependencies.clear();
		m_dependents.clear();
	}

	[[nodiscard]] std::size_t cacheSize() const
	{
		const std::lock_guard lock(m_mutex);
		return m_slots.size();
	}

private:
	struct LoadStackGuard
	{
		std::vector<std::string>& stack;
		LoadStackGuard(std::vector<std::string>& s, const std::string& id)
			: stack(s)
		{
			stack.push_back(id);
		}
		~LoadStackGuard()
		{
			stack.pop_back();
		}
	};

	/// @brief スレッドごとに現在ロード中の ID を保持する
	[[nodiscard]] static std::vector<std::string>& loadStack()
	{
		static thread_local std::vector<std::string> stack;
		return stack;
	}

	/// @brief 再ロードの連鎖ごとに訪問済みの ID を保持する
	[[nodiscard]] static std::unordered_set<std::string>& reloadVisited()
	{
		static thread_local std::unordered_set<std::string> visited;
		return visited;
	}

	/// @brief 呼び出し元の ID から今回の ID への依存を記録する
	void recordDependencyFromStack(const std::string& id)
	{
		auto& stack = loadStack();
		if (!stack.empty())
		{
			declareDependency(stack.back(), id);
		}
	}

	/// @note m_mutex を保持した状態で呼ぶ。
	[[nodiscard]] bool dependsOnTransitively(const std::string& from, const std::string& to) const
	{
		std::vector<std::string> pending{from};
		std::unordered_set<std::string> visited;

		while (!pending.empty())
		{
			const auto current = pending.back();
			pending.pop_back();
			if (current == to)
			{
				return true;
			}
			if (!visited.insert(current).second)
			{
				continue;
			}
			auto it = m_dependencies.find(current);
			if (it == m_dependencies.end())
			{
				continue;
			}
			for (const auto& next : it->second)
			{
				pending.push_back(next);
			}
		}
		return false;
	}

	/// @return 未登録なら nullopt、型が違う場合は nullptr を包んだ optional
	template <typename T>
	[[nodiscard]] std::optional<std::shared_ptr<detail::AssetSlot<T>>> findSlot(const std::string& id) const
	{
		const std::lock_guard lock(m_mutex);
		auto it = m_slots.find(id);
		if (it == m_slots.end())
		{
			return std::nullopt;
		}
		try
		{
			return std::any_cast<std::shared_ptr<detail::AssetSlot<T>>>(it->second);
		}
		catch (const std::bad_any_cast&)
		{
			return std::shared_ptr<detail::AssetSlot<T>>{nullptr};
		}
	}

	/// @details 呼び出す前に m_slots へスロットを登録する。
	template <typename T>
	void performLoad(const std::string& id, const std::string& path)
	{
		std::shared_ptr<detail::AssetSlot<T>> slot;
		IAssetLoaderBase* loader = nullptr;
		{
			const std::lock_guard lock(m_mutex);
			auto slotIt = m_slots.find(id);
			if (slotIt == m_slots.end())
			{
				return;
			}
			try
			{
				slot = std::any_cast<std::shared_ptr<detail::AssetSlot<T>>>(slotIt->second);
			}
			catch (const std::bad_any_cast&)
			{
				return;
			}

			auto loaderIt = m_loaders.find(sgc::typeId<T>());
			if (loaderIt == m_loaders.end())
			{
				return;
			}
			loader = loaderIt->second.get();
		}

		{
			const std::lock_guard lock(slot->mutex);
			const bool wasReady = (slot->state == AssetState::Ready || slot->state == AssetState::Reloading);
			slot->state = wasReady ? AssetState::Reloading : AssetState::Loading;
		}

		bool ok = false;
		try
		{
			auto result = loader->loadAny(path);
			auto asset = std::any_cast<std::shared_ptr<T>>(result);
			ok = (asset != nullptr);
			const std::lock_guard lock(slot->mutex);
			slot->asset = std::move(asset);
			slot->state = ok ? AssetState::Ready : AssetState::Failed;
		}
		catch (const std::bad_any_cast&)
		{
			const std::lock_guard lock(slot->mutex);
			slot->state = AssetState::Failed;
		}

		enqueueNotification<T>(id, slot);
		if (ok)
		{
			propagateReloadToDependents(id);
		}
	}

	/// @details ワーカースレッドから呼べる。コールバックは pump() で実行する。
	template <typename T>
	void enqueueNotification(const std::string& id, std::shared_ptr<detail::AssetSlot<T>> slot)
	{
		const std::lock_guard lock(m_pendingMutex);
		m_pendingNotifications.push_back([id, slot]() {
			std::vector<std::pair<std::size_t, typename AssetHandle<T>::ChangeCallback>> callbacks;
			{
				const std::lock_guard slotLock(slot->mutex);
				callbacks = slot->callbacks;
			}
			const auto handle = AssetHandle<T>::fromSlot(id, slot);
			for (auto& [token, callback] : callbacks)
			{
				(void)token;
				if (callback)
				{
					callback(handle);
				}
			}
		});
	}

	void propagateReloadToDependents(const std::string& id)
	{
		std::vector<std::string> dependents;
		{
			const std::lock_guard lock(m_mutex);
			auto it = m_dependents.find(id);
			if (it == m_dependents.end())
			{
				return;
			}
			dependents = it->second;
		}

		auto& visited = reloadVisited();
		for (const auto& dependentId : dependents)
		{
			if (visited.contains(dependentId))
			{
				continue;
			}
			visited.insert(dependentId);

			std::function<void()> reloader;
			{
				const std::lock_guard lock(m_mutex);
				auto it = m_reloaders.find(dependentId);
				if (it == m_reloaders.end())
				{
					continue;
				}
				reloader = it->second;
			}
			if (reloader)
			{
				reloader();
			}
		}
	}

	std::unordered_map<sgc::TypeIdValue, std::unique_ptr<IAssetLoaderBase>> m_loaders;

	/// @brief アセット ID から型消去したスロットへの対応
	/// @details shared_ptr<detail::AssetSlot<T>> を格納する。
	std::unordered_map<std::string, std::any> m_slots;

	std::unordered_map<std::string, std::function<void()>> m_reloaders;

	/// @brief アセット ID から依存先 ID への対応
	std::unordered_map<std::string, std::vector<std::string>> m_dependencies;

	/// @brief アセット ID から依存元 ID への対応
	std::unordered_map<std::string, std::vector<std::string>> m_dependents;

	mutable std::mutex m_mutex;  ///< m_loaders / m_slots / m_reloaders / 依存グラフ用

	std::mutex m_pendingMutex;                              ///< 通知キュー用
	std::vector<std::function<void()>> m_pendingNotifications; ///< pump() が消費する通知
};

} // namespace mitiru::resource
