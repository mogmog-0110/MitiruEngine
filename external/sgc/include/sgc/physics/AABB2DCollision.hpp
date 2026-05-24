#pragma once

/// @file AABB2DCollision.hpp
/// @brief AABB同士の2D衝突検出と応答
///
/// 軸平行バウンディングボックス（AABB）間の衝突検出、
/// めり込み深さ・衝突法線の計算、分離ベクトルによる衝突解決を提供する。
///
/// @code
/// using namespace sgc::physics;
///
/// sgc::AABB2f boxA{{0, 0}, {10, 10}};
/// sgc::AABB2f boxB{{8, 3}, {18, 13}};
///
/// auto result = detectAABBCollision(boxA, boxB);
/// if (result.colliding)
/// {
///     auto resolved = resolveCollision(boxA, result);
///     // resolvedはboxBと重ならないAABB
/// }
/// @endcode

#include "sgc/math/Geometry.hpp"

namespace sgc::physics
{

/// @brief AABB衝突結果
/// @tparam T 浮動小数点型
template <FloatingPoint T>
struct CollisionResult2D
{
	bool colliding{false};          ///< 衝突しているか
	Vec2<T> normal{};               ///< 衝突法線（AからBへの方向）
	T penetration{};                ///< めり込み深さ
	Vec2<T> separationVector{};     ///< 分離ベクトル（Aに適用すると離れる）
};

/// @brief AABB同士の衝突検出と分離ベクトル計算
///
/// 2つのAABBの重なりを検出し、最小めり込み軸に基づいて
/// 衝突法線と分離ベクトルを計算する。
///
/// @tparam T 浮動小数点型
/// @param a 1つ目のAABB
/// @param b 2つ目のAABB
/// @return 衝突結果（衝突していない場合はcolliding=false）
///
/// @code
/// sgc::AABB2f player{{5, 5}, {15, 15}};
/// sgc::AABB2f wall{{12, 0}, {20, 20}};
/// auto result = detectAABBCollision(player, wall);
/// // result.colliding == true
/// // result.normal == {1, 0}（右方向）
/// // result.penetration == 3.0f
/// @endcode
template <FloatingPoint T>
[[nodiscard]] constexpr CollisionResult2D<T> detectAABBCollision(
	const AABB2<T>& a, const AABB2<T>& b) noexcept
{
	CollisionResult2D<T> result;

	// 各軸のオーバーラップを計算
	const T overlapX1 = a.max.x - b.min.x;  // Aの右端 - Bの左端
	const T overlapX2 = b.max.x - a.min.x;  // Bの右端 - Aの左端
	const T overlapY1 = a.max.y - b.min.y;  // Aの下端 - Bの上端
	const T overlapY2 = b.max.y - a.min.y;  // Bの下端 - Aの上端

	// いずれかの軸で分離されていれば衝突なし
	if (overlapX1 <= T{0} || overlapX2 <= T{0} ||
		overlapY1 <= T{0} || overlapY2 <= T{0})
	{
		return result;
	}

	// 各軸の最小オーバーラップと方向を決定
	const T overlapX = (overlapX1 < overlapX2) ? overlapX1 : overlapX2;
	const T signX = (overlapX1 < overlapX2) ? T{1} : T{-1};

	const T overlapY = (overlapY1 < overlapY2) ? overlapY1 : overlapY2;
	const T signY = (overlapY1 < overlapY2) ? T{1} : T{-1};

	// 最小めり込み軸を選択
	result.colliding = true;

	if (overlapX < overlapY)
	{
		result.normal = {signX, T{0}};
		result.penetration = overlapX;
	}
	else
	{
		result.normal = {T{0}, signY};
		result.penetration = overlapY;
	}

	// 分離ベクトル = 法線の逆方向 × めり込み深さ（Aを押し戻す）
	result.separationVector = result.normal * (-result.penetration);

	return result;
}

/// @brief 分離ベクトルを適用してAABBを移動する
///
/// 衝突結果の分離ベクトルをAABBに適用して、重なりを解消する。
///
/// @tparam T 浮動小数点型
/// @param movable 移動対象のAABB
/// @param result detectAABBCollisionの戻り値
/// @return 分離後の新しいAABB
template <FloatingPoint T>
[[nodiscard]] constexpr AABB2<T> resolveCollision(
	const AABB2<T>& movable, const CollisionResult2D<T>& result) noexcept
{
	return {
		movable.min + result.separationVector,
		movable.max + result.separationVector
	};
}

using CollisionResult2Df = CollisionResult2D<float>;

} // namespace sgc::physics
