#pragma once

/// @file FollowCam.hpp
/// @brief 2D follow camera: deadzone + horizontal lookahead + exponential ease + world clamp。
/// @details 横スクロール作家全員が自前実装する pattern を共通化。game は毎フレーム setTarget /
///          setFacing して update(dt) を呼ぶだけ。`view()` で screen.setCamera に渡せる原点を得る。

#include <algorithm>
#include <cmath>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>

namespace mitiru::camera
{

/// @brief FollowCam の挙動パラメータ。game が好きに書き換えて良い (per-scene 切替可)。
struct FollowCamConfig
{
	float deadzoneHalfW = 32.0f;  ///< 横デッドゾーン半幅 (px)
	float deadzoneHalfH = 16.0f;  ///< 縦デッドゾーン半高 (px)
	float lookaheadX    = 0.0f;   ///< facing 方向への先読みオフセット (px)
	float ease          = 6.0f;   ///< 指数 ease の強さ。0=瞬時、大=滑らか。
	bool  clamp         = false;  ///< clampToWorld 有効化
	sgc::Rectf  worldBounds {};   ///< clamp 時のワールド境界 (camera center が外に出ない)
	float viewW         = 1280.0f;
	float viewH         = 720.0f;
};

class FollowCam
{
public:
	FollowCamConfig cfg;
	sgc::Vec2f      pos{0.0f, 0.0f};        ///< 現在の camera 中心
	sgc::Vec2f      targetPos{0.0f, 0.0f};  ///< 追従対象 (game が毎フレーム書く)
	float           facing = 0.0f;          ///< -1 = 左、+1 = 右 (lookahead 用)

	void setTarget(float x, float y) noexcept { targetPos = sgc::Vec2f{x, y}; }
	void setFacing(float d)          noexcept { facing = d; }

	/// @brief 1 フレーム camera を更新する。
	void update(float dt) noexcept
	{
		// 1. lookahead を加味した目標 desired (target + facing * lookahead)
		const float desiredX = targetPos.x + facing * cfg.lookaheadX;
		const float desiredY = targetPos.y;

		// 2. デッドゾーン適用: |desired - pos| <= deadzone なら動かない、超えたら edge に揃える。
		float wantX = pos.x;
		if (desiredX > pos.x + cfg.deadzoneHalfW)      { wantX = desiredX - cfg.deadzoneHalfW; }
		else if (desiredX < pos.x - cfg.deadzoneHalfW) { wantX = desiredX + cfg.deadzoneHalfW; }
		float wantY = pos.y;
		if (desiredY > pos.y + cfg.deadzoneHalfH)      { wantY = desiredY - cfg.deadzoneHalfH; }
		else if (desiredY < pos.y - cfg.deadzoneHalfH) { wantY = desiredY + cfg.deadzoneHalfH; }

		// 3. 指数 ease: pos += (want - pos) * (1 - exp(-ease * dt))
		const float k = (cfg.ease > 0.0f) ? (1.0f - std::exp(-cfg.ease * dt)) : 1.0f;
		pos.x += (wantX - pos.x) * k;
		pos.y += (wantY - pos.y) * k;

		// 4. world bounds clamp (view が外に出ないよう camera 中心を制限)
		if (cfg.clamp) { clampToBounds(); }
	}

	/// @brief デッドゾーン無視で camera を target に直接合わせる (シーン開始 / teleport)。
	void snapToTarget() noexcept
	{
		pos = sgc::Vec2f{targetPos.x + facing * cfg.lookaheadX, targetPos.y};
		if (cfg.clamp) { clampToBounds(); }
	}

	/// @brief 画面左上ワールド座標 (screen.setCamera に渡せる)。
	[[nodiscard]] sgc::Vec2f viewTopLeft() const noexcept
	{
		return sgc::Vec2f{pos.x - cfg.viewW * 0.5f, pos.y - cfg.viewH * 0.5f};
	}

private:
	void clampToBounds() noexcept
	{
		const float minX = cfg.worldBounds.x() + cfg.viewW * 0.5f;
		const float maxX = cfg.worldBounds.x() + cfg.worldBounds.width() - cfg.viewW * 0.5f;
		const float minY = cfg.worldBounds.y() + cfg.viewH * 0.5f;
		const float maxY = cfg.worldBounds.y() + cfg.worldBounds.height() - cfg.viewH * 0.5f;
		// view が world より大きい場合は min/max が反転するので center を world 中心に固定。
		if (minX > maxX) { pos.x = cfg.worldBounds.x() + cfg.worldBounds.width()  * 0.5f; }
		else             { pos.x = std::clamp(pos.x, minX, maxX); }
		if (minY > maxY) { pos.y = cfg.worldBounds.y() + cfg.worldBounds.height() * 0.5f; }
		else             { pos.y = std::clamp(pos.y, minY, maxY); }
	}
};

}  // namespace mitiru::camera
