#pragma once

/// @file Entity.hpp
/// @brief ECSエンティティ型定義
///
/// エンティティはID + 世代番号で一意に識別される。
/// 世代番号により、破棄されたエンティティIDの再利用を安全に検出できる。

#include <cstdint>
#include <functional>
#include <limits>

namespace sgc::ecs
{

/// @brief エンティティID型
using EntityId = std::uint32_t;

/// @brief 世代番号型
using Generation = std::uint32_t;

/// @brief 無効なエンティティID
constexpr EntityId INVALID_ENTITY_ID = std::numeric_limits<EntityId>::max();

/// @brief エンティティ
///
/// IDと世代番号のペアで構成される。
/// 世代番号が一致しないエンティティは無効とみなされる。
///
/// @code
/// sgc::ecs::Entity e;             // 無効なエンティティ
/// auto e2 = world.createEntity(); // 有効なエンティティ
/// @endcode
struct Entity
{
	EntityId id{INVALID_ENTITY_ID};   ///< エンティティID
	Generation generation{0};         ///< 世代番号

	/// @brief エンティティが有効かどうか
	/// @return 有効なIDであればtrue
	[[nodiscard]] constexpr bool isValid() const noexcept
	{
		return id != INVALID_ENTITY_ID;
	}

	/// @brief 等値比較
	[[nodiscard]] constexpr bool operator==(const Entity& rhs) const noexcept = default;
};

} // namespace sgc::ecs

/// @brief Entity用ハッシュ特殊化
template <>
struct std::hash<sgc::ecs::Entity>
{
	std::size_t operator()(const sgc::ecs::Entity& e) const noexcept
	{
		// IDと世代番号を組み合わせたハッシュ
		return std::hash<std::uint64_t>{}(
			(static_cast<std::uint64_t>(e.id) << 32) | e.generation);
	}
};
