#pragma once

/// @file View.hpp
/// @brief ECSクエリビュー — 再利用可能なコンポーネントクエリ
///
/// ストレージポインタをキャッシュし、繰り返しのクエリを高速化する。
/// World::view<Components...>() で生成する。
/// Exclude<Types...> で除外フィルタを指定できる。
///
/// @note シングルスレッド前提。マルチスレッドでの使用は未サポート。
///
/// @code
/// auto view = world.view<Position, Velocity>();
/// view.each([](Position& pos, Velocity& vel) {
///     pos.x += vel.dx;
///     pos.y += vel.dy;
/// });
///
/// // 除外フィルタ付き
/// auto view2 = world.viewExclude<Position, Velocity>(sgc::ecs::Exclude<Dead>{});
/// view2.each([](Position& pos, Velocity& vel) {
///     pos.x += vel.dx;
/// });
/// @endcode

#include <concepts>
#include <cstddef>
#include <tuple>
#include <vector>

#include "sgc/ecs/ComponentStorage.hpp"
#include "sgc/ecs/Entity.hpp"

namespace sgc::ecs
{

// 前方宣言
template <std::movable T>
class TypedStorage;

/// @brief 除外コンポーネントマーカー
/// @tparam Ts 除外するコンポーネント型
template <typename... Ts>
struct Exclude {};

/// @brief 再利用可能なECSクエリビュー
///
/// ストレージポインタをキャッシュして毎フレームのハッシュマップ検索を排除する。
/// 最小ストレージからイテレートし、残りのコンポーネントの存在をチェックする。
///
/// @tparam Components クエリ対象のコンポーネント型（ムーブ可能であること）
template <typename... Components>
	requires (std::movable<Components> && ...)
class View
{
public:
	/// @brief ビューを構築する（World::view()から呼ばれる）
	/// @param storages 各コンポーネントのTypedStorageポインタ
	/// @param generations 世代番号配列への参照
	explicit View(std::tuple<TypedStorage<Components>*...> storages,
		const std::vector<Generation>& generations)
		: m_storages(storages)
		, m_generations(generations)
	{
	}

	/// @brief 全てのコンポーネントを持つエンティティに対して関数を実行する
	/// @tparam Func コールバック型 (Components&...)
	/// @param func コールバック関数
	template <typename Func>
	void each(Func&& func)
	{
		if (!isValid()) return;

		// 最初のコンポーネントのストレージをイテレート基準にする
		auto* firstStorage = std::get<0>(m_storages);
		const auto& entities = firstStorage->storage().entities();

		for (std::size_t i = 0; i < entities.size(); ++i)
		{
			const EntityId id = entities[i];
			if (id >= m_generations.size()) continue;

			// 残りのコンポーネントが全て存在するか確認
			if (!hasAllComponents<1>(id)) continue;

			// コールバック実行
			func(*std::get<TypedStorage<Components>*>(m_storages)->storage().get(id)...);
		}
	}

	/// @brief 全てのコンポーネントを持つエンティティに対してEntity付きで関数を実行する
	/// @tparam Func コールバック型 (Entity, Components&...)
	/// @param func コールバック関数
	///
	/// @code
	/// auto view = world.view<Position, Velocity>();
	/// view.eachEntity([&](sgc::ecs::Entity entity, Position& pos, Velocity& vel) {
	///     if (pos.x < 0) world.destroyEntity(entity);
	/// });
	/// @endcode
	template <typename Func>
	void eachEntity(Func&& func)
	{
		if (!isValid()) return;

		auto* firstStorage = std::get<0>(m_storages);
		const auto& entities = firstStorage->storage().entities();

		for (std::size_t i = 0; i < entities.size(); ++i)
		{
			const EntityId id = entities[i];
			if (id >= m_generations.size()) continue;

			if (!hasAllComponents<1>(id)) continue;

			func(
				Entity{id, m_generations[id]},
				*std::get<TypedStorage<Components>*>(m_storages)->storage().get(id)...
			);
		}
	}

	/// @brief ビューが有効か（全ストレージが存在するか）
	/// @return 全ストレージが非nullならtrue
	[[nodiscard]] bool isValid() const noexcept
	{
		return allStoragesValid();
	}

private:
	std::tuple<TypedStorage<Components>*...> m_storages;
	const std::vector<Generation>& m_generations;

	/// @brief 全ストレージポインタが有効か確認する
	[[nodiscard]] bool allStoragesValid() const noexcept
	{
		return (std::get<TypedStorage<Components>*>(m_storages) && ...);
	}

	/// @brief インデックスI以降のコンポーネントを全て持つか確認する
	template <std::size_t I>
	[[nodiscard]] bool hasAllComponents(EntityId id) const
	{
		if constexpr (I >= sizeof...(Components))
		{
			return true;
		}
		else
		{
			auto* storage = std::get<I>(m_storages);
			if (!storage->storage().has(id)) return false;
			return hasAllComponents<I + 1>(id);
		}
	}
};

/// @brief 除外フィルタ付きECSクエリビュー
///
/// View と同様に動作するが、除外コンポーネントを持つエンティティをスキップする。
///
/// @tparam IncludeComponents 必須コンポーネント型
/// @tparam ExcludeComponents 除外コンポーネント型
template <typename IncludeList, typename ExcludeList>
class ExcludeView;

template <typename... Includes, typename... Excludes>
	requires ((std::movable<Includes> && ...) && (std::movable<Excludes> && ...))
class ExcludeView<std::tuple<Includes...>, std::tuple<Excludes...>>
{
public:
	/// @brief ビューを構築する
	explicit ExcludeView(
		std::tuple<TypedStorage<Includes>*...> includeStorages,
		std::tuple<TypedStorage<Excludes>*...> excludeStorages,
		const std::vector<Generation>& generations)
		: m_includeStorages(includeStorages)
		, m_excludeStorages(excludeStorages)
		, m_generations(generations)
	{
	}

	/// @brief 条件に合うエンティティに対して関数を実行する
	/// @tparam Func コールバック型 (Includes&...)
	/// @param func コールバック関数
	template <typename Func>
	void each(Func&& func)
	{
		if (!isValid()) return;

		auto* firstStorage = std::get<0>(m_includeStorages);
		const auto& entities = firstStorage->storage().entities();

		for (std::size_t i = 0; i < entities.size(); ++i)
		{
			const EntityId id = entities[i];
			if (id >= m_generations.size()) continue;

			// 必須コンポーネントの存在確認
			if (!hasAllIncludes<1>(id)) continue;

			// 除外コンポーネントの非存在確認
			if (hasAnyExclude(id)) continue;

			func(*std::get<TypedStorage<Includes>*>(m_includeStorages)->storage().get(id)...);
		}
	}

	/// @brief 条件に合うエンティティに対してEntity付きで関数を実行する
	/// @tparam Func コールバック型 (Entity, Includes&...)
	/// @param func コールバック関数
	template <typename Func>
	void eachEntity(Func&& func)
	{
		if (!isValid()) return;

		auto* firstStorage = std::get<0>(m_includeStorages);
		const auto& entities = firstStorage->storage().entities();

		for (std::size_t i = 0; i < entities.size(); ++i)
		{
			const EntityId id = entities[i];
			if (id >= m_generations.size()) continue;

			if (!hasAllIncludes<1>(id)) continue;
			if (hasAnyExclude(id)) continue;

			func(
				Entity{id, m_generations[id]},
				*std::get<TypedStorage<Includes>*>(m_includeStorages)->storage().get(id)...
			);
		}
	}

	/// @brief ビューが有効か
	[[nodiscard]] bool isValid() const noexcept
	{
		return (std::get<TypedStorage<Includes>*>(m_includeStorages) && ...);
	}

private:
	std::tuple<TypedStorage<Includes>*...> m_includeStorages;
	std::tuple<TypedStorage<Excludes>*...> m_excludeStorages;
	const std::vector<Generation>& m_generations;

	/// @brief 必須コンポーネントの存在確認
	template <std::size_t I>
	[[nodiscard]] bool hasAllIncludes(EntityId id) const
	{
		if constexpr (I >= sizeof...(Includes))
		{
			return true;
		}
		else
		{
			auto* storage = std::get<I>(m_includeStorages);
			if (!storage->storage().has(id)) return false;
			return hasAllIncludes<I + 1>(id);
		}
	}

	/// @brief 除外コンポーネントのいずれかを持つか確認
	[[nodiscard]] bool hasAnyExclude(EntityId id) const
	{
		return (hasExcludeComponent<Excludes>(id) || ...);
	}

	/// @brief 特定の除外コンポーネントを持つか
	template <typename E>
	[[nodiscard]] bool hasExcludeComponent(EntityId id) const
	{
		auto* storage = std::get<TypedStorage<E>*>(m_excludeStorages);
		return storage && storage->storage().has(id);
	}
};

} // namespace sgc::ecs
