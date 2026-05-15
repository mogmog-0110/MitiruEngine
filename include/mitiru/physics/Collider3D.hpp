#pragma once

/// @file Collider3D.hpp
/// @brief 3D衝突形状の定義
///
/// 球、AABB、OBB、カプセルの3Dコライダーを提供する。
///
/// @code
/// mitiru::physics3d::SphereCollider sphere{{0, 0, 0}, 1.0f};
/// mitiru::physics3d::AABBCollider3D aabb{{-1, -1, -1}, {1, 1, 1}};
/// mitiru::physics3d::OBBCollider3D obb{{0, 0, 0}, {1, 1, 1}, sgc::Quaternionf::identity()};
/// mitiru::physics3d::CapsuleCollider capsule{{0, 0, 0}, {0, 2, 0}, 0.5f};
/// @endcode

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "sgc/math/Mat3.hpp"

namespace mitiru::physics3d
{

/// @brief 3Dレイ
struct Ray3D
{
	sgc::Vec3f origin{};      ///< 始点
	sgc::Vec3f direction{};   ///< 方向（正規化推奨）
};

/// @brief 3D接触情報
struct ContactInfo3D
{
	sgc::Vec3f point{};     ///< 接触点
	sgc::Vec3f normal{};    ///< 接触法線（AからBへの方向）
	float depth{0.0f};      ///< 貫通深度
	bool hasContact{false}; ///< 接触が発生したか
};

/// @brief 球コライダー
struct SphereCollider
{
	sgc::Vec3f center{};    ///< 中心座標
	float radius{0.5f};     ///< 半径
};

/// @brief 3D軸平行バウンディングボックス
struct AABBCollider3D
{
	sgc::Vec3f min{};   ///< 最小角
	sgc::Vec3f max{};   ///< 最大角

	/// @brief 中心座標を返す
	[[nodiscard]] constexpr sgc::Vec3f center() const noexcept
	{
		return (min + max) * 0.5f;
	}

	/// @brief 半径サイズを返す
	[[nodiscard]] constexpr sgc::Vec3f halfExtents() const noexcept
	{
		return (max - min) * 0.5f;
	}

	/// @brief 中心と半径サイズから構築する
	/// @param center 中心座標
	/// @param halfExtents 半径サイズ
	/// @return AABBCollider3D
	[[nodiscard]] static constexpr AABBCollider3D fromCenterExtents(
		const sgc::Vec3f& center, const sgc::Vec3f& halfExtents) noexcept
	{
		return {center - halfExtents, center + halfExtents};
	}
};

/// @brief 有向バウンディングボックス（OBB）
struct OBBCollider3D
{
	sgc::Vec3f center{};                               ///< 中心座標
	sgc::Vec3f halfExtents{0.5f, 0.5f, 0.5f};         ///< 半径サイズ（ローカル空間）
	sgc::Quaternionf orientation{};                     ///< 回転（ワールド空間）

	/// @brief ローカル軸ベクトルを取得する
	/// @param axisIndex 0=X, 1=Y, 2=Z
	/// @return ワールド空間の軸方向ベクトル
	[[nodiscard]] sgc::Vec3f axis(int axisIndex) const noexcept
	{
		switch (axisIndex)
		{
		case 0: return orientation.rotate(sgc::Vec3f::unitX());
		case 1: return orientation.rotate(sgc::Vec3f::unitY());
		case 2: return orientation.rotate(sgc::Vec3f::unitZ());
		default: return {};
		}
	}
};

/// @brief カプセルコライダー（2端点 + 半径）
struct CapsuleCollider
{
	sgc::Vec3f pointA{};       ///< 端点A
	sgc::Vec3f pointB{};       ///< 端点B
	float radius{0.25f};       ///< 半径

	/// @brief 中心座標を返す
	[[nodiscard]] constexpr sgc::Vec3f center() const noexcept
	{
		return (pointA + pointB) * 0.5f;
	}

	/// @brief 軸方向を返す（AからBへの方向）
	[[nodiscard]] sgc::Vec3f axisDirection() const noexcept
	{
		return (pointB - pointA).normalized();
	}

	/// @brief 軸の長さを返す（端点間距離）
	[[nodiscard]] float axisLength() const noexcept
	{
		return (pointB - pointA).length();
	}
};

} // namespace mitiru::physics3d
