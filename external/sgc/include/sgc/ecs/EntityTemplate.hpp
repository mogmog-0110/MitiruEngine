#pragma once

/// @file EntityTemplate.hpp
/// @brief エンティティテンプレート（プリファブ）
///
/// コンポーネントの組み合わせをテンプレートとして保存し、
/// 同じ構成のエンティティを繰り返し生成できる。
///
/// @code
/// sgc::ecs::EntityTemplate tmpl;
/// tmpl.add(Position{10, 20})
///     .add(Velocity{1, 0})
///     .add<Health>();
///
/// auto entity = tmpl.instantiate(world);
/// @endcode

#include <concepts>
#include <cstddef>
#include <functional>
#include <typeindex>
#include <vector>

#include "sgc/ecs/Entity.hpp"
#include "sgc/ecs/World.hpp"

namespace sgc::ecs
{

/// @brief エンティティテンプレート（プリファブ）
///
/// コンポーネントの型と初期値を記録し、instantiate()で
/// 同じ構成のエンティティを何度でも生成できる。
class EntityTemplate
{
public:
	/// @brief コンポーネントを値付きで追加する
	/// @tparam T コンポーネント型（コピー可能であること。ラムダキャプチャで保持するため）
	/// @param component 初期値
	/// @return テンプレート自身への参照（メソッドチェーン用）
	template <std::copyable T>
	EntityTemplate& add(const T& component)
	{
		m_components.push_back({
			std::type_index(typeid(T)),
			[component](World& world, Entity entity)
			{
				world.addComponent(entity, component);
			}
		});
		return *this;
	}

	/// @brief デフォルト構築のコンポーネントを追加する
	/// @tparam T コンポーネント型（コピー可能かつデフォルト構築可能であること）
	/// @return テンプレート自身への参照（メソッドチェーン用）
	template <typename T>
		requires std::copyable<T> && std::default_initializable<T>
	EntityTemplate& add()
	{
		return add(T{});
	}

	/// @brief テンプレートからエンティティを生成する
	/// @param world 生成先のワールド
	/// @return 生成されたエンティティ
	Entity instantiate(World& world) const;

	/// @brief 登録されているコンポーネント数を返す
	/// @return コンポーネント数
	[[nodiscard]] std::size_t componentCount() const noexcept
	{
		return m_components.size();
	}

private:
	/// @brief コンポーネント適用エントリ
	struct ComponentEntry
	{
		std::type_index type;                            ///< コンポーネント型情報
		std::function<void(World&, Entity)> applier;     ///< 適用関数
	};

	std::vector<ComponentEntry> m_components;  ///< 登録済みコンポーネント
};

} // namespace sgc::ecs

// ── インライン実装（World.hppとの循環依存回避） ──

#include "sgc/ecs/World.hpp"

namespace sgc::ecs
{

inline Entity EntityTemplate::instantiate(World& world) const
{
	Entity entity = world.createEntity();
	for (const auto& entry : m_components)
	{
		entry.applier(world, entity);
	}
	return entity;
}

} // namespace sgc::ecs
