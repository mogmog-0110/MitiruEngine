#pragma once

/// @file CollisionFilter.hpp
/// @brief 衝突レイヤー・マスクによるフィルタリング
///
/// 剛体にレイヤーとマスクを設定し、衝突ペアの選択的フィルタリングを行う。
/// ビット演算により高速に衝突の可否を判定する。
///
/// @code
/// using namespace sgc::physics;
///
/// CollisionFilter player;
/// player.layer = 0x01;  // プレイヤーレイヤー
/// player.mask  = 0x02;  // 敵レイヤーとのみ衝突
///
/// CollisionFilter enemy;
/// enemy.layer = 0x02;   // 敵レイヤー
/// enemy.mask  = 0x01;   // プレイヤーレイヤーとのみ衝突
///
/// shouldCollide(player, enemy); // true
/// @endcode

#include <cstdint>

namespace sgc::physics
{

/// @brief 衝突フィルター（レイヤー＋マスク）
///
/// layer はこの剛体が属するレイヤービットフラグ。
/// mask はこの剛体が衝突対象とするレイヤービットフラグ。
/// デフォルトでは全ビットが立っており、全てのオブジェクトと衝突する。
struct CollisionFilter
{
	uint32_t layer = 0xFFFFFFFF;  ///< この剛体が属するレイヤー
	uint32_t mask  = 0xFFFFFFFF;  ///< この剛体が衝突するレイヤーマスク

	/// @brief 等価比較演算子
	[[nodiscard]] constexpr bool operator==(const CollisionFilter&) const noexcept = default;
};

/// @brief 2つのフィルター間で衝突すべきかを判定する
///
/// AがBのレイヤーを対象としており、かつBがAのレイヤーを対象としている場合にtrueを返す。
/// 判定式: (a.mask & b.layer) != 0 && (b.mask & a.layer) != 0
///
/// @param a フィルターA
/// @param b フィルターB
/// @return 衝突すべきならtrue
[[nodiscard]] constexpr bool shouldCollide(const CollisionFilter& a, const CollisionFilter& b) noexcept
{
	return (a.mask & b.layer) != 0 && (b.mask & a.layer) != 0;
}

} // namespace sgc::physics
