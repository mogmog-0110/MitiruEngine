#pragma once

/// @file EntityInspector.hpp
/// @brief ECSエンティティインスペクター
///
/// ECSワールドの統計情報を収集し、デバッグ表示に利用する。
/// テンプレートでワールド型に非依存な設計。
///
/// @code
/// // WorldTypeはentityCount(), isAlive(id)等のAPIを持つ型
/// auto stats = sgc::debug::collectWorldStats(world);
/// // stats.entityCount, stats.aliveEntityCount 等を参照
/// @endcode

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sgc::debug
{

/// @brief ECSエンティティの情報
struct EntityInfo
{
	uint32_t id;                               ///< エンティティID
	size_t componentCount;                     ///< コンポーネント数
	std::vector<std::string> componentNames;   ///< コンポーネント名リスト
	bool isAlive;                              ///< 生存フラグ
};

/// @brief ECSワールドの統計情報
struct WorldStats
{
	size_t entityCount;            ///< 総エンティティ数
	size_t aliveEntityCount;       ///< 生存エンティティ数
	size_t componentTypeCount;     ///< コンポーネント型数
	size_t systemCount;            ///< システム数
	std::vector<EntityInfo> entities;  ///< エンティティ情報リスト
};

/// @brief ワールドの統計情報を収集する
/// @tparam WorldType ECSワールド型（entityCount(), isAlive(uint32_t)を持つこと）
/// @param world 対象ワールド
/// @return 収集した統計情報
///
/// @note WorldTypeには以下のAPIが必要:
/// - size_t entityCount() const
/// - bool isAlive(uint32_t id) const
/// - size_t componentTypeCount() const (省略可)
/// - size_t systemCount() const (省略可)
template <typename WorldType>
[[nodiscard]] WorldStats collectWorldStats(const WorldType& world)
{
	WorldStats stats{};
	stats.entityCount = world.entityCount();

	// 生存エンティティ数を計算
	size_t alive = 0;
	for (uint32_t i = 0; i < static_cast<uint32_t>(stats.entityCount); ++i)
	{
		const bool isAlive = world.isAlive(i);
		if (isAlive) ++alive;

		EntityInfo info{};
		info.id = i;
		info.componentCount = 0;
		info.isAlive = isAlive;
		stats.entities.push_back(info);
	}
	stats.aliveEntityCount = alive;

	// オプショナルなAPI呼び出し
	if constexpr (requires { world.componentTypeCount(); })
	{
		stats.componentTypeCount = world.componentTypeCount();
	}
	if constexpr (requires { world.systemCount(); })
	{
		stats.systemCount = world.systemCount();
	}

	return stats;
}

} // namespace sgc::debug
