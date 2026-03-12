#pragma once

/// @file AsteroidGame.hpp
/// @brief アステロイドスタイルのサンプルゲーム
///
/// MitiruEngineのGameWorld・SystemRunner・コンポーネントを活用した
/// 完全にテスト可能なアステロイドゲーム実装。
/// 描画に依存せず、update()呼び出しのみでゲームが進行する。
///
/// @code
/// mitiru::sample::AsteroidGame game;
/// game.init(800.0f, 600.0f);
/// mitiru::sample::InputState input{};
/// input.thrust = true;
/// game.update(1.0f / 60.0f, input);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "mitiru/scene/GameWorld.hpp"
#include "mitiru/scene/SystemRunner.hpp"
#include "sgc/math/Vec3.hpp"

namespace mitiru::sample
{

// ── 定数 ────────────────────────────────────────────────

/// @brief 初期アステロイド数
inline constexpr int INITIAL_ASTEROID_COUNT = 4;

/// @brief 弾の速度
inline constexpr float BULLET_SPEED = 400.0f;

/// @brief 弾の最大生存時間（秒）
inline constexpr float BULLET_MAX_LIFETIME = 2.0f;

/// @brief 初期ライフ数
inline constexpr int INITIAL_LIVES = 3;

/// @brief アステロイドサイズごとの半径
inline constexpr float ASTEROID_RADIUS_LARGE = 40.0f;
inline constexpr float ASTEROID_RADIUS_MEDIUM = 20.0f;
inline constexpr float ASTEROID_RADIUS_SMALL = 10.0f;

/// @brief アステロイドサイズごとのスコア
inline constexpr int SCORE_LARGE = 20;
inline constexpr int SCORE_MEDIUM = 50;
inline constexpr int SCORE_SMALL = 100;

// ── 入力状態 ──────────────────────────────────────────

/// @brief 入力状態を表す構造体
struct InputState
{
	bool thrust{false};     ///< 前進
	bool turnLeft{false};   ///< 左旋回
	bool turnRight{false};  ///< 右旋回
	bool fire{false};       ///< 発射
};

// ── コンポーネント ──────────────────────────────────────

/// @brief 速度コンポーネント
struct VelocityComponent
{
	sgc::Vec3f velocity{0.0f, 0.0f, 0.0f};      ///< 現在の速度
	sgc::Vec3f acceleration{0.0f, 0.0f, 0.0f};  ///< 加速度
	float maxSpeed{300.0f};                       ///< 最大速度
	float drag{0.98f};                            ///< 抵抗（毎フレーム乗算）
};

/// @brief スプライトコンポーネント
struct SpriteComponent
{
	std::string spriteId;  ///< スプライトアセットID
	float width{32.0f};   ///< 幅
	float height{32.0f};  ///< 高さ
	float rotation{0.0f}; ///< 回転角（ラジアン）
};

/// @brief プレイヤーコンポーネント
struct PlayerComponent
{
	float thrustPower{200.0f};    ///< 推進力
	float turnSpeed{4.0f};       ///< 旋回速度（ラジアン/秒）
	float fireCooldown{0.25f};   ///< 発射クールダウン（秒）
	float fireTimer{0.0f};       ///< 発射タイマー
	int lives{INITIAL_LIVES};    ///< 残機
	int score{0};                ///< スコア
};

/// @brief 弾コンポーネント
struct BulletComponent
{
	float lifetime{0.0f};                      ///< 現在の生存時間
	float maxLifetime{BULLET_MAX_LIFETIME};    ///< 最大生存時間
	float damage{1.0f};                        ///< ダメージ
};

/// @brief アステロイドコンポーネント
/// @note size: 3=大, 2=中, 1=小
struct AsteroidComponent
{
	int size{3};               ///< サイズ（3=大, 2=中, 1=小）
	float rotationSpeed{1.0f}; ///< 回転速度（ラジアン/秒）
};

/// @brief 円形コライダーコンポーネント
struct ColliderCircle
{
	float radius{10.0f};  ///< 半径
};

// ── ゲーム状態（システム間共有用） ──────────────────────

/// @brief ゲーム全体の状態を保持する構造体
struct GameState
{
	float screenW{800.0f};   ///< 画面幅
	float screenH{600.0f};   ///< 画面高さ
	int level{1};            ///< 現在のレベル
	bool gameOver{false};    ///< ゲームオーバーフラグ
	InputState input{};      ///< 現在の入力状態

	/// @brief 破棄予定のエンティティリスト
	std::vector<scene::EntityId> entitiesToRemove;

	/// @brief 生成予定のアステロイド情報
	struct SpawnRequest
	{
		sgc::Vec3f position;  ///< 生成位置
		int size;             ///< サイズ
	};
	std::vector<SpawnRequest> asteroidsToSpawn;

	/// @brief スコア加算リクエスト
	int scoreToAdd{0};
};

// ── システム実装 ──────────────────────────────────────

/// @brief 移動システム — 速度を位置に適用し、画面端でラップする
class MovementSystem : public scene::ISystem
{
public:
	/// @param state ゲーム状態への参照
	explicit MovementSystem(GameState& state) : m_state(state) {}

	[[nodiscard]] std::string name() const override { return "Movement"; }

	void update(scene::GameWorld& world, float dt) override
	{
		world.forEach<VelocityComponent>(
			[this, &world, dt](scene::EntityId id, VelocityComponent& vel)
			{
				auto* transform = world.getComponent<scene::TransformComponent>(id);
				if (!transform) return;

				// 加速度を速度に適用
				vel.velocity += vel.acceleration * dt;

				// 速度制限
				const float speed = std::sqrt(
					vel.velocity.x * vel.velocity.x +
					vel.velocity.y * vel.velocity.y
				);
				if (speed > vel.maxSpeed)
				{
					vel.velocity = vel.velocity * (vel.maxSpeed / speed);
				}

				// 抵抗を適用
				vel.velocity *= vel.drag;

				// 位置を更新
				transform->position += vel.velocity * dt;

				// 画面端でラップ
				wrapPosition(transform->position);
			}
		);
	}

private:
	/// @brief 画面端でラップ処理を行う
	void wrapPosition(sgc::Vec3f& pos) const
	{
		if (pos.x < 0.0f) pos.x += m_state.screenW;
		if (pos.x > m_state.screenW) pos.x -= m_state.screenW;
		if (pos.y < 0.0f) pos.y += m_state.screenH;
		if (pos.y > m_state.screenH) pos.y -= m_state.screenH;
	}

	GameState& m_state;
};

/// @brief プレイヤー操作システム — 入力に基づいて推進・旋回・発射を処理する
class PlayerControlSystem : public scene::ISystem
{
public:
	/// @param state ゲーム状態への参照
	explicit PlayerControlSystem(GameState& state) : m_state(state) {}

	[[nodiscard]] std::string name() const override { return "PlayerControl"; }

	void update(scene::GameWorld& world, float dt) override
	{
		world.forEach<PlayerComponent>(
			[this, &world, dt](scene::EntityId id, PlayerComponent& player)
			{
				if (m_state.gameOver) return;

				auto* transform = world.getComponent<scene::TransformComponent>(id);
				auto* vel = world.getComponent<VelocityComponent>(id);
				auto* sprite = world.getComponent<SpriteComponent>(id);
				if (!transform || !vel || !sprite) return;

				// 旋回
				if (m_state.input.turnLeft)
				{
					sprite->rotation -= player.turnSpeed * dt;
				}
				if (m_state.input.turnRight)
				{
					sprite->rotation += player.turnSpeed * dt;
				}

				// 推進
				if (m_state.input.thrust)
				{
					const float angle = sprite->rotation;
					vel->acceleration.x = std::cos(angle) * player.thrustPower;
					vel->acceleration.y = std::sin(angle) * player.thrustPower;
				}
				else
				{
					vel->acceleration = {0.0f, 0.0f, 0.0f};
				}

				// 発射クールダウン更新
				if (player.fireTimer > 0.0f)
				{
					player.fireTimer -= dt;
				}

				// 発射
				if (m_state.input.fire && player.fireTimer <= 0.0f)
				{
					fireBullet(world, *transform, *sprite);
					player.fireTimer = player.fireCooldown;
				}
			}
		);
	}

private:
	/// @brief 弾を生成する
	void fireBullet(
		scene::GameWorld& world,
		const scene::TransformComponent& playerTransform,
		const SpriteComponent& playerSprite
	)
	{
		const auto bulletId = world.createEntity("Bullet");
		const float angle = playerSprite.rotation;

		scene::TransformComponent bulletTransform;
		bulletTransform.position = playerTransform.position;
		world.addComponent(bulletId, bulletTransform);

		VelocityComponent bulletVel;
		bulletVel.velocity.x = std::cos(angle) * BULLET_SPEED;
		bulletVel.velocity.y = std::sin(angle) * BULLET_SPEED;
		bulletVel.maxSpeed = BULLET_SPEED;
		bulletVel.drag = 1.0f;
		world.addComponent(bulletId, bulletVel);

		BulletComponent bullet;
		world.addComponent(bulletId, bullet);

		ColliderCircle collider;
		collider.radius = 3.0f;
		world.addComponent(bulletId, collider);
	}

	GameState& m_state;
};

/// @brief 弾システム — 弾の生存時間を管理し、期限切れの弾を削除する
class BulletSystem : public scene::ISystem
{
public:
	/// @param state ゲーム状態への参照
	explicit BulletSystem(GameState& state) : m_state(state) {}

	[[nodiscard]] std::string name() const override { return "Bullet"; }

	void update(scene::GameWorld& world, float dt) override
	{
		world.forEach<BulletComponent>(
			[this, dt](scene::EntityId id, BulletComponent& bullet)
			{
				bullet.lifetime += dt;
				if (bullet.lifetime >= bullet.maxLifetime)
				{
					m_state.entitiesToRemove.push_back(id);
				}
			}
		);

		// 期限切れの弾を削除
		for (const auto entityId : m_state.entitiesToRemove)
		{
			world.removeEntity(entityId);
		}
		m_state.entitiesToRemove.clear();
	}

private:
	GameState& m_state;
};

/// @brief 衝突判定システム — 円形コライダー同士の衝突を検出する
class CollisionSystem : public scene::ISystem
{
public:
	/// @param state ゲーム状態への参照
	explicit CollisionSystem(GameState& state) : m_state(state) {}

	[[nodiscard]] std::string name() const override { return "Collision"; }

	void update(scene::GameWorld& world, float dt) override
	{
		// 弾とアステロイドの衝突
		std::vector<scene::EntityId> bulletsToRemove;
		std::vector<scene::EntityId> asteroidsToRemove;

		world.forEach<BulletComponent>(
			[&](scene::EntityId bulletId, BulletComponent&)
			{
				auto* bulletTransform = world.getComponent<scene::TransformComponent>(bulletId);
				auto* bulletCollider = world.getComponent<ColliderCircle>(bulletId);
				if (!bulletTransform || !bulletCollider) return;

				world.forEach<AsteroidComponent>(
					[&](scene::EntityId asteroidId, AsteroidComponent& asteroid)
					{
						auto* astTransform = world.getComponent<scene::TransformComponent>(asteroidId);
						auto* astCollider = world.getComponent<ColliderCircle>(asteroidId);
						if (!astTransform || !astCollider) return;

						if (circlesOverlap(
							bulletTransform->position, bulletCollider->radius,
							astTransform->position, astCollider->radius))
						{
							bulletsToRemove.push_back(bulletId);
							asteroidsToRemove.push_back(asteroidId);

							// スコア加算
							switch (asteroid.size)
							{
							case 3: m_state.scoreToAdd += SCORE_LARGE; break;
							case 2: m_state.scoreToAdd += SCORE_MEDIUM; break;
							case 1: m_state.scoreToAdd += SCORE_SMALL; break;
							}

							// 分裂リクエスト
							if (asteroid.size > 1)
							{
								m_state.asteroidsToSpawn.push_back(
									{astTransform->position, asteroid.size - 1});
								m_state.asteroidsToSpawn.push_back(
									{astTransform->position, asteroid.size - 1});
							}
						}
					}
				);
			}
		);

		// 重複を排除して削除
		removeDuplicates(bulletsToRemove);
		removeDuplicates(asteroidsToRemove);

		for (const auto id : bulletsToRemove) world.removeEntity(id);
		for (const auto id : asteroidsToRemove) world.removeEntity(id);

		// 分裂したアステロイドを生成
		for (const auto& req : m_state.asteroidsToSpawn)
		{
			spawnAsteroid(world, req.position, req.size);
		}
		m_state.asteroidsToSpawn.clear();

		// プレイヤーとアステロイドの衝突
		world.forEach<PlayerComponent>(
			[&](scene::EntityId playerId, PlayerComponent& player)
			{
				if (m_state.gameOver) return;

				auto* playerTransform = world.getComponent<scene::TransformComponent>(playerId);
				auto* playerCollider = world.getComponent<ColliderCircle>(playerId);
				if (!playerTransform || !playerCollider) return;

				world.forEach<AsteroidComponent>(
					[&](scene::EntityId asteroidId, AsteroidComponent&)
					{
						auto* astTransform = world.getComponent<scene::TransformComponent>(asteroidId);
						auto* astCollider = world.getComponent<ColliderCircle>(asteroidId);
						if (!astTransform || !astCollider) return;

						if (circlesOverlap(
							playerTransform->position, playerCollider->radius,
							astTransform->position, astCollider->radius))
						{
							player.lives--;
							if (player.lives <= 0)
							{
								m_state.gameOver = true;
							}
							else
							{
								// プレイヤーを中央にリスポーン
								playerTransform->position = {
									m_state.screenW * 0.5f,
									m_state.screenH * 0.5f,
									0.0f
								};
								auto* vel = world.getComponent<VelocityComponent>(playerId);
								if (vel)
								{
									vel->velocity = {0.0f, 0.0f, 0.0f};
									vel->acceleration = {0.0f, 0.0f, 0.0f};
								}
							}
						}
					}
				);
			}
		);
	}

private:
	/// @brief 2つの円が重なっているか判定する
	[[nodiscard]] static bool circlesOverlap(
		const sgc::Vec3f& posA, float radiusA,
		const sgc::Vec3f& posB, float radiusB) noexcept
	{
		const float dx = posA.x - posB.x;
		const float dy = posA.y - posB.y;
		const float distSq = dx * dx + dy * dy;
		const float radSum = radiusA + radiusB;
		return distSq <= radSum * radSum;
	}

	/// @brief ベクターから重複要素を排除する
	static void removeDuplicates(std::vector<scene::EntityId>& vec)
	{
		std::sort(vec.begin(), vec.end());
		vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
	}

	/// @brief アステロイドを生成する
	static void spawnAsteroid(
		scene::GameWorld& world,
		const sgc::Vec3f& position,
		int size)
	{
		const auto id = world.createEntity("Asteroid");

		scene::TransformComponent transform;
		transform.position = position;
		world.addComponent(id, transform);

		float radius = ASTEROID_RADIUS_SMALL;
		float speed = 100.0f;
		if (size == 3) { radius = ASTEROID_RADIUS_LARGE; speed = 50.0f; }
		else if (size == 2) { radius = ASTEROID_RADIUS_MEDIUM; speed = 75.0f; }

		VelocityComponent vel;
		// 簡易的なランダム方向（シード不要 — サイズとIDで分散）
		const float angle = static_cast<float>(id) * 2.39996f;
		vel.velocity.x = std::cos(angle) * speed;
		vel.velocity.y = std::sin(angle) * speed;
		vel.maxSpeed = speed * 1.5f;
		vel.drag = 1.0f;
		world.addComponent(id, vel);

		AsteroidComponent asteroid;
		asteroid.size = size;
		asteroid.rotationSpeed = 1.0f / static_cast<float>(size);
		world.addComponent(id, asteroid);

		ColliderCircle collider;
		collider.radius = radius;
		world.addComponent(id, collider);

		SpriteComponent sprite;
		sprite.spriteId = "asteroid";
		sprite.width = radius * 2.0f;
		sprite.height = radius * 2.0f;
		world.addComponent(id, sprite);
	}

	GameState& m_state;
};

/// @brief アステロイド生成システム — 全アステロイド破壊時に次のウェーブを生成する
class AsteroidSpawnerSystem : public scene::ISystem
{
public:
	/// @param state ゲーム状態への参照
	explicit AsteroidSpawnerSystem(GameState& state) : m_state(state) {}

	[[nodiscard]] std::string name() const override { return "AsteroidSpawner"; }

	void update(scene::GameWorld& world, float dt) override
	{
		if (m_state.gameOver) return;

		// アステロイド数をカウント
		int asteroidCount = 0;
		world.forEach<AsteroidComponent>(
			[&asteroidCount](scene::EntityId, AsteroidComponent&)
			{
				++asteroidCount;
			}
		);

		// 全滅していたら次のウェーブを生成
		if (asteroidCount == 0)
		{
			m_state.level++;
			const int count = INITIAL_ASTEROID_COUNT + m_state.level - 1;
			for (int i = 0; i < count; ++i)
			{
				// 画面端にスポーン
				const float angle = static_cast<float>(i)
					* (2.0f * std::numbers::pi_v<float>)
					/ static_cast<float>(count);
				const sgc::Vec3f pos{
					m_state.screenW * 0.5f + std::cos(angle) * m_state.screenW * 0.4f,
					m_state.screenH * 0.5f + std::sin(angle) * m_state.screenH * 0.4f,
					0.0f
				};
				spawnLargeAsteroid(world, pos);
			}
		}
	}

private:
	/// @brief 大サイズアステロイドを生成する
	void spawnLargeAsteroid(scene::GameWorld& world, const sgc::Vec3f& position)
	{
		const auto id = world.createEntity("Asteroid");

		scene::TransformComponent transform;
		transform.position = position;
		world.addComponent(id, transform);

		VelocityComponent vel;
		const float angle = static_cast<float>(id) * 2.39996f;
		vel.velocity.x = std::cos(angle) * 50.0f;
		vel.velocity.y = std::sin(angle) * 50.0f;
		vel.maxSpeed = 75.0f;
		vel.drag = 1.0f;
		world.addComponent(id, vel);

		AsteroidComponent asteroid;
		asteroid.size = 3;
		asteroid.rotationSpeed = 0.33f;
		world.addComponent(id, asteroid);

		ColliderCircle collider;
		collider.radius = ASTEROID_RADIUS_LARGE;
		world.addComponent(id, collider);

		SpriteComponent sprite;
		sprite.spriteId = "asteroid_large";
		sprite.width = ASTEROID_RADIUS_LARGE * 2.0f;
		sprite.height = ASTEROID_RADIUS_LARGE * 2.0f;
		world.addComponent(id, sprite);
	}

	GameState& m_state;
};

/// @brief スコアシステム — スコア加算リクエストを処理する
class ScoreSystem : public scene::ISystem
{
public:
	/// @param state ゲーム状態への参照
	explicit ScoreSystem(GameState& state) : m_state(state) {}

	[[nodiscard]] std::string name() const override { return "Score"; }

	void update(scene::GameWorld& world, float dt) override
	{
		if (m_state.scoreToAdd <= 0) return;

		world.forEach<PlayerComponent>(
			[this](scene::EntityId, PlayerComponent& player)
			{
				player.score += m_state.scoreToAdd;
			}
		);
		m_state.scoreToAdd = 0;
	}

private:
	GameState& m_state;
};

// ── ゲーム本体 ──────────────────────────────────────

/// @brief アステロイドゲーム
///
/// MitiruEngineのGameWorldとSystemRunnerを使った
/// アステロイドスタイルのミニゲーム。
/// update()呼び出しのみでゲームが進行し、描画に依存しない。
class AsteroidGame
{
public:
	/// @brief ゲームを初期化する
	/// @param screenW 画面幅
	/// @param screenH 画面高さ
	void init(float screenW, float screenH)
	{
		m_world = scene::GameWorld{};
		m_runner = scene::SystemRunner{};
		m_state = GameState{};
		m_state.screenW = screenW;
		m_state.screenH = screenH;
		m_state.level = 0;
		m_playerId = scene::INVALID_ENTITY;

		// プレイヤーを生成
		m_playerId = m_world.createEntity("Player");

		scene::TransformComponent playerTransform;
		playerTransform.position = {screenW * 0.5f, screenH * 0.5f, 0.0f};
		m_world.addComponent(m_playerId, playerTransform);

		VelocityComponent playerVel;
		playerVel.maxSpeed = 300.0f;
		playerVel.drag = 0.98f;
		m_world.addComponent(m_playerId, playerVel);

		PlayerComponent playerComp;
		m_world.addComponent(m_playerId, playerComp);

		SpriteComponent playerSprite;
		playerSprite.spriteId = "player_ship";
		playerSprite.width = 32.0f;
		playerSprite.height = 32.0f;
		m_world.addComponent(m_playerId, playerSprite);

		ColliderCircle playerCollider;
		playerCollider.radius = 14.0f;
		m_world.addComponent(m_playerId, playerCollider);

		// システムを登録（優先度順）
		m_runner.addSystem(std::make_unique<PlayerControlSystem>(m_state), 0);
		m_runner.addSystem(std::make_unique<MovementSystem>(m_state), 10);
		m_runner.addSystem(std::make_unique<BulletSystem>(m_state), 20);
		m_runner.addSystem(std::make_unique<CollisionSystem>(m_state), 30);
		m_runner.addSystem(std::make_unique<ScoreSystem>(m_state), 40);
		m_runner.addSystem(std::make_unique<AsteroidSpawnerSystem>(m_state), 50);
	}

	/// @brief ゲームを更新する
	/// @param dt デルタタイム（秒）
	/// @param input 入力状態
	void update(float dt, const InputState& input)
	{
		m_state.input = input;
		m_runner.updateAll(m_world, dt);
	}

	/// @brief ゲームワールドを取得する
	/// @return ゲームワールドへのconst参照
	[[nodiscard]] const scene::GameWorld& getWorld() const noexcept
	{
		return m_world;
	}

	/// @brief ゲームワールドを取得する（非const版）
	/// @return ゲームワールドへの参照
	[[nodiscard]] scene::GameWorld& getWorld() noexcept
	{
		return m_world;
	}

	/// @brief ゲームオーバーか判定する
	/// @return ゲームオーバーならtrue
	[[nodiscard]] bool isGameOver() const noexcept
	{
		return m_state.gameOver;
	}

	/// @brief 現在のスコアを取得する
	/// @return スコア
	[[nodiscard]] int getScore() const noexcept
	{
		const auto* player = m_world.getComponent<PlayerComponent>(m_playerId);
		return player ? player->score : 0;
	}

	/// @brief 現在のレベルを取得する
	/// @return レベル
	[[nodiscard]] int getLevel() const noexcept
	{
		return m_state.level;
	}

	/// @brief 残機数を取得する
	/// @return 残機数
	[[nodiscard]] int getLives() const noexcept
	{
		const auto* player = m_world.getComponent<PlayerComponent>(m_playerId);
		return player ? player->lives : 0;
	}

	/// @brief ゲームをリセットする
	void reset()
	{
		const float w = m_state.screenW;
		const float h = m_state.screenH;
		init(w, h);
	}

	/// @brief プレイヤーのエンティティIDを取得する
	/// @return プレイヤーID
	[[nodiscard]] scene::EntityId getPlayerId() const noexcept
	{
		return m_playerId;
	}

	/// @brief ゲーム状態を取得する（テスト用）
	/// @return ゲーム状態への参照
	[[nodiscard]] GameState& getState() noexcept
	{
		return m_state;
	}

private:
	scene::GameWorld m_world;      ///< ゲームワールド
	scene::SystemRunner m_runner;  ///< システムランナー
	GameState m_state;             ///< ゲーム状態
	scene::EntityId m_playerId{scene::INVALID_ENTITY};  ///< プレイヤーID
};

} // namespace mitiru::sample
