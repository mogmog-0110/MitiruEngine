#pragma once

/// @file Collider3D.hpp
/// @brief 3D の形の値型 (レイ・軸平行の箱・カプセル・接触情報)
///
/// 物理ワールドの問い合わせ (IPhysicsWorld3D) が使う。衝突の解決そのものは Jolt が持つ。
/// game DLL の中で完結する当たり判定は mitiru/action/ が自分の型で持つ。

#include "sgc/math/Vec3.hpp"

namespace mitiru::physics3d
{

/// @brief 3D レイ
struct Ray3D
{
	sgc::Vec3f origin{};      ///< 始点
	sgc::Vec3f direction{};   ///< 方向（正規化推奨）
};

/// @brief 3D 接触情報
struct ContactInfo3D
{
	sgc::Vec3f point{};     ///< 接触点
	sgc::Vec3f normal{};    ///< 接触法線（AからBへの方向）
	float depth{0.0f};      ///< 貫通深度
	bool hasContact{false}; ///< 接触が発生したか
};

/// @brief 3D 軸平行バウンディングボックス
struct AABBCollider3D
{
	sgc::Vec3f min{};   ///< 最小角
	sgc::Vec3f max{};   ///< 最大角

	[[nodiscard]] constexpr sgc::Vec3f center() const noexcept
	{
		return (min + max) * 0.5f;
	}

	[[nodiscard]] constexpr sgc::Vec3f halfExtents() const noexcept
	{
		return (max - min) * 0.5f;
	}

	[[nodiscard]] static constexpr AABBCollider3D fromCenterExtents(
		const sgc::Vec3f& center, const sgc::Vec3f& halfExtents) noexcept
	{
		return {center - halfExtents, center + halfExtents};
	}
};

/// @brief カプセルコライダー（2 端点 + 半径）
struct CapsuleCollider
{
	sgc::Vec3f pointA{};       ///< 端点A
	sgc::Vec3f pointB{};       ///< 端点B
	float radius{0.25f};       ///< 半径
};

} // namespace mitiru::physics3d
