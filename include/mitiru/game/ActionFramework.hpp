#pragma once

/// @file ActionFramework.hpp
/// @brief 2Dアクションゲームフレームワーク
/// @details タイルマップ衝突判定、プラットフォーマー物理、ヒットボックスシステムを提供する。
///
/// @code
/// mitiru::game::PlatformerPhysics physics;
/// mitiru::game::PlatformerConfig config{};
/// mitiru::game::PlatformerState state{};
/// state = physics.update(dt, input, tilemap, state, config);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mitiru::game
{

// ─── 基本型 ───

/// @brief 2Dベクトル
struct Vec2f
{
	float x = 0.0f;
	float y = 0.0f;

	Vec2f operator+(const Vec2f& o) const noexcept { return {x + o.x, y + o.y}; }
	Vec2f operator-(const Vec2f& o) const noexcept { return {x - o.x, y - o.y}; }
	Vec2f operator*(float s) const noexcept { return {x * s, y * s}; }
};

/// @brief 軸平行バウンディングボックス
struct AABB
{
	float x = 0.0f;      ///< 左上X
	float y = 0.0f;      ///< 左上Y
	float width = 0.0f;  ///< 幅
	float height = 0.0f; ///< 高さ

	[[nodiscard]] float left() const noexcept { return x; }
	[[nodiscard]] float right() const noexcept { return x + width; }
	[[nodiscard]] float top() const noexcept { return y; }
	[[nodiscard]] float bottom() const noexcept { return y + height; }
	[[nodiscard]] Vec2f center() const noexcept { return {x + width * 0.5f, y + height * 0.5f}; }

	/// @brief 二つのAABBがオーバーラップしているか
	[[nodiscard]] bool overlaps(const AABB& other) const noexcept
	{
		return left() < other.right() && right() > other.left()
			&& top() < other.bottom() && bottom() > other.top();
	}
};

// ─── タイルマップ衝突 ───

/// @brief 衝突応答の種類
enum class CollisionResponse
{
	Slide,  ///< スライド
	Stop,   ///< 停止
	Bounce  ///< 反射
};

/// @brief タイルの種類
enum class TileType : std::uint8_t
{
	Empty = 0,  ///< 空
	Solid,      ///< ソリッド
	OneWay,     ///< 一方通行（上からのみ）
	Slope       ///< スロープ
};

/// @brief タイルマップデータ
struct Tilemap
{
	int width = 0;             ///< タイル数（横）
	int height = 0;            ///< タイル数（縦）
	float tileSize = 16.0f;    ///< タイルサイズ（ピクセル）
	std::vector<TileType> tiles; ///< タイルデータ (row-major)

	/// @brief 指定座標のタイルを取得する
	[[nodiscard]] TileType getTile(int col, int row) const noexcept
	{
		if (col < 0 || col >= width || row < 0 || row >= height)
		{
			return TileType::Solid; // 範囲外はソリッド扱い
		}
		return tiles[static_cast<std::size_t>(row * width + col)];
	}

	/// @brief ワールド座標からタイル列を取得する
	[[nodiscard]] int worldToCol(float wx) const noexcept
	{
		return static_cast<int>(std::floor(wx / tileSize));
	}

	/// @brief ワールド座標からタイル行を取得する
	[[nodiscard]] int worldToRow(float wy) const noexcept
	{
		return static_cast<int>(std::floor(wy / tileSize));
	}
};

/// @brief タイルマップ衝突判定結果
struct TilemapCollisionResult
{
	Vec2f resolvedPosition;  ///< 衝突解決後の位置
	Vec2f resolvedVelocity;  ///< 衝突解決後の速度
	bool collidedX = false;  ///< X軸で衝突したか
	bool collidedY = false;  ///< Y軸で衝突したか
};

/// @brief タイルマップとAABBの衝突判定・解決
class TilemapCollider
{
public:
	/// @brief AABBをタイルマップに対して移動・衝突解決する
	/// @param aabb 移動前のAABB
	/// @param velocity 速度ベクトル
	/// @param tilemap タイルマップ
	/// @param dt デルタタイム
	/// @param response 衝突応答タイプ
	/// @param bounceFactor バウンス時の反発係数
	/// @return 衝突解決結果
	[[nodiscard]] static TilemapCollisionResult resolve(
		const AABB& aabb,
		const Vec2f& velocity,
		const Tilemap& tilemap,
		float dt,
		CollisionResponse response = CollisionResponse::Slide,
		float bounceFactor = 0.5f) noexcept
	{
		TilemapCollisionResult result;
		result.resolvedVelocity = velocity;

		// X軸の移動と衝突解決
		AABB movedX = aabb;
		movedX.x += velocity.x * dt;

		if (checkTileOverlap(movedX, tilemap))
		{
			result.collidedX = true;
			movedX.x = aabb.x; // 元に戻す

			switch (response)
			{
			case CollisionResponse::Slide:
			case CollisionResponse::Stop:
				result.resolvedVelocity.x = 0.0f;
				break;
			case CollisionResponse::Bounce:
				result.resolvedVelocity.x = -velocity.x * bounceFactor;
				break;
			}
		}

		// Y軸の移動と衝突解決
		AABB movedXY = movedX;
		movedXY.y += velocity.y * dt;

		if (checkTileOverlap(movedXY, tilemap))
		{
			result.collidedY = true;
			movedXY.y = movedX.y; // 元に戻す

			switch (response)
			{
			case CollisionResponse::Slide:
			case CollisionResponse::Stop:
				result.resolvedVelocity.y = 0.0f;
				break;
			case CollisionResponse::Bounce:
				result.resolvedVelocity.y = -velocity.y * bounceFactor;
				break;
			}
		}

		result.resolvedPosition = {movedXY.x, movedXY.y};
		return result;
	}

private:
	/// @brief AABBがタイルマップのソリッドタイルとオーバーラップしているか
	[[nodiscard]] static bool checkTileOverlap(const AABB& aabb, const Tilemap& tilemap) noexcept
	{
		const int colMin = tilemap.worldToCol(aabb.left());
		const int colMax = tilemap.worldToCol(aabb.right() - 0.001f);
		const int rowMin = tilemap.worldToRow(aabb.top());
		const int rowMax = tilemap.worldToRow(aabb.bottom() - 0.001f);

		for (int row = rowMin; row <= rowMax; ++row)
		{
			for (int col = colMin; col <= colMax; ++col)
			{
				const TileType tile = tilemap.getTile(col, row);
				if (tile == TileType::Solid)
				{
					return true;
				}
				if (tile == TileType::OneWay)
				{
					// 一方通行：下から侵入する場合のみ当たり
					const float tileTop = static_cast<float>(row) * tilemap.tileSize;
					if (aabb.bottom() > tileTop && aabb.bottom() - tileTop < tilemap.tileSize * 0.5f)
					{
						return true;
					}
				}
			}
		}
		return false;
	}
};

// ─── プラットフォーマー物理 ───

/// @brief プラットフォーマー入力
struct PlatformerInput
{
	float moveX = 0.0f;   ///< 水平入力（-1..1）
	bool jump = false;     ///< ジャンプ入力
	bool jumpHeld = false; ///< ジャンプ長押し
};

/// @brief プラットフォーマー物理設定
struct PlatformerConfig
{
	float gravity = 980.0f;         ///< 重力加速度 (px/s²)
	float jumpForce = 400.0f;       ///< ジャンプ力 (px/s)
	float maxFallSpeed = 600.0f;    ///< 最大落下速度 (px/s)
	float coyoteTime = 0.1f;        ///< コヨーテタイム (秒)
	float jumpBufferTime = 0.12f;   ///< ジャンプバッファ時間 (秒)
	float wallSlideSpeed = 80.0f;   ///< 壁ずり落ち速度 (px/s)
	float airControl = 0.6f;        ///< 空中制御係数 (0..1)
	float moveSpeed = 200.0f;       ///< 地上移動速度 (px/s)
	float wallJumpForceX = 300.0f;  ///< 壁ジャンプ横力 (px/s)
	float wallJumpForceY = 380.0f;  ///< 壁ジャンプ縦力 (px/s)
};

/// @brief プラットフォーマー物理状態
struct PlatformerState
{
	bool grounded = false;        ///< 地面に接しているか
	bool onWall = false;          ///< 壁に接しているか
	int wallDirection = 0;        ///< 壁の方向（-1:左, 0:なし, 1:右）
	bool jumping = false;         ///< ジャンプ中か
	Vec2f velocity;               ///< 現在速度
	Vec2f position;               ///< 現在位置
	float coyoteTimer = 0.0f;     ///< コヨーテタイマー残量
	float jumpBufferTimer = 0.0f; ///< ジャンプバッファタイマー残量
};

/// @brief プラットフォーマー物理エンジン
/// @details 重力・ジャンプ・壁ジャンプ・コヨーテタイム・ジャンプバッファリングを処理する。
class PlatformerPhysics
{
public:
	/// @brief プラットフォーマー物理を1フレーム更新する
	/// @param dt デルタタイム（秒）
	/// @param input 入力
	/// @param tilemap タイルマップ
	/// @param aabbSize キャラクターのAABBサイズ
	/// @param state 現在の状態
	/// @param config 物理設定
	/// @return 更新後の状態
	[[nodiscard]] static PlatformerState update(
		float dt,
		const PlatformerInput& input,
		const Tilemap& tilemap,
		const Vec2f& aabbSize,
		const PlatformerState& state,
		const PlatformerConfig& config = {}) noexcept
	{
		PlatformerState next = state;

		// タイマー更新
		next.coyoteTimer = std::max(0.0f, next.coyoteTimer - dt);
		next.jumpBufferTimer = std::max(0.0f, next.jumpBufferTimer - dt);

		// ジャンプバッファ：入力があればタイマーセット
		if (input.jump)
		{
			next.jumpBufferTimer = config.jumpBufferTime;
		}

		// 水平移動
		const float control = next.grounded ? 1.0f : config.airControl;
		next.velocity.x = input.moveX * config.moveSpeed * control;

		// 重力
		next.velocity.y += config.gravity * dt;

		// 壁ずり落ち
		if (next.onWall && !next.grounded && next.velocity.y > 0.0f)
		{
			next.velocity.y = std::min(next.velocity.y, config.wallSlideSpeed);
		}

		// 最大落下速度制限
		next.velocity.y = std::min(next.velocity.y, config.maxFallSpeed);

		// ジャンプ判定（コヨーテタイム + ジャンプバッファ）
		const bool canJump = next.grounded || next.coyoteTimer > 0.0f;
		if (canJump && next.jumpBufferTimer > 0.0f)
		{
			next.velocity.y = -config.jumpForce;
			next.jumping = true;
			next.coyoteTimer = 0.0f;
			next.jumpBufferTimer = 0.0f;
		}

		// 壁ジャンプ
		if (next.onWall && !next.grounded && input.jump)
		{
			next.velocity.x = -static_cast<float>(next.wallDirection) * config.wallJumpForceX;
			next.velocity.y = -config.wallJumpForceY;
			next.jumping = true;
			next.onWall = false;
		}

		// 可変ジャンプ高さ：長押ししないと早く落ちる
		if (next.jumping && !input.jumpHeld && next.velocity.y < 0.0f)
		{
			next.velocity.y *= 0.5f;
			next.jumping = false;
		}

		// タイルマップ衝突解決
		const AABB aabb{next.position.x, next.position.y, aabbSize.x, aabbSize.y};
		const auto collisionResult = TilemapCollider::resolve(
			aabb, next.velocity, tilemap, dt, CollisionResponse::Slide);

		next.position = collisionResult.resolvedPosition;
		next.velocity = collisionResult.resolvedVelocity;

		// 接地判定：下方向に衝突した場合
		const bool wasGrounded = next.grounded;
		next.grounded = collisionResult.collidedY && state.velocity.y >= 0.0f;

		// コヨーテタイム：接地から離れた瞬間にタイマー開始
		if (wasGrounded && !next.grounded && !next.jumping)
		{
			next.coyoteTimer = config.coyoteTime;
		}

		if (next.grounded)
		{
			next.jumping = false;
		}

		// 壁接触判定
		next.onWall = collisionResult.collidedX && !next.grounded;
		if (next.onWall)
		{
			next.wallDirection = (input.moveX > 0.0f) ? 1 : -1;
		}
		else
		{
			next.wallDirection = 0;
		}

		return next;
	}
};

// ─── ヒットボックスシステム ───

/// @brief ヒット結果
struct HitResult
{
	std::uint32_t attackerOwnerId = 0; ///< 攻撃側オーナーID
	std::uint32_t defenderOwnerId = 0; ///< 防御側オーナーID
	std::uint32_t attackerHitboxIdx = 0; ///< 攻撃側ヒットボックスインデックス
	std::uint32_t defenderHitboxIdx = 0; ///< 防御側ヒットボックスインデックス
	float damage = 0.0f;              ///< ダメージ量
	Vec2f knockback;                   ///< ノックバックベクトル
};

/// @brief ヒットボックス
struct Hitbox
{
	std::uint32_t ownerId = 0; ///< オーナーエンティティID
	AABB rect;                 ///< 矩形領域
	std::uint32_t layer = 0;   ///< レイヤー（同レイヤーは衝突しない）
	bool active = true;        ///< 有効フラグ
	float damage = 0.0f;       ///< ダメージ量
	Vec2f knockback;           ///< ノックバックベクトル
};

/// @brief ヒットボックスシステム
/// @details ヒットボックスの登録・オーバーラップ検出・コールバック管理を行う。
class HitboxSystem
{
public:
	/// @brief ヒットボックスを登録する
	/// @param hitbox ヒットボックス
	/// @return 登録インデックス
	std::uint32_t registerHitbox(const Hitbox& hitbox)
	{
		const auto idx = static_cast<std::uint32_t>(m_hitboxes.size());
		m_hitboxes.push_back(hitbox);
		return idx;
	}

	/// @brief ヒットボックスを取得する（変更可）
	/// @param index インデックス
	/// @return ヒットボックスへのポインタ（無効な場合nullptr）
	[[nodiscard]] Hitbox* getHitbox(std::uint32_t index) noexcept
	{
		if (index >= m_hitboxes.size()) return nullptr;
		return &m_hitboxes[index];
	}

	/// @brief すべてのオーバーラップを検出する
	/// @return ヒット結果のリスト（同オーナー同士・同レイヤー同士は除外）
	[[nodiscard]] std::vector<HitResult> checkOverlaps() const
	{
		std::vector<HitResult> results;
		const auto count = m_hitboxes.size();

		for (std::size_t i = 0; i < count; ++i)
		{
			const auto& a = m_hitboxes[i];
			if (!a.active) continue;

			for (std::size_t j = i + 1; j < count; ++j)
			{
				const auto& b = m_hitboxes[j];
				if (!b.active) continue;

				// 同オーナー・同レイヤーはスキップ
				if (a.ownerId == b.ownerId) continue;
				if (a.layer == b.layer) continue;

				if (a.rect.overlaps(b.rect))
				{
					// a→b のヒット
					results.push_back(HitResult{
						a.ownerId, b.ownerId,
						static_cast<std::uint32_t>(i),
						static_cast<std::uint32_t>(j),
						a.damage, a.knockback
					});

					// b→a のヒット
					results.push_back(HitResult{
						b.ownerId, a.ownerId,
						static_cast<std::uint32_t>(j),
						static_cast<std::uint32_t>(i),
						b.damage, b.knockback
					});
				}
			}
		}
		return results;
	}

	/// @brief ヒットコールバックを設定する
	/// @param callback コールバック関数
	void setHitCallback(std::function<void(const HitResult&)> callback)
	{
		m_hitCallback = std::move(callback);
	}

	/// @brief オーバーラップを検出しコールバックを発火する
	void processOverlaps()
	{
		if (!m_hitCallback) return;

		const auto results = checkOverlaps();
		for (const auto& hit : results)
		{
			m_hitCallback(hit);
		}
	}

	/// @brief 全ヒットボックスをクリアする
	void clear()
	{
		m_hitboxes.clear();
	}

	/// @brief 登録数を返す
	[[nodiscard]] std::size_t count() const noexcept { return m_hitboxes.size(); }

private:
	std::vector<Hitbox> m_hitboxes;
	std::function<void(const HitResult&)> m_hitCallback;
};

} // namespace mitiru::game
