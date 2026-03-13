#pragma once

/// @file Player.hpp
/// @brief GraphWalkerプレイヤーオブジェクト
///
/// 重力・速度積分・ジャンプ・ダメージ・リスポーンを持つプレイヤー。
/// 円形の当たり判定を使用する。
///
/// @code
/// mitiru::gw::Player player({100.0f, 300.0f});
/// player.applyThrust({1.0f, 0.0f});
/// player.jump();
/// player.update(1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cmath>

#include "mitiru/graphwalker/GameObject.hpp"

namespace mitiru::gw
{

/// @brief プレイヤーオブジェクト
///
/// 円形コライダーを持つプレイヤー。重力によって落下し、
/// スラスト入力とジャンプで移動する。ライフ制で被弾管理。
class Player final : public IGameObject
{
public:
	/// @brief デフォルトの重力加速度（ピクセル/秒^2）
	static constexpr float DEFAULT_GRAVITY = 980.0f;
	/// @brief デフォルトの移動速度（ピクセル/秒）
	static constexpr float DEFAULT_SPEED = 300.0f;
	/// @brief デフォルトのジャンプ力（ピクセル/秒）
	static constexpr float DEFAULT_JUMP_FORCE = 500.0f;
	/// @brief デフォルトの最大水平速度（ピクセル/秒）
	static constexpr float DEFAULT_MAX_VEL_X = 400.0f;
	/// @brief デフォルトの半径
	static constexpr float DEFAULT_RADIUS = 15.0f;
	/// @brief デフォルトのライフ数
	static constexpr int DEFAULT_LIVES = 3;

	/// @brief プレイヤーを構築する
	/// @param pos 初期位置
	/// @param radius 円の半径
	explicit Player(sgc::Vec2f pos, float radius = DEFAULT_RADIUS)
		: m_position(pos)
		, m_radius(radius)
	{
	}

	/// @brief フレーム更新（重力適用・速度積分・速度クランプ）
	/// @param dt デルタタイム（秒）
	void update(float dt) override
	{
		if (!m_active)
		{
			return;
		}

		// 重力を適用
		m_velocity.y += m_gravity * dt;

		// 加速度を速度に反映
		m_velocity += m_acceleration * dt;

		// 水平速度をクランプ
		m_velocity.x = std::clamp(m_velocity.x, -m_maxVelX, m_maxVelX);

		// 位置を更新
		m_position += m_velocity * dt;

		// 加速度をリセット
		m_acceleration = sgc::Vec2f{0.0f, 0.0f};
	}

	/// @brief 方向入力によるスラスト（加速度）を適用する
	/// @param direction 入力方向（正規化推奨）
	void applyThrust(sgc::Vec2f direction)
	{
		m_acceleration += direction * m_speed;
	}

	/// @brief ジャンプ（接地時のみ有効）
	void jump()
	{
		if (m_isGrounded)
		{
			m_velocity.y = -m_jumpForce;
			m_isGrounded = false;
			m_isJumping = true;
		}
	}

	/// @brief ダメージを受ける（ライフ減少）
	void takeDamage()
	{
		if (m_lives > 0)
		{
			--m_lives;
		}
	}

	/// @brief 指定位置にリスポーンする
	/// @param pos リスポーン位置
	void respawn(sgc::Vec2f pos)
	{
		m_position = pos;
		m_velocity = sgc::Vec2f{0.0f, 0.0f};
		m_acceleration = sgc::Vec2f{0.0f, 0.0f};
		m_isGrounded = false;
		m_isJumping = false;
	}

	/// @brief 死亡判定
	/// @return ライフが0以下ならtrue
	[[nodiscard]] bool isDead() const { return m_lives <= 0; }

	// ── IGameObject実装 ─────────────────────────────────────

	[[nodiscard]] sgc::Vec2f getPosition() const override { return m_position; }
	void setPosition(sgc::Vec2f pos) override { m_position = pos; }
	[[nodiscard]] ObjectType getType() const override { return ObjectType::Player; }

	[[nodiscard]] sgc::Rectf getBounds() const override
	{
		return sgc::Rectf{
			m_position.x - m_radius,
			m_position.y - m_radius,
			m_radius * 2.0f,
			m_radius * 2.0f
		};
	}

	[[nodiscard]] bool isActive() const override { return m_active; }
	void setActive(bool active) override { m_active = active; }

	// ── アクセサ ────────────────────────────────────────────

	/// @brief 速度を取得する
	[[nodiscard]] sgc::Vec2f getVelocity() const { return m_velocity; }
	/// @brief 速度を設定する
	void setVelocity(sgc::Vec2f vel) { m_velocity = vel; }

	/// @brief 接地状態を取得する
	[[nodiscard]] bool isGrounded() const { return m_isGrounded; }
	/// @brief 接地状態を設定する
	void setGrounded(bool grounded) { m_isGrounded = grounded; }

	/// @brief ジャンプ中か取得する
	[[nodiscard]] bool isJumping() const { return m_isJumping; }

	/// @brief 残りライフ数を取得する
	[[nodiscard]] int getLives() const { return m_lives; }
	/// @brief ライフ数を設定する
	void setLives(int lives) { m_lives = lives; }

	/// @brief 半径を取得する
	[[nodiscard]] float getRadius() const { return m_radius; }

	/// @brief 重力を設定する
	void setGravity(float g) { m_gravity = g; }
	/// @brief 移動速度を設定する
	void setSpeed(float s) { m_speed = s; }
	/// @brief ジャンプ力を設定する
	void setJumpForce(float f) { m_jumpForce = f; }
	/// @brief 最大水平速度を設定する
	void setMaxVelX(float v) { m_maxVelX = v; }

private:
	sgc::Vec2f m_position{};                           ///< 中心位置
	sgc::Vec2f m_velocity{};                           ///< 速度
	sgc::Vec2f m_acceleration{};                       ///< 加速度（フレームごとにリセット）
	float m_radius{DEFAULT_RADIUS};                    ///< 半径
	float m_gravity{DEFAULT_GRAVITY};                  ///< 重力加速度
	float m_speed{DEFAULT_SPEED};                      ///< 移動速度
	float m_jumpForce{DEFAULT_JUMP_FORCE};             ///< ジャンプ力
	float m_maxVelX{DEFAULT_MAX_VEL_X};                ///< 最大水平速度
	bool m_isGrounded{false};                          ///< 接地フラグ
	bool m_isJumping{false};                           ///< ジャンプ中フラグ
	int m_lives{DEFAULT_LIVES};                        ///< 残りライフ
	bool m_active{true};                               ///< アクティブフラグ
};

} // namespace mitiru::gw
