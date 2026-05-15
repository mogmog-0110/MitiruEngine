#pragma once

/// @file PhysicsBridge.hpp
/// @brief sgc物理エンジンとMitiruの統合ブリッジ
/// @details sgc::physics::FixedTimestep、RigidBody2D、AABB衝突検出、
///          レイキャストをMitiruエンジンに統合する。

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/physics/AABB2DCollision.hpp>
#include <sgc/physics/FixedTimestep.hpp>
#include <sgc/physics/RayCast2D.hpp>
#include <sgc/physics/RigidBody2D.hpp>

namespace mitiru::bridge
{

/// @brief 物理ボディエントリ
/// @details 剛体ポインタとAABBハーフサイズを組み合わせて管理する。
struct PhysicsBodyEntry
{
	sgc::physics::RigidBody2Df* body = nullptr;  ///< 剛体へのポインタ（非所有）
	sgc::Vec2f halfSize{};                        ///< AABBの半分のサイズ
};

/// @brief 衝突イベント情報
struct CollisionEvent
{
	std::string labelA;                           ///< ボディAのラベル
	std::string labelB;                           ///< ボディBのラベル
	sgc::physics::CollisionResult2Df result;      ///< 衝突結果
};

/// @brief sgc物理統合ブリッジ
/// @details 固定タイムステップで剛体管理・重力適用・衝突検出/応答を実行する。
///
/// @code
/// mitiru::bridge::PhysicsBridge physics(1.0 / 60.0);
/// physics.setGravity({0.0f, 980.0f});
///
/// sgc::physics::RigidBody2Df playerBody;
/// playerBody.position = {100.0f, 100.0f};
/// playerBody.mass = 1.0f;
/// physics.addBody("player", &playerBody, {16.0f, 16.0f});
///
/// sgc::physics::RigidBody2Df groundBody;
/// groundBody.position = {400.0f, 500.0f};
/// groundBody.isStatic = true;
/// physics.addBody("ground", &groundBody, {400.0f, 16.0f});
///
/// // 毎フレーム
/// int steps = physics.step(dt);
/// @endcode
class PhysicsBridge
{
public:
	/// @brief コンストラクタ
	/// @param stepSize 固定ステップ幅（秒）。デフォルトは1/60
	explicit PhysicsBridge(double stepSize = 1.0 / 60.0)
		: m_stepper(stepSize)
	{
	}

	// ── ボディ管理 ──────────────────────────────────────────

	/// @brief 物理ボディを登録する
	/// @param label ボディラベル（一意識別子）
	/// @param body 剛体へのポインタ（呼び出し側が所有権を保持）
	/// @param halfSize AABBの半分のサイズ
	void addBody(const std::string& label, sgc::physics::RigidBody2Df* body,
		const sgc::Vec2f& halfSize)
	{
		if (!body) return;
		m_bodies[label] = PhysicsBodyEntry{body, halfSize};
	}

	/// @brief 物理ボディを削除する
	/// @param label ボディラベル
	void removeBody(const std::string& label)
	{
		m_bodies.erase(label);
	}

	/// @brief 物理ボディを取得する
	/// @param label ボディラベル
	/// @return ボディエントリへのポインタ（未登録時はnullptr）
	[[nodiscard]] const PhysicsBodyEntry* getBody(const std::string& label) const
	{
		const auto it = m_bodies.find(label);
		if (it == m_bodies.end()) return nullptr;
		return &it->second;
	}

	/// @brief 登録ボディ数を取得する
	/// @return ボディ数
	[[nodiscard]] std::size_t bodyCount() const noexcept
	{
		return m_bodies.size();
	}

	// ── 重力 ────────────────────────────────────────────────

	/// @brief 重力ベクトルを設定する
	/// @param gravity 重力加速度ベクトル（ピクセル/秒^2）
	void setGravity(const sgc::Vec2f& gravity) noexcept
	{
		m_gravity = gravity;
	}

	/// @brief 重力ベクトルを取得する
	/// @return 重力加速度ベクトル
	[[nodiscard]] const sgc::Vec2f& gravity() const noexcept
	{
		return m_gravity;
	}

	// ── 物理ステップ ────────────────────────────────────────

	/// @brief 固定タイムステップで物理更新を実行する
	/// @param dt フレームデルタタイム（秒）
	/// @return 実行されたステップ数
	///
	/// @details 各ステップで以下を実行する:
	///   1. 全動的ボディに重力を適用
	///   2. 全ボディを積分（位置・速度更新）
	///   3. 全ボディペアでAABB衝突検出
	///   4. 衝突応答（位置分離・速度反発）
	int step(double dt)
	{
		m_lastCollisions.clear();

		const int steps = m_stepper.update(dt, [this](double stepDt)
		{
			const auto fdt = static_cast<float>(stepDt);
			applyGravity(fdt);
			integrateAll(fdt);
			detectAndResolveCollisions();
		});

		m_totalSteps += steps;
		return steps;
	}

	/// @brief 外部コールバック付きで物理更新を実行する
	/// @tparam Callback void(double) で呼び出し可能な型
	/// @param dt フレームデルタタイム（秒）
	/// @param callback 各ステップで追加呼び出しされるコールバック
	/// @return 実行されたステップ数
	template <std::invocable<double> Callback>
	int step(double dt, Callback&& callback)
	{
		m_lastCollisions.clear();

		const int steps = m_stepper.update(dt, [this, &callback](double stepDt)
		{
			const auto fdt = static_cast<float>(stepDt);
			applyGravity(fdt);
			integrateAll(fdt);
			detectAndResolveCollisions();
			callback(stepDt);
		});

		m_totalSteps += steps;
		return steps;
	}

	/// @brief 衝突コールバックを設定する
	/// @param callback 衝突発生時に呼ばれるコールバック
	void setCollisionCallback(std::function<void(const CollisionEvent&)> callback)
	{
		m_collisionCallback = std::move(callback);
	}

	/// @brief 最後のステップで検出された衝突一覧を取得する
	/// @return 衝突イベントのベクタ
	[[nodiscard]] const std::vector<CollisionEvent>& lastCollisions() const noexcept
	{
		return m_lastCollisions;
	}

	// ── レイキャスト ────────────────────────────────────────

	/// @brief 全登録ボディに対してレイキャストを実行する
	/// @param origin レイの始点
	/// @param direction レイの方向ベクトル（正規化推奨）
	/// @param maxDistance 最大検出距離
	/// @return 最も近いヒット結果とラベル（ヒットなしの場合はnullopt）
	[[nodiscard]] std::optional<std::pair<std::string, sgc::physics::RayCastHit2Df>> rayCast(
		const sgc::Vec2f& origin, const sgc::Vec2f& direction,
		float maxDistance = 1e10f) const
	{
		std::optional<std::pair<std::string, sgc::physics::RayCastHit2Df>> closest;

		for (const auto& [label, entry] : m_bodies)
		{
			if (!entry.body) continue;

			const auto aabb = entry.body->bounds(entry.halfSize);
			const auto hit = sgc::physics::raycastAABB(origin, direction, aabb, maxDistance);

			if (hit.hit)
			{
				if (!closest.has_value() || hit.distance < closest->second.distance)
				{
					closest = {label, hit};
				}
			}
		}

		return closest;
	}

	// ── タイムステップ制御 ──────────────────────────────────

	/// @brief 蓄積時間をリセットする
	void resetAccumulator() noexcept
	{
		m_stepper.resetAccumulator();
	}

	/// @brief ステップ幅を取得する
	/// @return ステップ幅（秒）
	[[nodiscard]] double stepSize() const noexcept
	{
		return m_stepper.stepSize();
	}

	/// @brief ステップ幅を変更する
	/// @param stepSize 新しいステップ幅（秒）
	void setStepSize(double stepSize) noexcept
	{
		m_stepper.setStepSize(stepSize);
	}

	/// @brief 補間係数を取得する（描画の線形補間用）
	/// @return 0.0〜1.0の補間係数
	[[nodiscard]] double interpolationFactor() const noexcept
	{
		return m_stepper.interpolationFactor();
	}

	/// @brief 累計ステップ数を取得する
	/// @return 累計ステップ数
	[[nodiscard]] int totalSteps() const noexcept
	{
		return m_totalSteps;
	}

	/// @brief 最大ステップ数を設定する（スパイラル・オブ・デス防止）
	/// @param steps 最大ステップ数
	void setMaxSteps(int steps) noexcept
	{
		m_stepper.setMaxSteps(steps);
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief 物理状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"stepSize\":" + std::to_string(m_stepper.stepSize()) + ",";
		json += "\"accumulated\":" + std::to_string(m_stepper.accumulated()) + ",";
		json += "\"interpolationFactor\":" + std::to_string(m_stepper.interpolationFactor()) + ",";
		json += "\"totalSteps\":" + std::to_string(m_totalSteps) + ",";
		json += "\"maxSteps\":" + std::to_string(m_stepper.maxSteps()) + ",";
		json += "\"gravity\":{\"x\":" + std::to_string(m_gravity.x) +
			",\"y\":" + std::to_string(m_gravity.y) + "},";
		json += "\"bodies\":[";

		bool first = true;
		for (const auto& [label, entry] : m_bodies)
		{
			if (!entry.body) continue;
			if (!first) json += ",";

			json += "{\"label\":\"" + label + "\"";
			json += ",\"position\":{\"x\":" + std::to_string(entry.body->position.x) +
				",\"y\":" + std::to_string(entry.body->position.y) + "}";
			json += ",\"velocity\":{\"x\":" + std::to_string(entry.body->velocity.x) +
				",\"y\":" + std::to_string(entry.body->velocity.y) + "}";
			json += ",\"isStatic\":" + std::string(entry.body->isStatic ? "true" : "false");
			json += "}";
			first = false;
		}

		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief 全動的ボディに重力を適用する
	/// @param dt タイムステップ幅
	void applyGravity(float dt)
	{
		static_cast<void>(dt);
		for (auto& [label, entry] : m_bodies)
		{
			if (!entry.body || entry.body->isStatic) continue;
			entry.body->applyForce(m_gravity * entry.body->mass);
		}
	}

	/// @brief 全ボディを積分する
	/// @param dt タイムステップ幅
	void integrateAll(float dt)
	{
		for (auto& [label, entry] : m_bodies)
		{
			if (!entry.body) continue;
			entry.body->integrate(dt);
		}
	}

	/// @brief 全ボディペアでAABB衝突検出・応答を実行する
	void detectAndResolveCollisions()
	{
		/// ボディ一覧をベクタに変換する（ペア走査用）
		std::vector<std::pair<std::string, PhysicsBodyEntry*>> bodyList;
		bodyList.reserve(m_bodies.size());
		for (auto& [label, entry] : m_bodies)
		{
			if (entry.body)
			{
				bodyList.push_back({label, &entry});
			}
		}

		/// 全ペアを検査する
		for (std::size_t i = 0; i < bodyList.size(); ++i)
		{
			for (std::size_t j = i + 1; j < bodyList.size(); ++j)
			{
				auto* entryA = bodyList[i].second;
				auto* entryB = bodyList[j].second;

				const auto aabbA = entryA->body->bounds(entryA->halfSize);
				const auto aabbB = entryB->body->bounds(entryB->halfSize);

				const auto collision = sgc::physics::detectAABBCollision(aabbA, aabbB);

				if (collision.colliding)
				{
					/// 衝突応答
					sgc::physics::resolveRigidBodyCollision(
						*entryA->body, *entryB->body, collision);

					/// 衝突イベントを記録する
					CollisionEvent event{bodyList[i].first, bodyList[j].first, collision};
					m_lastCollisions.push_back(event);

					/// コールバック通知
					if (m_collisionCallback)
					{
						m_collisionCallback(event);
					}
				}
			}
		}
	}

	sgc::physics::FixedTimestep m_stepper;   ///< 固定タイムステップ
	int m_totalSteps = 0;                    ///< 累計ステップ数
	sgc::Vec2f m_gravity{};                  ///< 重力ベクトル

	/// @brief ラベル → 物理ボディエントリ
	std::unordered_map<std::string, PhysicsBodyEntry> m_bodies;

	/// @brief 衝突コールバック
	std::function<void(const CollisionEvent&)> m_collisionCallback;

	/// @brief 最後のステップで検出された衝突一覧
	std::vector<CollisionEvent> m_lastCollisions;
};

} // namespace mitiru::bridge
