#pragma once

/// @file CommandBuffer.hpp
/// @brief ECSコマンドバッファ — 遅延コマンド実行
///
/// イテレーション中にWorld を直接変更するとイテレータが無効化される問題を回避するため、
/// コマンドをバッファリングして一括適用する。
///
/// @code
/// sgc::ecs::CommandBuffer cmd;
/// cmd.createEntity([](sgc::ecs::World& w, sgc::ecs::Entity e) {
///     w.addComponent(e, Position{0, 0});
/// });
/// cmd.destroyEntity(someEntity);
/// cmd.execute(world); // 一括適用
/// @endcode

#include <concepts>
#include <functional>
#include <vector>

#include "sgc/ecs/Entity.hpp"

namespace sgc::ecs
{

// 前方宣言
class World;

/// @brief 遅延コマンドバッファ
///
/// エンティティの作成・破棄やコンポーネントの追加・削除を
/// バッファリングし、execute()で一括適用する。
class CommandBuffer
{
public:
	/// @brief エンティティ作成コマンドを追加する
	/// @param setup 作成後のセットアップ関数 (World&, Entity)
	void createEntity(std::function<void(World&, Entity)> setup)
	{
		m_createCommands.push_back(std::move(setup));
	}

	/// @brief エンティティ破棄コマンドを追加する
	/// @param entity 破棄するエンティティ
	void destroyEntity(Entity entity)
	{
		m_destroyCommands.push_back(entity);
	}

	/// @brief コンポーネント追加コマンドを追加する
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @param entity 対象エンティティ
	/// @param component コンポーネント
	template <std::movable T>
	void addComponent(Entity entity, T component);

	/// @brief コンポーネント削除コマンドを追加する
	/// @tparam T コンポーネント型（ムーブ可能であること）
	/// @param entity 対象エンティティ
	template <std::movable T>
	void removeComponent(Entity entity);

	/// @brief バッファされたコマンドを一括適用する
	/// @param world 適用先のWorld
	void execute(World& world);

	/// @brief バッファされたコマンド数
	/// @return コマンド数
	[[nodiscard]] std::size_t commandCount() const noexcept
	{
		return m_createCommands.size() + m_destroyCommands.size()
			+ m_genericCommands.size();
	}

	/// @brief バッファをクリアする
	void clear()
	{
		m_createCommands.clear();
		m_destroyCommands.clear();
		m_genericCommands.clear();
	}

private:
	std::vector<std::function<void(World&, Entity)>> m_createCommands;  ///< エンティティ作成
	std::vector<Entity> m_destroyCommands;                              ///< エンティティ破棄
	std::vector<std::function<void(World&)>> m_genericCommands;         ///< 汎用コマンド
};

} // namespace sgc::ecs

// World.hpp のインクルード後にテンプレートメンバと execute を定義
#include "sgc/ecs/World.hpp"

namespace sgc::ecs
{

template <std::movable T>
void CommandBuffer::addComponent(Entity entity, T component)
{
	m_genericCommands.push_back(
		[entity, comp = std::move(component)](World& w) mutable {
			w.addComponent(entity, std::move(comp));
		}
	);
}

template <std::movable T>
void CommandBuffer::removeComponent(Entity entity)
{
	m_genericCommands.push_back(
		[entity](World& w) {
			w.template removeComponent<T>(entity);
		}
	);
}

inline void CommandBuffer::execute(World& world)
{
	// 作成コマンドを先に実行
	for (auto& setup : m_createCommands)
	{
		auto entity = world.createEntity();
		if (setup) setup(world, entity);
	}

	// 汎用コマンド（コンポーネント追加/削除）
	for (auto& cmd : m_genericCommands)
	{
		if (cmd) cmd(world);
	}

	// 破棄コマンドを最後に実行
	for (const auto& entity : m_destroyCommands)
	{
		world.destroyEntity(entity);
	}

	clear();
}

} // namespace sgc::ecs
