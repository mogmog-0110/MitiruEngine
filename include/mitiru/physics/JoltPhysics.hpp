#pragma once

/// @file JoltPhysics.hpp
/// @brief Jolt Physics 3D物理エンジン統合ブリッジ
///
/// Jolt Physics (MIT License) をMitiruEngineの3D物理バックエンドとして統合する。
/// `MITIRU_HAS_JOLT` が定義されている場合はJolt APIを呼び出し、
/// 未定義の場合はNullスタブを提供してコンパイルを通す。
///
/// @code
/// mitiru::physics::JoltPhysicsConfig config;
/// config.gravity = {0.0f, -9.81f, 0.0f};
///
/// mitiru::physics::JoltPhysicsWorld world;
/// world.init(config);
///
/// auto box = world.createBoxShape({0.5f, 0.5f, 0.5f});
/// auto bodyId = world.createDynamicBody(box, {0, 10, 0}, {}, 1.0f);
///
/// world.update(1.0f / 60.0f);
/// auto pos = world.getPosition(bodyId);
/// world.shutdown();
/// @endcode
///
/// @note CMakeLists.txt で Jolt を有効にするには:
///   git submodule add https://github.com/jrouwe/JoltPhysics external/jolt
///   add_subdirectory(external/jolt/Build)
///   target_compile_definitions(mitiru INTERFACE MITIRU_HAS_JOLT=1)
///   target_link_libraries(mitiru INTERFACE Jolt)

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/math/Vec3.hpp"
#include "sgc/math/Quaternion.hpp"

#include <mitiru/debug/TracyZones.hpp>

#ifdef MITIRU_HAS_JOLT

// Jolt は include 前にこれらの define が必要
#ifndef JPH_DEBUG_RENDERER
#define JPH_DEBUG_RENDERER
#endif

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>

#endif // MITIRU_HAS_JOLT

namespace mitiru::physics
{

// ── 型定義 ──────────────────────────────────────────────────

/// @brief Joltボディ識別子
using JoltBodyId = uint32_t;

/// @brief 無効なJoltボディID
inline constexpr JoltBodyId INVALID_JOLT_BODY_ID = 0xFFFFFFFF;

// ── 設定 ──────────────────────────────────────────────────

/// @brief Jolt Physics初期化設定
struct JoltPhysicsConfig
{
	uint32_t maxBodies{65536};               ///< 最大ボディ数
	uint32_t maxBodyPairs{65536};            ///< 最大ボディペア数
	uint32_t maxContactConstraints{10240};   ///< 最大コンタクト拘束数
	uint32_t numBodyMutexes{0};              ///< ボディミューテックス数（0=自動）
	sgc::Vec3f gravity{0.0f, -9.81f, 0.0f}; ///< 重力加速度（m/s^2）
	float fixedTimeStep{1.0f / 60.0f};       ///< 固定タイムステップ（秒）
	int maxSubSteps{4};                      ///< 最大サブステップ数
};

// ── コンタクトイベント ──────────────────────────────────────

/// @brief 衝突コンタクトイベント
struct JoltContactEvent
{
	JoltBodyId body1{INVALID_JOLT_BODY_ID};  ///< ボディ1のID
	JoltBodyId body2{INVALID_JOLT_BODY_ID};  ///< ボディ2のID
	sgc::Vec3f contactPoint{};               ///< 接触点（ワールド座標）
	sgc::Vec3f normal{};                     ///< 接触法線（body1→body2方向）
	float penetration{0.0f};                 ///< 貫通深度
};

/// @brief コンタクトコールバック型
using JoltContactCallback = std::function<void(const JoltContactEvent&)>;

// ── レイキャスト結果 ────────────────────────────────────────

/// @brief レイキャストヒット結果
struct JoltRaycastResult
{
	JoltBodyId bodyId{INVALID_JOLT_BODY_ID}; ///< ヒットしたボディ
	sgc::Vec3f hitPoint{};                   ///< ヒット座標（ワールド空間）
	sgc::Vec3f hitNormal{};                  ///< ヒット法線
	float fraction{0.0f};                    ///< レイ上のヒット位置（0〜1）
};

// ── シェイプハンドル ────────────────────────────────────────

/// @brief シェイプ識別子（内部管理用）
using JoltShapeId = uint32_t;

/// @brief 無効なシェイプID
inline constexpr JoltShapeId INVALID_JOLT_SHAPE_ID = 0xFFFFFFFF;

// ── デバッグ描画ライン ──────────────────────────────────────

/// @brief デバッグ描画用ライン
struct JoltDebugLine
{
	sgc::Vec3f from{};    ///< 始点
	sgc::Vec3f to{};      ///< 終点
	uint32_t color{0xFFFFFFFF}; ///< RGBA色
};

// ══════════════════════════════════════════════════════════════
// Jolt が有効な場合の実装
// ══════════════════════════════════════════════════════════════

#ifdef MITIRU_HAS_JOLT

namespace detail
{

/// @brief Jolt用オブジェクトレイヤー定義
namespace Layers
{
	static constexpr JPH::ObjectLayer NON_MOVING = 0;
	static constexpr JPH::ObjectLayer MOVING = 1;
	static constexpr JPH::uint NUM_LAYERS = 2;
} // namespace Layers

/// @brief Jolt用ブロードフェーズレイヤー定義
namespace BroadPhaseLayers
{
	static constexpr JPH::BroadPhaseLayer NON_MOVING{0};
	static constexpr JPH::BroadPhaseLayer MOVING{1};
	static constexpr JPH::uint NUM_LAYERS = 2;
} // namespace BroadPhaseLayers

/// @brief オブジェクトレイヤー→ブロードフェーズレイヤーのマッピング
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
	{
		switch (inObject1)
		{
		case Layers::NON_MOVING:
			return inObject2 == Layers::MOVING;
		case Layers::MOVING:
			return true;
		default:
			return false;
		}
	}
};

/// @brief ブロードフェーズレイヤーインターフェース実装
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		m_objectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		m_objectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}

	JPH::uint GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::NUM_LAYERS;
	}

	JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
	{
		return m_objectToBroadPhase[inLayer];
	}

private:
	JPH::BroadPhaseLayer m_objectToBroadPhase[Layers::NUM_LAYERS];
};

/// @brief オブジェクト vs ブロードフェーズレイヤーのフィルター
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:
			return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING:
			return true;
		default:
			return false;
		}
	}
};

/// @brief コンタクトリスナー実装
class ContactListenerImpl : public JPH::ContactListener
{
public:
	void SetOnContactAdded(JoltContactCallback callback) { m_onAdded = std::move(callback); }
	void SetOnContactRemoved(JoltContactCallback callback) { m_onRemoved = std::move(callback); }

	JPH::ValidateResult OnContactValidate(
		const JPH::Body& /*inBody1*/,
		const JPH::Body& /*inBody2*/,
		JPH::RVec3Arg /*inBaseOffset*/,
		const JPH::CollideShapeResult& /*inCollisionResult*/) override
	{
		return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	void OnContactAdded(
		const JPH::Body& inBody1,
		const JPH::Body& inBody2,
		const JPH::ContactManifold& inManifold,
		JPH::ContactSettings& /*ioSettings*/) override
	{
		if (!m_onAdded) return;

		JoltContactEvent event;
		event.body1 = inBody1.GetID().GetIndexAndSequenceNumber();
		event.body2 = inBody2.GetID().GetIndexAndSequenceNumber();
		if (inManifold.mRelativeContactPointsOn1.size() > 0)
		{
			event.contactPoint = {
				static_cast<float>(inManifold.mBaseOffset.GetX() + inManifold.mRelativeContactPointsOn1[0].GetX()),
				static_cast<float>(inManifold.mBaseOffset.GetY() + inManifold.mRelativeContactPointsOn1[0].GetY()),
				static_cast<float>(inManifold.mBaseOffset.GetZ() + inManifold.mRelativeContactPointsOn1[0].GetZ())
			};
		}
		event.normal = {
			inManifold.mWorldSpaceNormal.GetX(),
			inManifold.mWorldSpaceNormal.GetY(),
			inManifold.mWorldSpaceNormal.GetZ()
		};
		event.penetration = inManifold.mPenetrationDepth;

		m_onAdded(event);
	}

	void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
	{
		if (!m_onRemoved) return;

		JoltContactEvent event;
		event.body1 = inSubShapePair.GetBody1ID().GetIndexAndSequenceNumber();
		event.body2 = inSubShapePair.GetBody2ID().GetIndexAndSequenceNumber();

		m_onRemoved(event);
	}

private:
	JoltContactCallback m_onAdded;
	JoltContactCallback m_onRemoved;
};

} // namespace detail

/// @brief sgc::Vec3f → JPH::Vec3 変換
[[nodiscard]] inline JPH::Vec3 toJolt(const sgc::Vec3f& v) noexcept
{
	return JPH::Vec3(v.x, v.y, v.z);
}

/// @brief JPH::Vec3 → sgc::Vec3f 変換
[[nodiscard]] inline sgc::Vec3f fromJolt(const JPH::Vec3& v) noexcept
{
	return {v.GetX(), v.GetY(), v.GetZ()};
}

/// @brief sgc::Quaternionf → JPH::Quat 変換
[[nodiscard]] inline JPH::Quat toJoltQuat(const sgc::Quaternionf& q) noexcept
{
	return JPH::Quat(q.x, q.y, q.z, q.w);
}

/// @brief JPH::Quat → sgc::Quaternionf 変換
[[nodiscard]] inline sgc::Quaternionf fromJoltQuat(const JPH::Quat& q) noexcept
{
	return sgc::Quaternionf{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

/// @brief Jolt Physics 3D物理ワールド
///
/// Jolt PhysicsのPhysicsSystemをラップし、MitiruEngineの型で操作できる
/// 統合レイヤーを提供する。固定タイムステップアキュムレータ方式で更新する。
class JoltPhysicsWorld
{
public:
	JoltPhysicsWorld() = default;
	~JoltPhysicsWorld() { shutdown(); }

	// コピー禁止
	JoltPhysicsWorld(const JoltPhysicsWorld&) = delete;
	JoltPhysicsWorld& operator=(const JoltPhysicsWorld&) = delete;

	// ムーブ禁止 (Jolt のグローバル state と内部 pointer により move は危険)
	JoltPhysicsWorld(JoltPhysicsWorld&&) = delete;
	JoltPhysicsWorld& operator=(JoltPhysicsWorld&&) = delete;

	// ── 初期化・終了 ────────────────────────────────────────

	/// @brief Jolt Physicsを初期化する
	/// @param config 初期化設定
	void init(const JoltPhysicsConfig& config = {})
	{
		if (m_initialized) return;
		m_config = config;

		// Joltタイプ登録（グローバル、参照カウント管理）
		if (s_joltRefCount.fetch_add(1, std::memory_order_acq_rel) == 0)
		{
			JPH::RegisterDefaultAllocator();
			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();
		}

		// テンポラリアロケータ（10MB）
		m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

		// ジョブシステム（最大利用可能スレッド数、ただし最低1）
		m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
			JPH::cMaxPhysicsJobs,
			JPH::cMaxPhysicsBarriers,
			std::max(1u, std::thread::hardware_concurrency() - 1));

		// レイヤーインターフェース
		m_bpLayerInterface = std::make_unique<detail::BPLayerInterfaceImpl>();
		m_objectVsBpFilter = std::make_unique<detail::ObjectVsBroadPhaseLayerFilterImpl>();
		m_objectLayerPairFilter = std::make_unique<detail::ObjectLayerPairFilterImpl>();

		// コンタクトリスナー
		m_contactListener = std::make_unique<detail::ContactListenerImpl>();

		// 物理システム
		m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
		m_physicsSystem->Init(
			config.maxBodies,
			config.numBodyMutexes,
			config.maxBodyPairs,
			config.maxContactConstraints,
			*m_bpLayerInterface,
			*m_objectVsBpFilter,
			*m_objectLayerPairFilter);

		m_physicsSystem->SetGravity(toJolt(config.gravity));
		m_physicsSystem->SetContactListener(m_contactListener.get());

		m_initialized = true;
	}

	/// @brief Jolt Physicsをシャットダウンする
	void shutdown()
	{
		if (!m_initialized) return;

		m_shapes.clear();
		m_physicsSystem.reset();
		m_contactListener.reset();
		m_objectLayerPairFilter.reset();
		m_objectVsBpFilter.reset();
		m_bpLayerInterface.reset();
		m_jobSystem.reset();
		m_tempAllocator.reset();

		if (s_joltRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			JPH::UnregisterTypes();
			if (JPH::Factory::sInstance)
			{
				delete JPH::Factory::sInstance;
				JPH::Factory::sInstance = nullptr;
			}
		}

		m_accumulator = 0.0f;
		m_initialized = false;
	}

	/// @brief 初期化済みかどうかを返す
	[[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

	/// @brief 現在の設定を返す
	[[nodiscard]] const JoltPhysicsConfig& config() const noexcept { return m_config; }

	// ── シミュレーション更新 ────────────────────────────────

	/// @brief 物理シミュレーションを更新する（固定タイムステップアキュムレータ方式）
	/// @param dt フレームデルタタイム（秒）
	void update(float dt)
	{
		MITIRU_ZONE_NAMED("Physics::Jolt::Step");
		if (!m_initialized) return;

		m_accumulator += dt;

		int steps = 0;
		while (m_accumulator >= m_config.fixedTimeStep && steps < m_config.maxSubSteps)
		{
			m_physicsSystem->Update(
				m_config.fixedTimeStep,
				1,                    // collision step 数
				m_tempAllocator.get(),
				m_jobSystem.get());

			m_accumulator -= m_config.fixedTimeStep;
			++steps;
		}
	}

	// ── シェイプ作成 ────────────────────────────────────────

	/// @brief ボックスシェイプを作成する
	/// @param halfExtents 半径サイズ
	/// @return シェイプID
	[[nodiscard]] JoltShapeId createBoxShape(const sgc::Vec3f& halfExtents)
	{
		auto shape = new JPH::BoxShape(toJolt(halfExtents));
		return registerShape(shape);
	}

	/// @brief 球シェイプを作成する
	/// @param radius 半径
	/// @return シェイプID
	[[nodiscard]] JoltShapeId createSphereShape(float radius)
	{
		auto shape = new JPH::SphereShape(radius);
		return registerShape(shape);
	}

	/// @brief カプセルシェイプを作成する
	/// @param halfHeight 半高さ（半球間の距離の半分）
	/// @param radius 半径
	/// @return シェイプID
	[[nodiscard]] JoltShapeId createCapsuleShape(float halfHeight, float radius)
	{
		auto shape = new JPH::CapsuleShape(halfHeight, radius);
		return registerShape(shape);
	}

	/// @brief メッシュシェイプを作成する（静的レベルジオメトリ用）
	/// @param vertices 頂点配列
	/// @param indices インデックス配列（3の倍数、三角形リスト）
	/// @return シェイプID
	[[nodiscard]] JoltShapeId createMeshShape(
		const std::vector<sgc::Vec3f>& vertices,
		const std::vector<uint32_t>& indices)
	{
		JPH::TriangleList triangles;
		triangles.reserve(indices.size() / 3);

		for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size())
				return INVALID_JOLT_SHAPE_ID;

			const auto& v0 = vertices[indices[i]];
			const auto& v1 = vertices[indices[i + 1]];
			const auto& v2 = vertices[indices[i + 2]];

			triangles.push_back(JPH::Triangle(
				JPH::Float3(v0.x, v0.y, v0.z),
				JPH::Float3(v1.x, v1.y, v1.z),
				JPH::Float3(v2.x, v2.y, v2.z)));
		}

		auto settings = new JPH::MeshShapeSettings(triangles);
		auto result = settings->Create();
		delete settings;

		if (result.HasError()) return INVALID_JOLT_SHAPE_ID;

		return registerShape(const_cast<JPH::Shape*>(result.Get().GetPtr()));
	}

	/// @brief 複合シェイプを作成する（複数シェイプの組み合わせ）
	/// @param shapeIds 組み合わせるシェイプIDの配列
	/// @return シェイプID
	[[nodiscard]] JoltShapeId createCompoundShape(const std::vector<JoltShapeId>& shapeIds)
	{
		JPH::StaticCompoundShapeSettings settings;

		for (const auto id : shapeIds)
		{
			auto it = m_shapes.find(id);
			if (it == m_shapes.end()) continue;

			settings.AddShape(
				JPH::Vec3::sZero(),
				JPH::Quat::sIdentity(),
				it->second);
		}

		auto result = settings.Create();
		if (result.HasError()) return INVALID_JOLT_SHAPE_ID;

		return registerShape(const_cast<JPH::Shape*>(result.Get().GetPtr()));
	}

	// ── ボディ作成・削除 ────────────────────────────────────

	/// @brief 静的ボディを作成する
	/// @param shapeId シェイプID
	/// @param position 初期位置
	/// @param rotation 初期回転
	/// @return ボディID
	[[nodiscard]] JoltBodyId createStaticBody(
		JoltShapeId shapeId,
		const sgc::Vec3f& position,
		const sgc::Quaternionf& rotation = {})
	{
		auto shapeIt = m_shapes.find(shapeId);
		if (shapeIt == m_shapes.end()) return INVALID_JOLT_BODY_ID;

		JPH::BodyCreationSettings settings(
			shapeIt->second,
			toJolt(position).IsNaN() ? JPH::RVec3::sZero() : JPH::RVec3(toJolt(position)),
			toJoltQuat(rotation),
			JPH::EMotionType::Static,
			detail::Layers::NON_MOVING);

		return createBodyFromSettings(settings);
	}

	/// @brief 動的ボディを作成する
	/// @param shapeId シェイプID
	/// @param position 初期位置
	/// @param rotation 初期回転
	/// @param mass 質量（kg）
	/// @return ボディID
	[[nodiscard]] JoltBodyId createDynamicBody(
		JoltShapeId shapeId,
		const sgc::Vec3f& position,
		const sgc::Quaternionf& rotation = {},
		float mass = 1.0f)
	{
		auto shapeIt = m_shapes.find(shapeId);
		if (shapeIt == m_shapes.end()) return INVALID_JOLT_BODY_ID;

		JPH::BodyCreationSettings settings(
			shapeIt->second,
			toJolt(position).IsNaN() ? JPH::RVec3::sZero() : JPH::RVec3(toJolt(position)),
			toJoltQuat(rotation),
			JPH::EMotionType::Dynamic,
			detail::Layers::MOVING);

		settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
		settings.mMassPropertiesOverride.mMass = mass;

		return createBodyFromSettings(settings);
	}

	/// @brief キネマティックボディを作成する
	/// @param shapeId シェイプID
	/// @param position 初期位置
	/// @param rotation 初期回転
	/// @return ボディID
	[[nodiscard]] JoltBodyId createKinematicBody(
		JoltShapeId shapeId,
		const sgc::Vec3f& position,
		const sgc::Quaternionf& rotation = {})
	{
		auto shapeIt = m_shapes.find(shapeId);
		if (shapeIt == m_shapes.end()) return INVALID_JOLT_BODY_ID;

		JPH::BodyCreationSettings settings(
			shapeIt->second,
			toJolt(position).IsNaN() ? JPH::RVec3::sZero() : JPH::RVec3(toJolt(position)),
			toJoltQuat(rotation),
			JPH::EMotionType::Kinematic,
			detail::Layers::MOVING);

		return createBodyFromSettings(settings);
	}

	/// @brief ボディを削除する
	/// @param bodyId ボディID
	void removeBody(JoltBodyId bodyId)
	{
		if (!m_initialized) return;

		auto& bodyInterface = m_physicsSystem->GetBodyInterface();
		const JPH::BodyID jphId(bodyId);

		if (bodyInterface.IsAdded(jphId))
		{
			bodyInterface.RemoveBody(jphId);
		}
		bodyInterface.DestroyBody(jphId);
	}

	// ── 位置・回転 ──────────────────────────────────────────

	/// @brief ボディの位置を設定する
	/// @param bodyId ボディID
	/// @param position 新しい位置
	void setPosition(JoltBodyId bodyId, const sgc::Vec3f& position)
	{
		if (!m_initialized) return;
		auto& bi = m_physicsSystem->GetBodyInterface();
		bi.SetPosition(JPH::BodyID(bodyId), JPH::RVec3(toJolt(position)), JPH::EActivation::Activate);
	}

	/// @brief ボディの位置を取得する
	/// @param bodyId ボディID
	/// @return 位置ベクトル
	[[nodiscard]] sgc::Vec3f getPosition(JoltBodyId bodyId) const
	{
		if (!m_initialized) return {};
		const auto& bi = m_physicsSystem->GetBodyInterface();
		const auto pos = bi.GetPosition(JPH::BodyID(bodyId));
		return {static_cast<float>(pos.GetX()),
		        static_cast<float>(pos.GetY()),
		        static_cast<float>(pos.GetZ())};
	}

	/// @brief ボディの回転を設定する
	/// @param bodyId ボディID
	/// @param rotation 新しい回転
	void setRotation(JoltBodyId bodyId, const sgc::Quaternionf& rotation)
	{
		if (!m_initialized) return;
		auto& bi = m_physicsSystem->GetBodyInterface();
		bi.SetRotation(JPH::BodyID(bodyId), toJoltQuat(rotation), JPH::EActivation::Activate);
	}

	/// @brief ボディの回転を取得する
	/// @param bodyId ボディID
	/// @return クォータニオン
	[[nodiscard]] sgc::Quaternionf getRotation(JoltBodyId bodyId) const
	{
		if (!m_initialized) return {};
		const auto& bi = m_physicsSystem->GetBodyInterface();
		return fromJoltQuat(bi.GetRotation(JPH::BodyID(bodyId)));
	}

	// ── 速度 ────────────────────────────────────────────────

	/// @brief ボディの線形速度を設定する
	/// @param bodyId ボディID
	/// @param velocity 速度ベクトル
	void setVelocity(JoltBodyId bodyId, const sgc::Vec3f& velocity)
	{
		if (!m_initialized) return;
		auto& bi = m_physicsSystem->GetBodyInterface();
		bi.SetLinearVelocity(JPH::BodyID(bodyId), toJolt(velocity));
	}

	/// @brief ボディの線形速度を取得する
	/// @param bodyId ボディID
	/// @return 速度ベクトル
	[[nodiscard]] sgc::Vec3f getVelocity(JoltBodyId bodyId) const
	{
		if (!m_initialized) return {};
		const auto& bi = m_physicsSystem->GetBodyInterface();
		return fromJolt(bi.GetLinearVelocity(JPH::BodyID(bodyId)));
	}

	// ── 力・インパルス ──────────────────────────────────────

	/// @brief ボディに力を加える（次のステップで適用される）
	/// @param bodyId ボディID
	/// @param force 力ベクトル（N）
	void applyForce(JoltBodyId bodyId, const sgc::Vec3f& force)
	{
		if (!m_initialized) return;
		auto& bi = m_physicsSystem->GetBodyInterface();
		bi.AddForce(JPH::BodyID(bodyId), toJolt(force));
	}

	/// @brief ボディにインパルスを適用する（即座に速度変化）
	/// @param bodyId ボディID
	/// @param impulse インパルスベクトル（N*s）
	void applyImpulse(JoltBodyId bodyId, const sgc::Vec3f& impulse)
	{
		if (!m_initialized) return;
		auto& bi = m_physicsSystem->GetBodyInterface();
		bi.AddImpulse(JPH::BodyID(bodyId), toJolt(impulse));
	}

	// ── クエリ ──────────────────────────────────────────────

	/// @brief レイキャストを実行する
	/// @param origin レイの始点
	/// @param direction レイの方向（正規化推奨）
	/// @param maxDist 最大検出距離
	/// @return 最も近いヒット結果（ヒットなしの場合はnullopt）
	[[nodiscard]] std::optional<JoltRaycastResult> raycast(
		const sgc::Vec3f& origin,
		const sgc::Vec3f& direction,
		float maxDist = 1000.0f) const
	{
		if (!m_initialized) return std::nullopt;

		const JPH::Vec3 dir = toJolt(direction).Normalized();
		JPH::RRayCast ray(JPH::RVec3(toJolt(origin)), dir * maxDist);

		JPH::RayCastResult hit;
		const auto& query = m_physicsSystem->GetNarrowPhaseQuery();

		if (query.CastRay(ray, hit))
		{
			JoltRaycastResult result;
			result.bodyId = hit.mBodyID.GetIndexAndSequenceNumber();
			result.fraction = hit.mFraction;
			result.hitPoint = fromJolt(JPH::Vec3(ray.GetPointOnRay(hit.mFraction)));

			// 法線取得
			JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), hit.mBodyID);
			if (lock.Succeeded())
			{
				const JPH::Body& body = lock.GetBody();
				result.hitNormal = fromJolt(
					body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
			}

			return result;
		}

		return std::nullopt;
	}

	/// @brief 球オーバーラップクエリを実行する
	/// @param center 球の中心
	/// @param radius 球の半径
	/// @return オーバーラップしたボディIDの配列
	[[nodiscard]] std::vector<JoltBodyId> overlapSphere(
		const sgc::Vec3f& center,
		float radius) const
	{
		if (!m_initialized) return {};

		std::vector<JoltBodyId> results;

		// アクティブボディのみを取得して距離判定
		const auto& bi = m_physicsSystem->GetBodyInterface();
		JPH::BodyIDVector activeIds;
		m_physicsSystem->GetActiveBodies(JPH::EBodyType::RigidBody, activeIds);

		for (const auto& bodyId : activeIds)
		{
			const auto bodyPos = bi.GetPosition(bodyId);
			const float dx = static_cast<float>(bodyPos.GetX()) - center.x;
			const float dy = static_cast<float>(bodyPos.GetY()) - center.y;
			const float dz = static_cast<float>(bodyPos.GetZ()) - center.z;
			const float distSq = dx * dx + dy * dy + dz * dz;

			if (distSq <= radius * radius)
			{
				results.push_back(bodyId.GetIndexAndSequenceNumber());
			}
		}

		return results;
	}

	// ── コールバック ────────────────────────────────────────

	/// @brief コンタクト追加コールバックを登録する
	/// @param callback コールバック関数
	void onContactAdded(JoltContactCallback callback)
	{
		if (m_contactListener)
		{
			m_contactListener->SetOnContactAdded(std::move(callback));
		}
	}

	/// @brief コンタクト削除コールバックを登録する
	/// @param callback コールバック関数
	void onContactRemoved(JoltContactCallback callback)
	{
		if (m_contactListener)
		{
			m_contactListener->SetOnContactRemoved(std::move(callback));
		}
	}

	// ── デバッグ情報 ────────────────────────────────────────

	/// @brief 全ボディ数を返す
	[[nodiscard]] uint32_t getBodyCount() const noexcept
	{
		if (!m_initialized) return 0;
		return m_physicsSystem->GetNumBodies();
	}

	/// @brief アクティブボディ数を返す
	[[nodiscard]] uint32_t getActiveBodyCount() const noexcept
	{
		if (!m_initialized) return 0;
		return m_physicsSystem->GetNumActiveBodies(JPH::EBodyType::RigidBody);
	}

	/// @brief デバッグ描画用のワイヤーフレームラインを生成する
	/// @return デバッグラインの配列
	///
	/// @details 全アクティブボディのAABBをワイヤーフレームとして返す。
	///          描画はレンダラー側の責務（PhysicsDebugRenderer3Dと同様のパターン）。
	[[nodiscard]] std::vector<JoltDebugLine> gatherDebugLines() const
	{
		if (!m_initialized) return {};

		std::vector<JoltDebugLine> lines;
		const auto& bi = m_physicsSystem->GetBodyInterface();

		// アクティブボディのAABBをワイヤーフレームで描画
		JPH::BodyIDVector bodyIds;
		m_physicsSystem->GetBodies(bodyIds);

		for (const auto& id : bodyIds)
		{
			if (!bi.IsActive(id)) continue;

			const auto aabb = bi.GetWorldSpaceBounds(id);
			const auto min = fromJolt(JPH::Vec3(aabb.mMin));
			const auto max = fromJolt(JPH::Vec3(aabb.mMax));

			const uint32_t color = 0xFF00FF00; // 緑

			// 底面
			lines.push_back({{min.x, min.y, min.z}, {max.x, min.y, min.z}, color});
			lines.push_back({{max.x, min.y, min.z}, {max.x, min.y, max.z}, color});
			lines.push_back({{max.x, min.y, max.z}, {min.x, min.y, max.z}, color});
			lines.push_back({{min.x, min.y, max.z}, {min.x, min.y, min.z}, color});

			// 上面
			lines.push_back({{min.x, max.y, min.z}, {max.x, max.y, min.z}, color});
			lines.push_back({{max.x, max.y, min.z}, {max.x, max.y, max.z}, color});
			lines.push_back({{max.x, max.y, max.z}, {min.x, max.y, max.z}, color});
			lines.push_back({{min.x, max.y, max.z}, {min.x, max.y, min.z}, color});

			// 柱
			lines.push_back({{min.x, min.y, min.z}, {min.x, max.y, min.z}, color});
			lines.push_back({{max.x, min.y, min.z}, {max.x, max.y, min.z}, color});
			lines.push_back({{max.x, min.y, max.z}, {max.x, max.y, max.z}, color});
			lines.push_back({{min.x, min.y, max.z}, {min.x, max.y, max.z}, color});
		}

		return lines;
	}

	/// @brief 内部のJolt PhysicsSystemへのアクセス（上級者向け）
	/// @return PhysicsSystemへのポインタ（未初期化時はnullptr）
	[[nodiscard]] JPH::PhysicsSystem* rawPhysicsSystem() noexcept
	{
		return m_physicsSystem.get();
	}

	/// @brief 内部のJolt PhysicsSystemへのアクセス（const版）
	[[nodiscard]] const JPH::PhysicsSystem* rawPhysicsSystem() const noexcept
	{
		return m_physicsSystem.get();
	}

private:
	/// @brief シェイプを内部マップに登録する
	/// @param shape Joltシェイプ（参照カウント管理される）
	/// @return シェイプID
	JoltShapeId registerShape(const JPH::Shape* shape)
	{
		const JoltShapeId id = m_nextShapeId++;
		m_shapes[id] = JPH::RefConst<JPH::Shape>(shape);
		return id;
	}

	/// @brief BodyCreationSettingsからボディを作成する
	/// @param settings 作成設定
	/// @return ボディID
	JoltBodyId createBodyFromSettings(const JPH::BodyCreationSettings& settings)
	{
		if (!m_initialized) return INVALID_JOLT_BODY_ID;

		auto& bi = m_physicsSystem->GetBodyInterface();
		const JPH::Body* body = bi.CreateBody(settings);
		if (!body) return INVALID_JOLT_BODY_ID;

		const JPH::BodyID id = body->GetID();
		bi.AddBody(id, JPH::EActivation::Activate);

		return id.GetIndexAndSequenceNumber();
	}

	JoltPhysicsConfig m_config;

	std::unique_ptr<JPH::TempAllocatorImpl> m_tempAllocator;
	std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
	std::unique_ptr<detail::BPLayerInterfaceImpl> m_bpLayerInterface;
	std::unique_ptr<detail::ObjectVsBroadPhaseLayerFilterImpl> m_objectVsBpFilter;
	std::unique_ptr<detail::ObjectLayerPairFilterImpl> m_objectLayerPairFilter;
	std::unique_ptr<detail::ContactListenerImpl> m_contactListener;
	std::unique_ptr<JPH::PhysicsSystem> m_physicsSystem;

	std::unordered_map<JoltShapeId, JPH::RefConst<JPH::Shape>> m_shapes;
	JoltShapeId m_nextShapeId{1};

	float m_accumulator{0.0f};
	bool m_initialized{false};

	/// @brief Joltグローバル初期化の参照カウント
	static inline std::atomic<int> s_joltRefCount{0};
};

#else // !MITIRU_HAS_JOLT

// ══════════════════════════════════════════════════════════════
// Jolt 未定義時のNullスタブ
// ══════════════════════════════════════════════════════════════

/// @brief Jolt Physics未導入時のNullスタブ
///
/// `MITIRU_HAS_JOLT` が未定義の場合に提供される。
/// 全メソッドがコンパイルを通すが何もしない。
class JoltPhysicsWorld
{
public:
	JoltPhysicsWorld() = default;
	~JoltPhysicsWorld() = default;

	JoltPhysicsWorld(const JoltPhysicsWorld&) = delete;
	JoltPhysicsWorld& operator=(const JoltPhysicsWorld&) = delete;
	JoltPhysicsWorld(JoltPhysicsWorld&&) = delete;
	JoltPhysicsWorld& operator=(JoltPhysicsWorld&&) = delete;

	// ── 初期化・終了 ────────────────────────────────────────

	void init(const JoltPhysicsConfig& /*config*/ = {}) {}
	void shutdown() {}
	[[nodiscard]] bool isInitialized() const noexcept { return false; }
	[[nodiscard]] const JoltPhysicsConfig& config() const noexcept { return m_config; }

	// ── シミュレーション更新 ────────────────────────────────

	void update(float /*dt*/) {}

	// ── シェイプ作成 ────────────────────────────────────────

	[[nodiscard]] JoltShapeId createBoxShape(const sgc::Vec3f& /*halfExtents*/)
	{ return INVALID_JOLT_SHAPE_ID; }

	[[nodiscard]] JoltShapeId createSphereShape(float /*radius*/)
	{ return INVALID_JOLT_SHAPE_ID; }

	[[nodiscard]] JoltShapeId createCapsuleShape(float /*halfHeight*/, float /*radius*/)
	{ return INVALID_JOLT_SHAPE_ID; }

	[[nodiscard]] JoltShapeId createMeshShape(
		const std::vector<sgc::Vec3f>& /*vertices*/,
		const std::vector<uint32_t>& /*indices*/)
	{ return INVALID_JOLT_SHAPE_ID; }

	[[nodiscard]] JoltShapeId createCompoundShape(const std::vector<JoltShapeId>& /*shapeIds*/)
	{ return INVALID_JOLT_SHAPE_ID; }

	// ── ボディ作成・削除 ────────────────────────────────────

	[[nodiscard]] JoltBodyId createStaticBody(
		JoltShapeId /*shapeId*/,
		const sgc::Vec3f& /*position*/,
		const sgc::Quaternionf& /*rotation*/ = {})
	{ return INVALID_JOLT_BODY_ID; }

	[[nodiscard]] JoltBodyId createDynamicBody(
		JoltShapeId /*shapeId*/,
		const sgc::Vec3f& /*position*/,
		const sgc::Quaternionf& /*rotation*/ = {},
		float /*mass*/ = 1.0f)
	{ return INVALID_JOLT_BODY_ID; }

	[[nodiscard]] JoltBodyId createKinematicBody(
		JoltShapeId /*shapeId*/,
		const sgc::Vec3f& /*position*/,
		const sgc::Quaternionf& /*rotation*/ = {})
	{ return INVALID_JOLT_BODY_ID; }

	void removeBody(JoltBodyId /*bodyId*/) {}

	// ── 位置・回転 ──────────────────────────────────────────

	void setPosition(JoltBodyId /*bodyId*/, const sgc::Vec3f& /*position*/) {}
	[[nodiscard]] sgc::Vec3f getPosition(JoltBodyId /*bodyId*/) const { return {}; }
	void setRotation(JoltBodyId /*bodyId*/, const sgc::Quaternionf& /*rotation*/) {}
	[[nodiscard]] sgc::Quaternionf getRotation(JoltBodyId /*bodyId*/) const { return {}; }

	// ── 速度 ────────────────────────────────────────────────

	void setVelocity(JoltBodyId /*bodyId*/, const sgc::Vec3f& /*velocity*/) {}
	[[nodiscard]] sgc::Vec3f getVelocity(JoltBodyId /*bodyId*/) const { return {}; }

	// ── 力・インパルス ──────────────────────────────────────

	void applyForce(JoltBodyId /*bodyId*/, const sgc::Vec3f& /*force*/) {}
	void applyImpulse(JoltBodyId /*bodyId*/, const sgc::Vec3f& /*impulse*/) {}

	// ── クエリ ──────────────────────────────────────────────

	[[nodiscard]] std::optional<JoltRaycastResult> raycast(
		const sgc::Vec3f& /*origin*/,
		const sgc::Vec3f& /*direction*/,
		float /*maxDist*/ = 1000.0f) const
	{ return std::nullopt; }

	[[nodiscard]] std::vector<JoltBodyId> overlapSphere(
		const sgc::Vec3f& /*center*/,
		float /*radius*/) const
	{ return {}; }

	// ── コールバック ────────────────────────────────────────

	void onContactAdded(JoltContactCallback /*callback*/) {}
	void onContactRemoved(JoltContactCallback /*callback*/) {}

	// ── デバッグ情報 ────────────────────────────────────────

	[[nodiscard]] uint32_t getBodyCount() const noexcept { return 0; }
	[[nodiscard]] uint32_t getActiveBodyCount() const noexcept { return 0; }
	[[nodiscard]] std::vector<JoltDebugLine> gatherDebugLines() const { return {}; }

private:
	JoltPhysicsConfig m_config;
};

#endif // MITIRU_HAS_JOLT

} // namespace mitiru::physics
