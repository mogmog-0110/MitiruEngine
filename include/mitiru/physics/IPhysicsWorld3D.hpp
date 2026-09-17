#pragma once

/// @file IPhysicsWorld3D.hpp
/// @brief 3D物理ワールドの純粋仮想インターフェース
///
/// `PhysicsWorld3D`（内蔵ソルバー）が実装する。step / raycast 系 / overlap 系 /
/// gravity / 衝突コールバックという「ワールドに対して問い合わせる」操作だけを
/// 切り出しており、`addBody()` のようにバックエンド固有のボディ型（内蔵は
/// `RigidBody3D&`、Jolt は `JoltBodyId`）を返す操作は含まない。

#include <cstdint>
#include <optional>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "mitiru/physics/Collider3D.hpp"

namespace mitiru::physics3d
{

struct RayHit3D;
struct OverlapResult3D;

/// @brief 共通ボディ識別子。各バックエンド内部の ID 空間（Jolt の BodyID、
/// NativeEngine の BodyId 等）とは別に、アダプタ層で発行・管理する。
using BodyId = std::uint32_t;

/// @brief 無効なボディID
inline constexpr BodyId kInvalidBodyId = 0;

/// @brief `addBody()` の最小生成情報。3バックエンド共通で表現できる範囲のみを持ち、
/// backend固有機能（ジョイント・トリガー・CCD等）はここに含めない。
struct BodyDesc
{
	enum class Type { Dynamic, Static, Kinematic };
	enum class Shape { Sphere, Box, Capsule };

	Type type{Type::Dynamic};
	Shape shape{Shape::Box};
	sgc::Vec3f position{};
	sgc::Quaternionf rotation{};
	sgc::Vec3f halfExtents{0.5f, 0.5f, 0.5f}; ///< Box
	float radius{0.5f};                        ///< Sphere / Capsule
	float halfHeight{0.5f};                    ///< Capsule（半球間距離の半分）
	float mass{1.0f};                          ///< Static/Kinematicでは無視される
	std::uint32_t layer{0};                    ///< 所属レイヤー（0-31）
	std::uint32_t mask{0xFFFFFFFFu};           ///< 衝突対象レイヤーマスク
};

/// @brief ボディの姿勢（位置+回転）
struct BodyTransform
{
	sgc::Vec3f position{};
	sgc::Quaternionf rotation{};
};

/// @brief 3D物理ワールドの共通操作
///
/// @details JoltPhysics.hpp / NativePhysicsBridge.hpp は本インターフェースを
///          継承していない。理由（ボディ生成 API・名前空間・ECS 統合方式が
///          `PhysicsWorld3D` と異なる）は `docs/PHYSICS_NATIVE_BACKEND.md` を参照。
class IPhysicsWorld3D
{
public:
	virtual ~IPhysicsWorld3D() = default;

	/// @brief シミュレーションを1ステップ進める
	virtual void step(float dt) noexcept = 0;

	/// @brief 重力を設定する
	virtual void setGravity(const sgc::Vec3f& gravity) noexcept = 0;

	/// @brief 重力を取得する
	[[nodiscard]] virtual const sgc::Vec3f& gravity() const noexcept = 0;

	/// @brief 最も近いヒットを1件返すレイキャスト
	[[nodiscard]] virtual std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist, std::uint32_t mask) const noexcept = 0;

	/// @brief レイと交差する全コライダーを収集する
	virtual void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept = 0;

	/// @brief 球とのオーバーラップ問い合わせ
	virtual void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept = 0;

	/// @brief AABBとのオーバーラップ問い合わせ
	virtual void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept = 0;

	/// @brief ボディを追加する
	[[nodiscard]] virtual BodyId addBody(const BodyDesc& desc) noexcept = 0;

	/// @brief ボディを削除する
	virtual void removeBody(BodyId id) noexcept = 0;

	/// @brief ボディの現在姿勢を取得する（存在しなければ nullopt）
	[[nodiscard]] virtual std::optional<BodyTransform> bodyTransform(BodyId id) const noexcept = 0;
};

} // namespace mitiru::physics3d
