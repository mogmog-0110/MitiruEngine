#pragma once

/// @file IPhysicsWorld3D.hpp
/// @brief 3D 物理ワールドの純粋仮想インターフェース
///
/// 既定の実装は Jolt Physics を使う `JoltPhysicsWorld3D`。`NativePhysicsWorld3D`
/// (周期境界、opt-in) も同じ契約を満たす。step / raycast 系 / overlap 系 / gravity /
/// addBody という「ワールドに対して問い合わせる」操作だけを持ち、接触イベントや
/// センサーのようなバックエンド固有の機能は各実装の側に置く。

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"
#include "mitiru/physics/Collider3D.hpp"

namespace mitiru::physics3d
{

/// @brief 共通ボディ識別子。各バックエンド内部の ID 空間（Jolt の BodyID、
/// NativeEngine の BodyId 等）とは別に、アダプタ層で発行・管理する。
using BodyId = std::uint32_t;

/// @brief 無効なボディ ID
inline constexpr BodyId kInvalidBodyId = 0;

/// @details HE2 の GOCCollider::filterCategory と同じ双方向ビット判定
[[nodiscard]] constexpr bool layersCollide(
	std::uint32_t layerA, std::uint32_t maskA,
	std::uint32_t layerB, std::uint32_t maskB) noexcept
{
	// layer は契約上 0-31。範囲外を渡されても `1u << layer` のシフト量 >= 32 (未定義動作) にならないよう範囲内に収める
	return ((maskA & (1u << (layerB & 31u))) != 0u) && ((maskB & (1u << (layerA & 31u))) != 0u);
}

[[nodiscard]] constexpr bool layerMatchesQuery(std::uint32_t colliderLayer, std::uint32_t queryMask) noexcept
{
	return (queryMask & (1u << (colliderLayer & 31u))) != 0u;
}

/// @brief レイキャストのヒット 1 件
struct RayHit3D
{
	sgc::Vec3f point{};              ///< ヒット座標
	sgc::Vec3f normal{};             ///< ヒット面の法線
	float distance{0.0f};            ///< レイ始点からの距離
	BodyId bodyId{kInvalidBodyId};   ///< 当たったボディ (分からないバックエンドでは kInvalidBodyId)
	std::uint32_t layer{0};          ///< 当たったボディの所属レイヤー
};

/// @brief オーバーラップ問い合わせのヒット 1 件
struct OverlapResult3D
{
	BodyId bodyId{kInvalidBodyId};
	std::size_t colliderIndex{0};
};

/// @brief ボディ同士の接触 1 件。bodyA < bodyB、normal は A から B へ押し出す向き。
struct BodyContact3D
{
	BodyId bodyA{kInvalidBodyId};
	BodyId bodyB{kInvalidBodyId};
	sgc::Vec3f point{};
	sgc::Vec3f normal{};
	float depth{0.0f};
	bool isSensor{false};   ///< どちらかがセンサー (押し返さない)
};

/// @brief `addBody()` の最小生成情報。どのバックエンドでも表現できる範囲だけを持ち、
/// センサー・材質・ジョイント等は各実装の API で後から設定する。
struct BodyDesc
{
	enum class Type { Dynamic, Static, Kinematic };
	enum class Shape { Sphere, Box, Capsule, Mesh };

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

	/// Mesh: 三角形の列 (ローカル座標、添字は 3 つで 1 枚)。addBody() の中で写し取るので、
	/// 呼び出しの後は手放してよい。動かない地形向けで、Dynamic のメッシュは作れない。
	const sgc::Vec3f* meshVertices{nullptr};
	std::size_t meshVertexCount{0};
	const std::uint32_t* meshIndices{nullptr};
	std::size_t meshIndexCount{0};
};

/// @brief ボディの姿勢（位置+回転）
struct BodyTransform
{
	sgc::Vec3f position{};
	sgc::Quaternionf rotation{};
};

/// @brief 3D 物理ワールドの共通操作
class IPhysicsWorld3D
{
public:
	virtual ~IPhysicsWorld3D() = default;

	/// @brief シミュレーションを dt 秒進める
	virtual void step(float dt) noexcept = 0;

	/// @brief 重力を設定する
	virtual void setGravity(const sgc::Vec3f& gravity) noexcept = 0;

	/// @brief 重力を取得する
	[[nodiscard]] virtual const sgc::Vec3f& gravity() const noexcept = 0;

	/// @brief 最も近いヒットを 1 件返すレイキャスト
	[[nodiscard]] virtual std::optional<RayHit3D> raycast(
		const Ray3D& ray, float maxDist, std::uint32_t mask) const noexcept = 0;

	/// @brief レイと交差する全コライダーを収集する
	virtual void raycastAll(const Ray3D& ray, float maxDist, std::uint32_t mask,
		std::vector<RayHit3D>& outHits) const noexcept = 0;

	/// @brief 球とのオーバーラップ問い合わせ
	virtual void overlapSphere(const sgc::Vec3f& center, float radius, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept = 0;

	/// @brief AABB とのオーバーラップ問い合わせ
	virtual void overlapBox(const AABBCollider3D& aabb, std::uint32_t mask,
		std::vector<OverlapResult3D>& outResults) const noexcept = 0;

	/// @brief ボディを追加する（作れない形状なら kInvalidBodyId）
	[[nodiscard]] virtual BodyId addBody(const BodyDesc& desc) noexcept = 0;

	/// @brief ボディを削除する
	virtual void removeBody(BodyId id) noexcept = 0;

	/// @brief ボディの現在姿勢を取得する（存在しなければ nullopt）
	[[nodiscard]] virtual std::optional<BodyTransform> bodyTransform(BodyId id) const noexcept = 0;
};

} // namespace mitiru::physics3d
