#pragma once

/// @file PhysicsBridge.hpp
/// @brief sgc物理エンジンとmitiruのGameWorldを接続するブリッジ
///
/// TransformComponent + RigidBodyComponent を持つエンティティに対して
/// 簡易的な位置ベース物理（オイラー積分）を適用する。
///
/// @code
/// mitiru::physics::PhysicsBridge bridge;
/// bridge.init(mitiru::physics::PhysicsConfig{});
/// bridge.syncToPhysics(world);
/// bridge.stepSimulation(1.0f / 60.0f);
/// bridge.syncFromPhysics(world);
/// @endcode

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Mat4.hpp"
#include "mitiru/scene/GameWorld.hpp"

namespace mitiru::physics
{

// ── ブロードフェーズ種別 ──────────────────────────────────

/// @brief ブロードフェーズ衝突検出アルゴリズム
enum class BroadphaseType
{
	BruteForce,    ///< 全ペア総当たり
	SpatialHash,   ///< 空間ハッシュ
	BVH            ///< バウンディングボリューム階層
};

// ── 物理設定 ──────────────────────────────────────────────

/// @brief 物理シミュレーション設定
struct PhysicsConfig
{
	sgc::Vec3f gravity{0.0f, -9.81f, 0.0f};  ///< 重力加速度（m/s^2）
	float fixedTimeStep{1.0f / 60.0f};        ///< 固定ステップ幅（秒）
	int maxSubSteps{8};                        ///< 最大サブステップ数
	BroadphaseType broadphaseType{BroadphaseType::BruteForce}; ///< ブロードフェーズ種別
};

// ── コライダー形状 ────────────────────────────────────────

/// @brief コライダー形状の種類
enum class ColliderShapeType
{
	Sphere,   ///< 球
	Box,      ///< 直方体
	Capsule,  ///< カプセル
	Mesh      ///< メッシュ（未実装、予約）
};

/// @brief 球形状データ
struct SphereData
{
	float radius{0.5f};  ///< 半径
};

/// @brief ボックス形状データ
struct BoxData
{
	sgc::Vec3f halfExtents{0.5f, 0.5f, 0.5f};  ///< 半径サイズ
};

/// @brief カプセル形状データ
struct CapsuleData
{
	float radius{0.25f};  ///< 半径
	float height{1.0f};   ///< 高さ（半球間の距離）
};

/// @brief メッシュ形状データ（プレースホルダ）
struct MeshData
{
	std::string meshId;  ///< メッシュアセットID
};

/// @brief 形状データバリアント
using ShapeData = std::variant<SphereData, BoxData, CapsuleData, MeshData>;

/// @brief コライダー形状
struct ColliderShape
{
	ColliderShapeType type{ColliderShapeType::Sphere};  ///< 形状タイプ
	ShapeData data{SphereData{}};                       ///< 形状パラメータ
};

// ── 剛体コンポーネント ────────────────────────────────────

/// @brief 剛体コンポーネント（GameWorldに登録して使用）
struct RigidBodyComponent
{
	float mass{1.0f};             ///< 質量（kg）、0なら静的
	float restitution{0.3f};      ///< 反発係数
	float friction{0.5f};         ///< 摩擦係数
	float linearDamping{0.01f};   ///< 線形減衰
	float angularDamping{0.05f};  ///< 角速度減衰
	ColliderShape shape{};         ///< コライダー形状
	bool isKinematic{false};       ///< キネマティックか
	bool isTrigger{false};         ///< トリガーか（衝突応答なし）
};

// ── レイキャスト結果 ──────────────────────────────────────

/// @brief レイキャストヒット結果
struct RaycastHit
{
	scene::EntityId entityId{scene::INVALID_ENTITY};  ///< ヒットしたエンティティ
	sgc::Vec3f point{};                                ///< ヒット座標
	sgc::Vec3f normal{};                               ///< ヒット面の法線
	float distance{0.0f};                              ///< レイ始点からの距離
};

// ── 内部物理状態 ──────────────────────────────────────────

/// @brief エンティティごとの物理状態（内部使用）
struct InternalBodyState
{
	sgc::Vec3f position{};       ///< 位置
	sgc::Vec3f rotation{};       ///< 回転（オイラー角）
	sgc::Vec3f velocity{};       ///< 線形速度
	sgc::Vec3f angularVelocity{};///< 角速度
	float mass{1.0f};            ///< 質量
	float restitution{0.3f};     ///< 反発係数
	float friction{0.5f};        ///< 摩擦係数
	float linearDamping{0.01f};  ///< 線形減衰
	float angularDamping{0.05f}; ///< 角速度減衰
	ColliderShape shape{};        ///< コライダー形状
	bool isKinematic{false};      ///< キネマティックか
	bool isTrigger{false};        ///< トリガーか
};

// ── PhysicsBridge ─────────────────────────────────────────

/// @brief sgc物理とmitiruのGameWorldを接続するブリッジ
///
/// 簡易的なオイラー積分による位置ベース物理シミュレーションを提供する。
/// 外部の物理エンジンに依存せず、基本的な重力・減衰・レイキャストをサポートする。
class PhysicsBridge
{
public:
	/// @brief 物理ブリッジを初期化する
	/// @param config 物理設定
	void init(const PhysicsConfig& config)
	{
		m_config = config;
		m_bodies.clear();
		m_initialized = true;
	}

	/// @brief 物理ブリッジをシャットダウンする
	void shutdown()
	{
		m_bodies.clear();
		m_initialized = false;
	}

	/// @brief 初期化済みかどうかを返す
	/// @return 初期化済みならtrue
	[[nodiscard]] bool isInitialized() const noexcept
	{
		return m_initialized;
	}

	/// @brief 現在の設定を返す
	/// @return 物理設定の参照
	[[nodiscard]] const PhysicsConfig& config() const noexcept
	{
		return m_config;
	}

	/// @brief GameWorldからTransformComponent + RigidBodyComponentを読み取り内部シミュレーションに反映する
	/// @param world ゲームワールド
	void syncToPhysics(scene::GameWorld& world)
	{
		world.forEach<RigidBodyComponent>(
			[this, &world](scene::EntityId id, RigidBodyComponent& rb)
			{
				auto* transform = world.getComponent<scene::TransformComponent>(id);
				if (!transform) return;

				auto it = m_bodies.find(id);
				if (it == m_bodies.end())
				{
					// 新規エンティティ：内部状態を作成
					InternalBodyState state;
					state.position = transform->position;
					state.rotation = transform->rotation;
					state.mass = rb.mass;
					state.restitution = rb.restitution;
					state.friction = rb.friction;
					state.linearDamping = rb.linearDamping;
					state.angularDamping = rb.angularDamping;
					state.shape = rb.shape;
					state.isKinematic = rb.isKinematic;
					state.isTrigger = rb.isTrigger;
					m_bodies[id] = state;
				}
				else
				{
					// 既存エンティティ：位置とパラメータを同期
					it->second.position = transform->position;
					it->second.rotation = transform->rotation;
					it->second.mass = rb.mass;
					it->second.restitution = rb.restitution;
					it->second.friction = rb.friction;
					it->second.linearDamping = rb.linearDamping;
					it->second.angularDamping = rb.angularDamping;
					it->second.shape = rb.shape;
					it->second.isKinematic = rb.isKinematic;
					it->second.isTrigger = rb.isTrigger;
				}
			}
		);
	}

	/// @brief 物理シミュレーションを1ステップ進める
	/// @param dt フレームデルタタイム（秒）
	///
	/// @details 固定タイムステップでオイラー積分を実行する。
	///   1. 重力を適用（動的ボディのみ）
	///   2. 速度から位置を更新
	///   3. 減衰を適用
	void stepSimulation(float dt)
	{
		m_accumulator += dt;

		int steps = 0;
		while (m_accumulator >= m_config.fixedTimeStep && steps < m_config.maxSubSteps)
		{
			integrateStep(m_config.fixedTimeStep);
			m_accumulator -= m_config.fixedTimeStep;
			++steps;
		}
	}

	/// @brief 内部シミュレーション結果をGameWorldのTransformComponentに書き戻す
	/// @param world ゲームワールド
	void syncFromPhysics(scene::GameWorld& world)
	{
		for (auto& [entityId, state] : m_bodies)
		{
			auto* transform = world.getComponent<scene::TransformComponent>(entityId);
			if (!transform) continue;

			transform->position = state.position;
			transform->rotation = state.rotation;
		}
	}

	/// @brief レイキャストを実行する
	/// @param origin レイの始点
	/// @param direction レイの方向（正規化推奨）
	/// @param maxDist 最大検出距離
	/// @return 最も近いヒット結果（ヒットなしの場合はnullopt）
	[[nodiscard]] std::optional<RaycastHit> raycast(
		const sgc::Vec3f& origin,
		const sgc::Vec3f& direction,
		float maxDist = 1000.0f) const
	{
		const sgc::Vec3f dir = direction.normalized();
		std::optional<RaycastHit> closest;

		for (const auto& [entityId, state] : m_bodies)
		{
			// 球コライダーのみレイキャスト対応
			if (state.shape.type != ColliderShapeType::Sphere) continue;

			const auto& sphere = std::get<SphereData>(state.shape.data);
			const auto hit = raycastSphere(origin, dir, state.position, sphere.radius, maxDist);

			if (hit.has_value())
			{
				if (!closest.has_value() || hit->distance < closest->distance)
				{
					RaycastHit result;
					result.entityId = entityId;
					result.point = hit->point;
					result.normal = hit->normal;
					result.distance = hit->distance;
					closest = result;
				}
			}
		}

		return closest;
	}

	/// @brief 内部ボディ数を返す（デバッグ用）
	/// @return 管理中のボディ数
	[[nodiscard]] std::size_t bodyCount() const noexcept
	{
		return m_bodies.size();
	}

	/// @brief エンティティの速度を取得する（デバッグ・テスト用）
	/// @param entityId エンティティID
	/// @return 速度ベクトル（見つからない場合はゼロ）
	[[nodiscard]] sgc::Vec3f getVelocity(scene::EntityId entityId) const noexcept
	{
		auto it = m_bodies.find(entityId);
		if (it == m_bodies.end()) return {};
		return it->second.velocity;
	}

	/// @brief エンティティの内部位置を取得する（デバッグ・テスト用）
	/// @param entityId エンティティID
	/// @return 位置ベクトル（見つからない場合はゼロ）
	[[nodiscard]] sgc::Vec3f getPosition(scene::EntityId entityId) const noexcept
	{
		auto it = m_bodies.find(entityId);
		if (it == m_bodies.end()) return {};
		return it->second.position;
	}

private:
	/// @brief 球へのレイキャスト結果（内部用）
	struct SphereHitResult
	{
		sgc::Vec3f point;   ///< ヒット座標
		sgc::Vec3f normal;  ///< ヒット法線
		float distance;     ///< 距離
	};

	/// @brief 球に対するレイキャスト（解析解）
	/// @param origin レイ始点
	/// @param dir レイ方向（正規化済み）
	/// @param center 球の中心
	/// @param radius 球の半径
	/// @param maxDist 最大距離
	/// @return ヒット結果（ヒットなしの場合はnullopt）
	[[nodiscard]] static std::optional<SphereHitResult> raycastSphere(
		const sgc::Vec3f& origin,
		const sgc::Vec3f& dir,
		const sgc::Vec3f& center,
		float radius,
		float maxDist)
	{
		const sgc::Vec3f oc = origin - center;
		const float a = dir.dot(dir);
		const float b = 2.0f * oc.dot(dir);
		const float c = oc.dot(oc) - radius * radius;
		const float discriminant = b * b - 4.0f * a * c;

		if (discriminant < 0.0f) return std::nullopt;

		const float sqrtD = std::sqrt(discriminant);
		float t = (-b - sqrtD) / (2.0f * a);

		// レイの後方にある場合、もう一方の交点を試す
		if (t < 0.0f)
		{
			t = (-b + sqrtD) / (2.0f * a);
		}

		if (t < 0.0f || t > maxDist) return std::nullopt;

		const sgc::Vec3f hitPoint = origin + dir * t;
		const sgc::Vec3f hitNormal = (hitPoint - center).normalized();

		return SphereHitResult{hitPoint, hitNormal, t};
	}

	/// @brief 1固定ステップ分の積分を実行する
	/// @param dt ステップ幅
	void integrateStep(float dt)
	{
		for (auto& [entityId, state] : m_bodies)
		{
			// 静的・キネマティックボディはスキップ
			if (state.mass <= 0.0f || state.isKinematic) continue;

			// 重力を適用
			state.velocity += m_config.gravity * dt;

			// 線形減衰
			state.velocity *= (1.0f - state.linearDamping * dt);

			// 位置を更新（オイラー積分）
			state.position += state.velocity * dt;

			// 角速度減衰
			state.angularVelocity *= (1.0f - state.angularDamping * dt);

			// 回転を更新
			state.rotation += state.angularVelocity * dt;
		}
	}

	PhysicsConfig m_config;   ///< 物理設定
	bool m_initialized{false}; ///< 初期化済みフラグ
	float m_accumulator{0.0f}; ///< 蓄積時間

	/// @brief エンティティID → 内部物理状態
	std::unordered_map<scene::EntityId, InternalBodyState> m_bodies;
};

} // namespace mitiru::physics
