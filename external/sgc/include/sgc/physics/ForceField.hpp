#pragma once

/// @file ForceField.hpp
/// @brief 環境フォースフィールド（力場ゾーン）
///
/// AABB領域内にいる剛体に力を加えるフォースフィールド群を提供する。
/// 重力反転、水中抵抗、風力、渦巻き等の環境ギミックを実現する。
///
/// @code
/// using namespace sgc::physics;
///
/// // 重力反転ゾーン
/// GravityFlipField flip{AABB2f{{100, 200}, {400, 300}}, {0, -15.0f}};
///
/// // 水中抵抗ゾーン
/// DragField water{AABB2f{{0, 400}, {800, 200}}, 0.85f, {0, -3.0f}};
///
/// // 風ゾーン
/// WindField wind{AABB2f{{300, 0}, {100, 600}}, {8.0f, 0}};
///
/// // RigidBody3Dに適用
/// RigidBody3D body;
/// flip.apply(body);
/// @endcode

#include <cmath>
#include <cstdint>

#include "sgc/math/Geometry.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/math/Vec3.hpp"

namespace sgc::physics
{

/// @brief フォースフィールドの種別
enum class ForceFieldType : int32_t
{
	Gravity,   ///< 重力（定常力）
	Drag,      ///< 抵抗（速度減衰 + 浮力）
	Wind,      ///< 風（定常方向力）
	Vortex,    ///< 渦（中心向き回転力）
	Impulse    ///< 瞬間力（入った瞬間のみ）
};

/// @brief 2Dフォースフィールド基底
///
/// AABB2f領域内にある物体に力を加える。
/// 2Dゲームでは(x,y)平面、3DではXZ平面としても利用可能。
struct ForceFieldBase
{
	AABB2f bounds{};          ///< 有効領域
	bool active = true;       ///< 有効フラグ

	/// @brief 位置がフィールド内にあるか判定する
	/// @param pos 判定位置（2D）
	/// @return フィールド内ならtrue
	[[nodiscard]] bool contains(const Vec2f& pos) const noexcept
	{
		return active && bounds.contains(pos);
	}

	/// @brief 位置がフィールド内にあるか判定する（3D、XZ平面で判定）
	/// @param pos 判定位置（3D）
	/// @return フィールドのXZ範囲内ならtrue
	[[nodiscard]] bool containsXZ(const Vec3f& pos) const noexcept
	{
		return contains(Vec2f{pos.x, pos.z});
	}
};

/// @brief 重力変更フィールド
///
/// 領域内の物体にカスタム重力を適用する。
/// 重力反転、低重力、無重力等を実現する。
struct GravityField : ForceFieldBase
{
	Vec2f gravity{0.0f, -9.8f};  ///< カスタム重力ベクトル

	/// @brief コンストラクタ
	/// @param area 有効領域
	/// @param g カスタム重力
	GravityField() noexcept = default;
	GravityField(const AABB2f& area, const Vec2f& g) noexcept
		: ForceFieldBase{area, true}, gravity{g} {}

	/// @brief 2D位置の物体に力を加える
	/// @param pos 物体の位置
	/// @param mass 物体の質量
	/// @param[out] outForce 加算する力
	/// @return フィールド内ならtrue
	[[nodiscard]] bool computeForce(const Vec2f& pos, float mass, Vec2f& outForce) const noexcept
	{
		if (!contains(pos)) return false;
		outForce = gravity * mass;
		return true;
	}
};

/// @brief 抵抗フィールド（水中・粘性等）
///
/// 領域内の物体に速度ダンピングと浮力を適用する。
struct DragField : ForceFieldBase
{
	float damping = 0.9f;          ///< 速度減衰係数（0.0〜1.0、1.0=減衰なし）
	Vec2f buoyancy{0.0f, 0.0f};   ///< 浮力ベクトル

	/// @brief コンストラクタ
	DragField() noexcept = default;
	DragField(const AABB2f& area, float damp, const Vec2f& buoy = {}) noexcept
		: ForceFieldBase{area, true}, damping{damp}, buoyancy{buoy} {}

	/// @brief 速度にダンピングを適用した結果を返す
	/// @param pos 物体の位置
	/// @param velocity 現在の速度
	/// @return ダンピング後の速度（フィールド外ならそのまま）
	[[nodiscard]] Vec2f applyDamping(const Vec2f& pos, const Vec2f& velocity) const noexcept
	{
		if (!contains(pos)) return velocity;
		return velocity * damping;
	}

	/// @brief 浮力を計算する
	/// @param pos 物体の位置
	/// @param mass 物体の質量
	/// @param[out] outForce 加算する力
	/// @return フィールド内ならtrue
	[[nodiscard]] bool computeForce(const Vec2f& pos, float mass, Vec2f& outForce) const noexcept
	{
		if (!contains(pos)) return false;
		outForce = buoyancy * mass;
		return true;
	}
};

/// @brief 風フィールド
///
/// 領域内の物体に一定方向の力を加える。
/// 質量非依存（軽い物体ほど大きく加速される）。
struct WindField : ForceFieldBase
{
	Vec2f force{0.0f, 0.0f};  ///< 風力ベクトル（質量非依存）

	/// @brief コンストラクタ
	WindField() noexcept = default;
	WindField(const AABB2f& area, const Vec2f& f) noexcept
		: ForceFieldBase{area, true}, force{f} {}

	/// @brief 風力を計算する
	/// @param pos 物体の位置
	/// @param[out] outForce 加算する力
	/// @return フィールド内ならtrue
	[[nodiscard]] bool computeForce(const Vec2f& pos, Vec2f& outForce) const noexcept
	{
		if (!contains(pos)) return false;
		outForce = force;
		return true;
	}
};

/// @brief 渦フィールド
///
/// 領域の中心に向かう力と接線方向の回転力を組み合わせる。
struct VortexField : ForceFieldBase
{
	float pullStrength = 5.0f;    ///< 中心への引力の強さ
	float spinStrength = 3.0f;    ///< 回転方向の力の強さ

	/// @brief コンストラクタ
	VortexField() noexcept = default;
	VortexField(const AABB2f& area, float pull, float spin) noexcept
		: ForceFieldBase{area, true}, pullStrength{pull}, spinStrength{spin} {}

	/// @brief 渦力を計算する
	/// @param pos 物体の位置
	/// @param[out] outForce 加算する力
	/// @return フィールド内ならtrue
	[[nodiscard]] bool computeForce(const Vec2f& pos, Vec2f& outForce) const noexcept
	{
		if (!contains(pos)) return false;

		const Vec2f center{
			(bounds.min.x + bounds.max.x) * 0.5f,
			(bounds.min.y + bounds.max.y) * 0.5f
		};
		const Vec2f toCenter = center - pos;
		const float dist = std::sqrt(toCenter.x * toCenter.x + toCenter.y * toCenter.y);
		if (dist < 0.001f)
		{
			outForce = {};
			return true;
		}

		const Vec2f dir = toCenter / dist;
		const Vec2f tangent{-dir.y, dir.x};  // 90度回転

		outForce = dir * pullStrength + tangent * spinStrength;
		return true;
	}
};

} // namespace sgc::physics
