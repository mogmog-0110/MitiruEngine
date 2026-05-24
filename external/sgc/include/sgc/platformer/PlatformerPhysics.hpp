#pragma once

/// @file PlatformerPhysics.hpp
/// @brief 2Dプラットフォーマー物理
///
/// プラットフォーマーゲームのキャラクター物理を提供する。
/// 重力・ジャンプ・コヨーテタイム・ジャンプバッファリングを実装する。
///
/// @code
/// using namespace sgc::platformer;
/// PlatformerConfig config;
/// PlatformerBody body;
/// body.position = {100.0f, 200.0f};
/// PlatformerInput input{0.0f, true, false};
/// body = updatePlatformerBody(body, config, input, 1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cmath>

#include "sgc/math/Vec2.hpp"

namespace sgc::platformer
{

/// @brief プラットフォーマー入力
struct PlatformerInput
{
	float moveX = 0.0f;          ///< 水平入力 [-1, 1]
	bool jumpPressed = false;    ///< ジャンプボタンが押された瞬間
	bool jumpHeld = false;       ///< ジャンプボタンが押し続けられている
};

/// @brief プラットフォーマー物理設定
struct PlatformerConfig
{
	float gravity = 980.0f;           ///< 重力加速度（ピクセル/秒^2）
	float maxFallSpeed = 600.0f;      ///< 最大落下速度
	float walkSpeed = 200.0f;         ///< 歩行速度
	float jumpForce = 400.0f;         ///< ジャンプ初速
	float coyoteTime = 0.1f;          ///< コヨーテタイム（秒）
	float jumpBufferTime = 0.1f;      ///< ジャンプバッファ時間（秒）
	float jumpCutMultiplier = 0.5f;   ///< ジャンプキャンセル時の速度倍率
	float airControlFactor = 0.8f;    ///< 空中での操作性倍率
};

/// @brief プラットフォーマーキャラクターの物理状態
struct PlatformerBody
{
	sgc::Vec2f position{};          ///< 位置
	sgc::Vec2f velocity{};          ///< 速度
	bool grounded = false;          ///< 接地中か
	bool onWall = false;            ///< 壁に接触中か
	bool facingRight = true;        ///< 右を向いているか
	float coyoteTimer = 0.0f;       ///< コヨーテタイムの残り時間
	float jumpBufferTimer = 0.0f;   ///< ジャンプバッファの残り時間
	bool jumping = false;           ///< ジャンプ中か
};

/// @brief プラットフォーマー物理を更新する
///
/// 重力・水平移動・ジャンプ・コヨーテタイム・ジャンプバッファを
/// 統合的に処理し、更新後のBodyを返す。
///
/// @param prev 前フレームの状態
/// @param config 物理設定
/// @param input 入力情報
/// @param dt デルタタイム（秒）
/// @return 更新後の物理状態
[[nodiscard]] inline PlatformerBody updatePlatformerBody(
	const PlatformerBody& prev,
	const PlatformerConfig& config,
	const PlatformerInput& input,
	float dt)
{
	PlatformerBody next = prev;

	// ── コヨーテタイム管理 ──
	if (next.grounded)
	{
		next.coyoteTimer = config.coyoteTime;
	}
	else
	{
		next.coyoteTimer = std::max(0.0f, next.coyoteTimer - dt);
	}

	// ── ジャンプバッファ管理 ──
	if (input.jumpPressed)
	{
		next.jumpBufferTimer = config.jumpBufferTime;
	}
	else
	{
		next.jumpBufferTimer = std::max(0.0f, next.jumpBufferTimer - dt);
	}

	// ── 水平移動 ──
	const float speedFactor = next.grounded ? 1.0f : config.airControlFactor;
	next.velocity.x = input.moveX * config.walkSpeed * speedFactor;

	// 向き更新
	if (input.moveX > 0.01f)
	{
		next.facingRight = true;
	}
	else if (input.moveX < -0.01f)
	{
		next.facingRight = false;
	}

	// ── ジャンプ処理 ──
	const bool canJump = next.coyoteTimer > 0.0f;
	const bool wantsJump = next.jumpBufferTimer > 0.0f;

	if (canJump && wantsJump)
	{
		next.velocity.y = -config.jumpForce;
		next.coyoteTimer = 0.0f;
		next.jumpBufferTimer = 0.0f;
		next.jumping = true;
		next.grounded = false;
	}

	// ── ジャンプキャンセル（可変ジャンプ高さ） ──
	if (next.jumping && !input.jumpHeld && next.velocity.y < 0.0f)
	{
		next.velocity.y *= config.jumpCutMultiplier;
		next.jumping = false;
	}

	// ── 重力適用 ──
	if (!next.grounded)
	{
		next.velocity.y += config.gravity * dt;
		next.velocity.y = std::min(next.velocity.y, config.maxFallSpeed);
	}

	// ── 位置更新 ──
	next.position.x += next.velocity.x * dt;
	next.position.y += next.velocity.y * dt;

	// 着地判定（ジャンプ中の下方向速度がゼロ以上で接地した場合）
	if (next.grounded && next.velocity.y >= 0.0f)
	{
		next.jumping = false;
	}

	return next;
}

} // namespace sgc::platformer
