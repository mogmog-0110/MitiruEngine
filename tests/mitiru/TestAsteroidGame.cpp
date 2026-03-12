#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/sample/AsteroidGame.hpp"

using namespace mitiru::sample;
using namespace mitiru::scene;

// ── ヘルパー関数 ──────────────────────────────────────

/// @brief アステロイド数をカウントする
static int countAsteroids(AsteroidGame& game)
{
	int count = 0;
	// const_castは不要 — getWorld()非const版を使用
	game.getWorld().forEach<AsteroidComponent>(
		[&count](EntityId, AsteroidComponent&) { ++count; }
	);
	return count;
}

/// @brief 弾数をカウントする
static int countBullets(AsteroidGame& game)
{
	int count = 0;
	game.getWorld().forEach<BulletComponent>(
		[&count](EntityId, BulletComponent&) { ++count; }
	);
	return count;
}

/// @brief 空の入力状態を返す
static InputState noInput()
{
	return {};
}

// ── テストケース ──────────────────────────────────────

TEST_CASE("AsteroidGame init creates player entity", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	const auto playerId = game.getPlayerId();
	REQUIRE(playerId != INVALID_ENTITY);
	REQUIRE(game.getWorld().isValid(playerId));
	REQUIRE(game.getWorld().entityName(playerId) == "Player");

	// プレイヤーは必要なコンポーネントを持つ
	REQUIRE(game.getWorld().getComponent<TransformComponent>(playerId) != nullptr);
	REQUIRE(game.getWorld().getComponent<VelocityComponent>(playerId) != nullptr);
	REQUIRE(game.getWorld().getComponent<PlayerComponent>(playerId) != nullptr);
	REQUIRE(game.getWorld().getComponent<SpriteComponent>(playerId) != nullptr);
	REQUIRE(game.getWorld().getComponent<ColliderCircle>(playerId) != nullptr);

	// 初期位置は画面中央
	const auto* transform = game.getWorld().getComponent<TransformComponent>(playerId);
	REQUIRE(transform->position.x == Catch::Approx(400.0f));
	REQUIRE(transform->position.y == Catch::Approx(300.0f));
}

TEST_CASE("AsteroidGame init spawns asteroids", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// 最初のupdateでAsteroidSpawnerSystemがウェーブを生成する
	game.update(1.0f / 60.0f, noInput());

	const int asteroids = countAsteroids(game);
	// level 1 = INITIAL_ASTEROID_COUNT + 1 - 1 = INITIAL_ASTEROID_COUNT
	REQUIRE(asteroids >= INITIAL_ASTEROID_COUNT);
}

TEST_CASE("MovementSystem updates positions", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// プレイヤーに速度を設定
	auto* vel = game.getWorld().getComponent<VelocityComponent>(game.getPlayerId());
	REQUIRE(vel != nullptr);
	vel->velocity = {100.0f, 0.0f, 0.0f};
	vel->drag = 1.0f;

	const auto* transform = game.getWorld().getComponent<TransformComponent>(game.getPlayerId());
	const float initialX = transform->position.x;

	// AsteroidSpawnerが動かないようゲームオーバーにしないが、
	// 衝突しないよう短いdtで更新
	game.update(0.1f, noInput());

	REQUIRE(transform->position.x > initialX);
}

TEST_CASE("MovementSystem wraps around screen bounds", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	auto* transform = game.getWorld().getComponent<TransformComponent>(game.getPlayerId());
	auto* vel = game.getWorld().getComponent<VelocityComponent>(game.getPlayerId());
	REQUIRE(transform != nullptr);
	REQUIRE(vel != nullptr);

	// 画面右端を超える位置に設定
	transform->position = {810.0f, 300.0f, 0.0f};
	vel->velocity = {0.0f, 0.0f, 0.0f};
	vel->drag = 1.0f;

	game.update(0.016f, noInput());

	// ラップして画面内に戻る
	REQUIRE(transform->position.x < 800.0f);
	REQUIRE(transform->position.x >= 0.0f);
}

TEST_CASE("PlayerControlSystem thrust changes velocity", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	auto* vel = game.getWorld().getComponent<VelocityComponent>(game.getPlayerId());
	REQUIRE(vel != nullptr);
	const float initialSpeedSq = vel->velocity.x * vel->velocity.x
		+ vel->velocity.y * vel->velocity.y;

	InputState input{};
	input.thrust = true;

	// 複数フレーム更新して加速を確認
	for (int i = 0; i < 10; ++i)
	{
		game.update(1.0f / 60.0f, input);
	}

	const float finalSpeedSq = vel->velocity.x * vel->velocity.x
		+ vel->velocity.y * vel->velocity.y;
	REQUIRE(finalSpeedSq > initialSpeedSq);
}

TEST_CASE("PlayerControlSystem fire creates bullet", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	REQUIRE(countBullets(game) == 0);

	InputState input{};
	input.fire = true;

	game.update(1.0f / 60.0f, input);

	REQUIRE(countBullets(game) >= 1);
}

TEST_CASE("BulletSystem removes expired bullets", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// 弾を発射
	InputState fireInput{};
	fireInput.fire = true;
	game.update(1.0f / 60.0f, fireInput);
	REQUIRE(countBullets(game) >= 1);

	// 弾の生存時間を超過させる
	// BULLET_MAX_LIFETIME + 余裕分を一気に進める
	for (int i = 0; i < 200; ++i)
	{
		game.update(0.02f, noInput());
	}

	REQUIRE(countBullets(game) == 0);
}

TEST_CASE("CollisionSystem detects bullet-asteroid hit", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// 初回更新でアステロイド生成
	game.update(1.0f / 60.0f, noInput());
	const int initialAsteroids = countAsteroids(game);
	REQUIRE(initialAsteroids > 0);

	// アステロイドの位置を取得
	sgc::Vec3f asteroidPos{};
	EntityId targetAsteroidId = INVALID_ENTITY;
	game.getWorld().forEach<AsteroidComponent>(
		[&](EntityId id, AsteroidComponent&)
		{
			if (targetAsteroidId != INVALID_ENTITY) return;
			auto* t = game.getWorld().getComponent<TransformComponent>(id);
			if (t)
			{
				asteroidPos = t->position;
				targetAsteroidId = id;
			}
		}
	);

	// プレイヤー位置をアステロイドの近くに設定し、弾を発射
	auto* playerTransform = game.getWorld().getComponent<TransformComponent>(game.getPlayerId());
	auto* playerSprite = game.getWorld().getComponent<SpriteComponent>(game.getPlayerId());
	REQUIRE(playerTransform != nullptr);
	REQUIRE(playerSprite != nullptr);

	// プレイヤーをアステロイド方向に配置
	const float dx = asteroidPos.x - playerTransform->position.x;
	const float dy = asteroidPos.y - playerTransform->position.y;
	playerSprite->rotation = std::atan2(dy, dx);

	// 弾を発射して複数フレーム更新
	InputState fireInput{};
	fireInput.fire = true;
	game.update(1.0f / 60.0f, fireInput);

	// 弾をアステロイドの位置に直接移動（確実にヒットさせるため）
	game.getWorld().forEach<BulletComponent>(
		[&](EntityId bulletId, BulletComponent&)
		{
			auto* bt = game.getWorld().getComponent<TransformComponent>(bulletId);
			if (bt)
			{
				bt->position = asteroidPos;
			}
		}
	);

	// 衝突判定を実行
	game.update(1.0f / 60.0f, noInput());

	// アステロイドが破壊された（数が変わった）
	// 大アステロイドは2つの中アステロイドに分裂するので、
	// 総数は変わるか、元のIDが無効になっている
	REQUIRE_FALSE(game.getWorld().isValid(targetAsteroidId));
}

TEST_CASE("CollisionSystem asteroid splits into smaller pieces", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// 初回更新でアステロイド生成
	game.update(1.0f / 60.0f, noInput());

	// 大アステロイドを見つける
	EntityId largeAsteroidId = INVALID_ENTITY;
	sgc::Vec3f asteroidPos{};
	game.getWorld().forEach<AsteroidComponent>(
		[&](EntityId id, AsteroidComponent& ast)
		{
			if (largeAsteroidId != INVALID_ENTITY) return;
			if (ast.size == 3)
			{
				largeAsteroidId = id;
				auto* t = game.getWorld().getComponent<TransformComponent>(id);
				if (t) asteroidPos = t->position;
			}
		}
	);
	REQUIRE(largeAsteroidId != INVALID_ENTITY);

	// 弾を発射してアステロイド位置に配置
	InputState fireInput{};
	fireInput.fire = true;
	game.update(1.0f / 60.0f, fireInput);

	game.getWorld().forEach<BulletComponent>(
		[&](EntityId bulletId, BulletComponent&)
		{
			auto* bt = game.getWorld().getComponent<TransformComponent>(bulletId);
			if (bt) bt->position = asteroidPos;
		}
	);

	// 衝突前の中アステロイド数
	int mediumBefore = 0;
	game.getWorld().forEach<AsteroidComponent>(
		[&](EntityId, AsteroidComponent& ast)
		{
			if (ast.size == 2) ++mediumBefore;
		}
	);

	game.update(1.0f / 60.0f, noInput());

	// 中アステロイドが2つ増えている
	int mediumAfter = 0;
	game.getWorld().forEach<AsteroidComponent>(
		[&](EntityId, AsteroidComponent& ast)
		{
			if (ast.size == 2) ++mediumAfter;
		}
	);

	REQUIRE(mediumAfter == mediumBefore + 2);
}

TEST_CASE("AsteroidSpawnerSystem spawns new wave", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// 初回更新でレベル1のウェーブが生成される
	game.update(1.0f / 60.0f, noInput());
	REQUIRE(game.getLevel() == 1);

	// 全アステロイドを手動で削除
	std::vector<EntityId> asteroidIds;
	game.getWorld().forEach<AsteroidComponent>(
		[&](EntityId id, AsteroidComponent&) { asteroidIds.push_back(id); }
	);
	for (const auto id : asteroidIds)
	{
		game.getWorld().removeEntity(id);
	}
	REQUIRE(countAsteroids(game) == 0);

	// 次のupdateで新しいウェーブが生成される
	game.update(1.0f / 60.0f, noInput());

	REQUIRE(game.getLevel() == 2);
	REQUIRE(countAsteroids(game) > 0);
}

TEST_CASE("Score increases on asteroid destruction", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// 初回更新でアステロイド生成
	game.update(1.0f / 60.0f, noInput());

	REQUIRE(game.getScore() == 0);

	// アステロイドの位置を取得
	sgc::Vec3f asteroidPos{};
	game.getWorld().forEach<AsteroidComponent>(
		[&](EntityId id, AsteroidComponent&)
		{
			auto* t = game.getWorld().getComponent<TransformComponent>(id);
			if (t) asteroidPos = t->position;
		}
	);

	// 弾を発射してアステロイド位置に配置
	InputState fireInput{};
	fireInput.fire = true;
	game.update(1.0f / 60.0f, fireInput);

	game.getWorld().forEach<BulletComponent>(
		[&](EntityId bulletId, BulletComponent&)
		{
			auto* bt = game.getWorld().getComponent<TransformComponent>(bulletId);
			if (bt) bt->position = asteroidPos;
		}
	);

	game.update(1.0f / 60.0f, noInput());

	REQUIRE(game.getScore() > 0);
}

TEST_CASE("Game over when lives reach 0", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	REQUIRE_FALSE(game.isGameOver());

	// ライフを直接0にする
	auto* player = game.getWorld().getComponent<PlayerComponent>(game.getPlayerId());
	REQUIRE(player != nullptr);
	player->lives = 1;

	// 初回更新でアステロイド生成
	game.update(1.0f / 60.0f, noInput());

	// プレイヤーをアステロイドの位置に移動
	sgc::Vec3f asteroidPos{};
	game.getWorld().forEach<AsteroidComponent>(
		[&](EntityId id, AsteroidComponent&)
		{
			auto* t = game.getWorld().getComponent<TransformComponent>(id);
			if (t) asteroidPos = t->position;
		}
	);

	auto* playerTransform = game.getWorld().getComponent<TransformComponent>(game.getPlayerId());
	REQUIRE(playerTransform != nullptr);
	playerTransform->position = asteroidPos;

	game.update(1.0f / 60.0f, noInput());

	REQUIRE(game.isGameOver());
}

TEST_CASE("Reset restores initial state", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	// ゲームを少し進める
	InputState input{};
	input.thrust = true;
	input.fire = true;
	for (int i = 0; i < 30; ++i)
	{
		game.update(1.0f / 60.0f, input);
	}

	// リセット
	game.reset();

	REQUIRE_FALSE(game.isGameOver());
	REQUIRE(game.getScore() == 0);
	REQUIRE(game.getLives() == INITIAL_LIVES);

	// プレイヤーが画面中央に戻っている
	const auto* transform = game.getWorld().getComponent<TransformComponent>(game.getPlayerId());
	REQUIRE(transform != nullptr);
	REQUIRE(transform->position.x == Catch::Approx(400.0f));
	REQUIRE(transform->position.y == Catch::Approx(300.0f));
}

TEST_CASE("AsteroidGame initial lives count", "[mitiru][sample][asteroid]")
{
	AsteroidGame game;
	game.init(800.0f, 600.0f);

	REQUIRE(game.getLives() == INITIAL_LIVES);
}
