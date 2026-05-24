#pragma once

/// @file World.hpp
/// @brief ECSワールド — エンティティとコンポーネントの管理
///
/// エンティティのライフサイクル管理とコンポーネントCRUDを提供する。
/// typeId<T>()による動的型識別でコンポーネントストレージを管理する。
///
/// @code
/// sgc::ecs::World world;
/// auto e = world.createEntity();
/// world.addComponent(e, Position{10.0f, 20.0f});
/// world.addComponent(e, Velocity{1.0f, 0.0f});
///
/// world.forEach<Position, Velocity>([](Position& pos, Velocity& vel) {
///     pos.x += vel.dx;
///     pos.y += vel.dy;
/// });
/// @endcode

#include <concepts>
#include <cstdint>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "sgc/core/TypeId.hpp"
#include "sgc/ecs/ComponentStorage.hpp"
#include "sgc/ecs/Entity.hpp"
#include "sgc/ecs/View.hpp"
#include "sgc/patterns/EventDispatcher.hpp"

namespace sgc::ecs
{

/// @brief コンポーネント追加イベント
/// @tparam T コンポーネント型（ムーブ可能であること）
template <std::movable T>
struct ComponentAdded
{
	Entity entity;  ///< 対象エンティティ
};

/// @brief コンポーネント削除イベント
/// @tparam T コンポーネント型（ムーブ可能であること）
template <std::movable T>
struct ComponentRemoved
{
	Entity entity;  ///< 対象エンティティ
};

/// @brief コンポーネントストレージの基底インターフェース（型消去用）
class IComponentStorage
{
public:
	virtual ~IComponentStorage() = default;

	/// @brief 指定エンティティのコンポーネントを削除する
	/// @param id エンティティID
	virtual void removeEntity(EntityId id) = 0;

	/// @brief 指定エンティティがコンポーネントを持つか確認する
	/// @param id エンティティID
	/// @return 持っていればtrue
	[[nodiscard]] virtual bool hasEntity(EntityId id) const = 0;
};

/// @brief 型付きコンポーネントストレージ（IComponentStorageの実装）
/// @tparam T コンポーネント型（ムーブ可能であること）
template <std::movable T>
class TypedStorage : public IComponentStorage
{
public:
	/// @brief 内部ストレージへの参照を返す
	[[nodiscard]] ComponentStorage<T>& storage() noexcept { return m_storage; }

	/// @brief 内部ストレージへのconst参照を返す
	[[nodiscard]] const ComponentStorage<T>& storage() const noexcept { return m_storage; }

	/// @brief 指定エンティティのコンポーネントを削除する
	void removeEntity(EntityId id) override { m_storage.remove(id); }

	/// @brief 指定エンティティがコンポーネントを持つか確認する
	[[nodiscard]] bool hasEntity(EntityId id) const override { return m_storage.has(id); }

private:
	ComponentStorage<T> m_storage;
};

/// @brief ECSワールド
///
/// エンティティの作成・破棄とコンポーネントの追加・取得・削除を管理する。
/// forEach()で型安全なクエリを提供する。
///
/// @note コピー・ムーブ不可。View が内部メンバへの参照を保持するため、
///       ムーブされるとダングリング参照が発生する。
class World
{
public:
	World() = default;
	World(const World&) = delete;
	World& operator=(const World&) = delete;
	World(World&&) = delete;
	World& operator=(World&&) = delete;

	/// @brief エンティティを作成する
	/// @return 新しいエンティティ
	[[nodiscard]] Entity createEntity()
	{
		EntityId id;
		if (!m_freeIds.empty())
		{
			id = m_freeIds.back();
			m_freeIds.pop_back();
		}
		else
		{
			id = m_nextId++;
			m_generations.push_back(0);
		}
		++m_entityCount;
		return Entity{id, m_generations[id]};
	}

	/// @brief エンティティを破棄する
	/// @param entity 破棄するエンティティ
	void destroyEntity(Entity entity)
	{
		if (!isAlive(entity)) return;

		// 全ストレージからコンポーネントを削除
		for (auto& [typeId, storage] : m_storages)
		{
			storage->removeEntity(entity.id);
		}

		// タグを削除
		m_tags.erase(entity.id);

		// 世代をインクリメントしてIDを再利用可能にする
		++m_generations[entity.id];
		m_freeIds.push_back(entity.id);
		--m_entityCount;
	}

	/// @brief エンティティが生存しているか確認する
	/// @param entity 確認するエンティティ
	/// @return 生存していればtrue
	[[nodiscard]] bool isAlive(Entity entity) const noexcept
	{
		return entity.id < m_generations.size()
			&& m_generations[entity.id] == entity.generation;
	}

	/// @brief エンティティにコンポーネントを追加する
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @param entity 対象エンティティ
	/// @param component コンポーネント
	template <std::movable T>
	void addComponent(Entity entity, T component)
	{
		if (!isAlive(entity)) return;
		getOrCreateStorage<T>().add(entity.id, std::move(component));
		if (m_eventDispatcher)
		{
			m_eventDispatcher->emit(ComponentAdded<T>{entity});
		}
	}

	/// @brief エンティティのコンポーネントを取得する
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @param entity 対象エンティティ
	/// @return コンポーネントへのポインタ（なければnullptr）
	template <std::movable T>
	[[nodiscard]] T* getComponent(Entity entity)
	{
		if (!isAlive(entity)) return nullptr;
		auto* typed = findStorage<T>();
		if (!typed) return nullptr;
		return typed->storage().get(entity.id);
	}

	/// @brief エンティティのコンポーネントを取得する（const版）
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @param entity 対象エンティティ
	/// @return コンポーネントへのconstポインタ（なければnullptr）
	template <std::movable T>
	[[nodiscard]] const T* getComponent(Entity entity) const
	{
		if (!isAlive(entity)) return nullptr;
		const auto* typed = findStorage<T>();
		if (!typed) return nullptr;
		return typed->storage().get(entity.id);
	}

	/// @brief エンティティからコンポーネントを削除する
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @param entity 対象エンティティ
	template <std::movable T>
	void removeComponent(Entity entity)
	{
		if (!isAlive(entity)) return;
		auto* typed = findStorage<T>();
		if (typed)
		{
			typed->storage().remove(entity.id);
			if (m_eventDispatcher)
			{
				m_eventDispatcher->emit(ComponentRemoved<T>{entity});
			}
		}
	}

	/// @brief エンティティがコンポーネントを持つか確認する
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @param entity 対象エンティティ
	/// @return 持っていればtrue
	template <std::movable T>
	[[nodiscard]] bool hasComponent(Entity entity) const
	{
		if (!isAlive(entity)) return false;
		const auto* typed = findStorage<T>();
		return typed && typed->storage().has(entity.id);
	}

	/// @brief 指定コンポーネントを全て持つエンティティに対して関数を実行する
	///
	/// ストレージポインタを事前キャッシュし、ループ中のハッシュマップ検索を排除する。
	///
	/// @tparam First 最初のコンポーネント型（ムーブ可能であること）
	/// @tparam Rest 残りのコンポーネント型（ムーブ可能であること）
	/// @tparam Func コールバック型
	/// @param func コールバック関数 (First&, Rest&...)
	template <typename First, typename... Rest, typename Func>
		requires (std::movable<First> && (std::movable<Rest> && ...))
	void forEach(Func&& func)
	{
		auto* firstTyped = findStorage<First>();
		if (!firstTyped) return;

		// ストレージポインタを事前キャッシュ（ループ内のfindStorage排除）
		if constexpr (sizeof...(Rest) > 0)
		{
			auto restStorages = std::make_tuple(findStorage<Rest>()...);
			// 全ストレージが存在するか確認
			const bool allFound = (std::get<TypedStorage<Rest>*>(restStorages) && ...);
			if (!allFound) return;

			const auto& entities = firstTyped->storage().entities();
			for (std::size_t i = 0; i < entities.size(); ++i)
			{
				const EntityId id = entities[i];
				if (id >= m_generations.size()) continue;

				// キャッシュされたポインタでhasチェック
				const bool hasAll = (std::get<TypedStorage<Rest>*>(restStorages)->storage().has(id) && ...);
				if (!hasAll) continue;

				func(
					*firstTyped->storage().get(id),
					*std::get<TypedStorage<Rest>*>(restStorages)->storage().get(id)...
				);
			}
		}
		else
		{
			const auto& entities = firstTyped->storage().entities();
			for (std::size_t i = 0; i < entities.size(); ++i)
			{
				const EntityId id = entities[i];
				if (id >= m_generations.size()) continue;

				func(*firstTyped->storage().get(id));
			}
		}
	}

	/// @brief 指定コンポーネントを全て持つエンティティに対してEntity付きで関数を実行する
	///
	/// forEach()と同様だが、コールバックの第1引数にEntityを渡す。
	/// ループ内でgetComponent()やdestroyEntity()を呼ぶ場合に使用する。
	///
	/// @tparam First 最初のコンポーネント型（ムーブ可能であること）
	/// @tparam Rest 残りのコンポーネント型（ムーブ可能であること）
	/// @tparam Func コールバック型
	/// @param func コールバック関数 (Entity, First&, Rest&...)
	///
	/// @code
	/// world.forEachEntity<Position, Velocity>([&](sgc::ecs::Entity entity, Position& pos, Velocity& vel) {
	///     if (pos.x < 0) world.destroyEntity(entity);
	/// });
	/// @endcode
	template <typename First, typename... Rest, typename Func>
		requires (std::movable<First> && (std::movable<Rest> && ...))
	void forEachEntity(Func&& func)
	{
		auto* firstTyped = findStorage<First>();
		if (!firstTyped) return;

		if constexpr (sizeof...(Rest) > 0)
		{
			auto restStorages = std::make_tuple(findStorage<Rest>()...);
			const bool allFound = (std::get<TypedStorage<Rest>*>(restStorages) && ...);
			if (!allFound) return;

			const auto& entities = firstTyped->storage().entities();
			for (std::size_t i = 0; i < entities.size(); ++i)
			{
				const EntityId id = entities[i];
				if (id >= m_generations.size()) continue;

				const bool hasAll = (std::get<TypedStorage<Rest>*>(restStorages)->storage().has(id) && ...);
				if (!hasAll) continue;

				func(
					Entity{id, m_generations[id]},
					*firstTyped->storage().get(id),
					*std::get<TypedStorage<Rest>*>(restStorages)->storage().get(id)...
				);
			}
		}
		else
		{
			const auto& entities = firstTyped->storage().entities();
			for (std::size_t i = 0; i < entities.size(); ++i)
			{
				const EntityId id = entities[i];
				if (id >= m_generations.size()) continue;

				func(Entity{id, m_generations[id]}, *firstTyped->storage().get(id));
			}
		}
	}

	/// @brief 再利用可能なクエリビューを作成する
	///
	/// ストレージポインタをキャッシュし、毎フレームのforEachより効率的なイテレーションを提供する。
	///
	/// @tparam Components クエリ対象のコンポーネント型（ムーブ可能であること）
	/// @return View オブジェクト
	///
	/// @code
	/// auto view = world.view<Position, Velocity>();
	/// view.each([](Position& pos, Velocity& vel) {
	///     pos.x += vel.dx;
	/// });
	/// @endcode
	template <typename... Components>
		requires (std::movable<Components> && ...)
	[[nodiscard]] View<Components...> view()
	{
		auto storages = std::make_tuple(findStorage<Components>()...);
		return View<Components...>(storages, m_generations);
	}

	/// @brief 除外フィルタ付きクエリビューを作成する
	///
	/// 指定した除外コンポーネントを持つエンティティをスキップする。
	///
	/// @tparam Components 必須コンポーネント型（ムーブ可能であること）
	/// @tparam Excludes 除外コンポーネント型（ムーブ可能であること）
	/// @param 除外マーカー（型推論用）
	/// @return ExcludeView オブジェクト
	///
	/// @code
	/// auto view = world.viewExclude<Position, Velocity>(Exclude<Dead>{});
	/// view.each([](Position& pos, Velocity& vel) {
	///     pos.x += vel.dx;
	/// });
	/// @endcode
	template <typename... Components, typename... Excludes>
		requires ((std::movable<Components> && ...) && (std::movable<Excludes> && ...))
	[[nodiscard]] auto viewExclude(Exclude<Excludes...>)
	{
		auto includeStorages = std::make_tuple(findStorage<Components>()...);
		auto excludeStorages = std::make_tuple(findStorage<Excludes>()...);
		return ExcludeView<std::tuple<Components...>, std::tuple<Excludes...>>(
			includeStorages, excludeStorages, m_generations);
	}

	// ── タグ ────────────────────────────────────────────────

	/// @brief エンティティにタグを設定する
	///
	/// Hash.hppの `"player"_hash` と組み合わせて使用することを推奨。
	///
	/// @param entity 対象エンティティ
	/// @param tag タグ値（0はタグなしを意味する）
	void setTag(Entity entity, std::uint64_t tag)
	{
		if (!isAlive(entity)) return;
		m_tags[entity.id] = tag;
	}

	/// @brief エンティティのタグを取得する
	/// @param entity 対象エンティティ
	/// @return タグ値（未設定の場合は0）
	[[nodiscard]] std::uint64_t getTag(Entity entity) const
	{
		if (!isAlive(entity)) return 0;
		const auto it = m_tags.find(entity.id);
		return (it != m_tags.end()) ? it->second : 0;
	}

	/// @brief 指定タグを持つエンティティを検索する
	/// @param tag 検索するタグ値
	/// @return 一致するエンティティのリスト
	[[nodiscard]] std::vector<Entity> findByTag(std::uint64_t tag) const
	{
		std::vector<Entity> result;
		for (const auto& [id, t] : m_tags)
		{
			if (t == tag && id < m_generations.size())
			{
				result.push_back(Entity{id, m_generations[id]});
			}
		}
		return result;
	}

	// ── EventDispatcher連携 ─────────────────────────────────

	/// @brief EventDispatcherを設定する
	///
	/// 設定すると、addComponent/removeComponent時に
	/// ComponentAdded<T>/ComponentRemoved<T>イベントが発火される。
	///
	/// @param dispatcher EventDispatcherへのポインタ（nullptrで無効化）
	void setEventDispatcher(sgc::EventDispatcher* dispatcher) noexcept
	{
		m_eventDispatcher = dispatcher;
	}

	/// @brief 生存エンティティ数
	/// @return エンティティ数
	[[nodiscard]] std::size_t entityCount() const noexcept
	{
		return m_entityCount;
	}

	/// @brief 指定型のコンポーネントストレージが存在するか確認する
	///
	/// SystemTraitsの実行時検証（validateSystemRequirements）で使用する。
	///
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @return ストレージが存在すればtrue
	template <std::movable T>
	[[nodiscard]] bool hasStorage() const
	{
		return m_storages.find(typeId<T>()) != m_storages.end();
	}

private:
	std::vector<Generation> m_generations;     ///< EntityId → 現在の世代
	std::vector<EntityId> m_freeIds;           ///< 再利用可能なID
	EntityId m_nextId{0};                      ///< 次に割り当てるID
	std::size_t m_entityCount{0};              ///< 生存エンティティ数

	/// @brief 型ID → コンポーネントストレージ
	std::unordered_map<TypeIdValue, std::unique_ptr<IComponentStorage>> m_storages;

	std::unordered_map<EntityId, std::uint64_t> m_tags;  ///< エンティティタグ
	sgc::EventDispatcher* m_eventDispatcher{nullptr};    ///< イベントディスパッチャー（オプション）

	/// @brief 型Tのストレージを取得または作成する
	template <std::movable T>
	ComponentStorage<T>& getOrCreateStorage()
	{
		const auto tid = typeId<T>();
		auto it = m_storages.find(tid);
		if (it == m_storages.end())
		{
			auto storage = std::make_unique<TypedStorage<T>>();
			auto* ptr = storage.get();
			m_storages.emplace(tid, std::move(storage));
			return ptr->storage();
		}
		return static_cast<TypedStorage<T>*>(it->second.get())->storage();
	}

	/// @brief 型Tのストレージを検索する
	template <std::movable T>
	[[nodiscard]] TypedStorage<T>* findStorage()
	{
		const auto it = m_storages.find(typeId<T>());
		if (it == m_storages.end()) return nullptr;
		return static_cast<TypedStorage<T>*>(it->second.get());
	}

	/// @brief 型Tのストレージを検索する（const版）
	template <std::movable T>
	[[nodiscard]] const TypedStorage<T>* findStorage() const
	{
		const auto it = m_storages.find(typeId<T>());
		if (it == m_storages.end()) return nullptr;
		return static_cast<const TypedStorage<T>*>(it->second.get());
	}

	/// @brief EntityIdでコンポーネントの有無を確認する（内部用）
	template <std::movable T>
	[[nodiscard]] bool hasComponentById(EntityId id) const
	{
		const auto* typed = findStorage<T>();
		return typed && typed->storage().has(id);
	}
};

} // namespace sgc::ecs
