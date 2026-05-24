#pragma once

/// @file SystemTraits.hpp
/// @brief ECSシステムのコンポーネント依存宣言と検証
///
/// システムに `requires_components` 型エイリアスを定義することで、
/// 必要なコンポーネントの静的・動的検証が可能になる。
///
/// @code
/// struct MovementSystem {
///     using requires_components = sgc::TypeList<Position, Velocity>;
///
///     void update(sgc::ecs::World& world, float dt) {
///         world.forEach<Position, Velocity>([dt](Position& pos, Velocity& vel) {
///             pos.x += vel.dx * dt;
///         });
///     }
/// };
///
/// // 静的チェック
/// static_assert(sgc::ecs::HasRequiredComponents<MovementSystem>);
///
/// // 動的検証
/// bool ok = sgc::ecs::validateSystemRequirements<MovementSystem>(world);
/// @endcode

#include <type_traits>

#include "sgc/ecs/World.hpp"
#include "sgc/types/TypeList.hpp"

namespace sgc::ecs
{

// ── コンセプト ──────────────────────────────────────────

/// @brief requires_components 型エイリアスを持つかを判定するコンセプト
///
/// システムが必要とするコンポーネント型を TypeList で宣言しているかを検出する。
template <typename S>
concept HasRequiredComponents = requires
{
	typename S::requires_components;
};

// ── 型特性 ──────────────────────────────────────────────

namespace detail
{

/// @brief requires_components を持つ型からTypeListを取得する（実装用）
template <typename S, bool HasReq>
struct RequiredComponentsHelper
{
	using Type = TypeList<>;
};

template <typename S>
struct RequiredComponentsHelper<S, true>
{
	using Type = typename S::requires_components;
};

/// @brief TypeListの各型に対してWorldのhasStorageを呼ぶ（実装用）
template <typename... Ts>
struct ValidateStorages;

template <>
struct ValidateStorages<>
{
	[[nodiscard]] static bool check(const World& /*world*/) noexcept
	{
		return true;
	}
};

template <typename First, typename... Rest>
struct ValidateStorages<First, Rest...>
{
	/// @note World::hasStorage<T>()を使用するため、呼び出し側でWorld.hppのインクルードが必要
	[[nodiscard]] static bool check(const World& world)
	{
		if (!world.template hasStorage<First>()) return false;
		return ValidateStorages<Rest...>::check(world);
	}
};

/// @brief TypeListをアンパックしてValidateStoragesに委譲する（実装用）
template <typename TL>
struct ValidateFromTypeList;

template <typename... Ts>
struct ValidateFromTypeList<TypeList<Ts...>>
{
	[[nodiscard]] static bool check(const World& world)
	{
		return ValidateStorages<Ts...>::check(world);
	}
};

} // namespace detail

/// @brief システムの必須コンポーネント型リストを取得する
///
/// requires_components が定義されていれば、そのTypeListを返す。
/// 未定義の場合は空の TypeList<> を返す。
///
/// @tparam S システム型
template <typename S>
using RequiredComponentsOfT =
	typename detail::RequiredComponentsHelper<S, HasRequiredComponents<S>>::Type;

/// @brief requires_components が TypeList であることを検証するコンセプト
///
/// addSystem時のstatic_assertで使用する。
template <typename TL>
concept IsTypeList = requires
{
	[]<typename... Ts>(TypeList<Ts...>*) {}(static_cast<TL*>(nullptr));
};

/// @brief 実行時検証: Worldに必要なコンポーネントストレージが全て存在するか
///
/// requires_components が未定義のシステムに対しては常にtrueを返す。
///
/// @tparam S システム型
/// @param world 検証対象のECSワールド
/// @return 必要なストレージが全て存在すればtrue
template <typename S>
[[nodiscard]] bool validateSystemRequirements(const World& world)
{
	using Required = RequiredComponentsOfT<S>;
	return detail::ValidateFromTypeList<Required>::check(world);
}

} // namespace sgc::ecs
