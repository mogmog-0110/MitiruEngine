#pragma once

/// @file RigidBodyComponent3D.hpp
/// @brief 3D 剛体の ECS コンポーネント
///
/// TransformComponent と組み合わせて GameWorld に付ける。どの物理システムが解くかは
/// SystemRunner に足した側で決まる。既定は Jolt の PhysicsSystem3D で、周期境界が要るときは
/// NativePhysicsSystem を使う。

#include <cstdint>
#include <memory>
#include <vector>

#include "sgc/math/Vec3.hpp"

namespace mitiru::physics3d
{

/// @brief メッシュコライダーの三角形 (ローカル座標、添字 3 つで 1 枚、表から見て反時計回り)。
/// @details 描画用のメッシュから作って複数のエンティティで共有するので、const で持ち回す。
struct TriangleMesh3D
{
	std::vector<sgc::Vec3f> vertices;
	std::vector<std::uint32_t> indices;
};

/// @brief コライダーの形。Box はボディと一緒に回る直方体
enum class ColliderType3D
{
	Sphere,
	Box,
	Capsule,
	Mesh     ///< colliderMesh の三角形。動かない地形か Kinematic 向けで、Dynamic には付けられない
};

/// @brief 3D 剛体コンポーネント
struct RigidBodyComponent3D
{
	float mass{1.0f};                 ///< 質量（kg）。0以下で静的ボディ
	float restitution{0.3f};          ///< 反発係数
	float friction{0.5f};             ///< 摩擦係数
	float linearDamping{0.01f};       ///< 線形減衰率
	float angularDamping{0.05f};      ///< 角速度減衰率
	ColliderType3D colliderType{ColliderType3D::Sphere};  ///< コライダータイプ
	float colliderRadius{0.5f};       ///< 球/カプセル半径
	sgc::Vec3f colliderHalfExtents{0.5f, 0.5f, 0.5f}; ///< Box の半径サイズ
	float capsuleHeight{1.0f};        ///< カプセルの胴の長さ（両端の半球を除く）
	std::shared_ptr<const TriangleMesh3D> colliderMesh;  ///< Mesh の三角形。TransformComponent::scale を掛けて使う
	bool isKinematic{false};          ///< キネマティックボディか（Transform を正として動かす）
	sgc::Vec3f linearVelocity{};      ///< 線形速度（生成時に渡し、毎フレーム書き戻される）
	sgc::Vec3f angularVelocity{};     ///< 角速度（同上）

	bool isTrigger{false};                     ///< true なら押し返さず接触だけを報告する
	std::uint32_t layer{0};                    ///< 所属レイヤー（0-31のビットindex）
	std::uint32_t mask{0xFFFFFFFFu};           ///< 衝突対象レイヤーマスク
};

} // namespace mitiru::physics3d
