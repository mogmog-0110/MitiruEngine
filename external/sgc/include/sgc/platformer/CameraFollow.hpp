#pragma once

/// @file CameraFollow.hpp
/// @brief 2Dカメラ追従
///
/// プラットフォーマーゲームのカメラ追従を提供する。
/// デッドゾーン・スムージング・先読み・ワールド境界クランプを実装する。
///
/// @code
/// using namespace sgc::platformer;
/// CameraFollowConfig config;
/// config.smoothing = 0.1f;
/// config.deadZone = {-20, -20, 40, 40};
/// sgc::Vec2f camPos{0, 0};
/// sgc::Vec2f target{100, 50};
/// camPos = updateCamera(camPos, target, config, 1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <optional>

#include "sgc/math/Rect.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::platformer
{

/// @brief カメラ追従設定
struct CameraFollowConfig
{
	float smoothing = 0.1f;                           ///< スムージング係数（0〜1、小さいほど遅延大）
	sgc::Rectf deadZone{-16.0f, -16.0f, 32.0f, 32.0f};  ///< デッドゾーン（カメラ中心からの相対矩形）
	float lookAhead = 0.0f;                           ///< 先読み距離（進行方向にオフセット）
	float verticalBias = 0.0f;                        ///< 垂直バイアス（正で下方向にずらす）
	std::optional<sgc::Rectf> worldBounds;            ///< ワールド境界（クランプ用、nulloptなら無制限）
};

/// @brief カメラ位置を更新する
///
/// ターゲット追従・デッドゾーン・先読み・スムージング・境界クランプを
/// 統合的に処理し、更新後のカメラ位置を返す。
///
/// @param currentPos 現在のカメラ位置
/// @param targetPos ターゲット（プレイヤー）位置
/// @param config カメラ設定
/// @param dt デルタタイム（秒）
/// @return 更新後のカメラ位置
[[nodiscard]] inline sgc::Vec2f updateCamera(
	const sgc::Vec2f& currentPos,
	const sgc::Vec2f& targetPos,
	const CameraFollowConfig& config,
	float dt)
{
	// デッドゾーン計算（カメラ中心からの相対座標）
	const float diffX = targetPos.x - currentPos.x;
	const float diffY = targetPos.y - currentPos.y;

	float desiredX = currentPos.x;
	float desiredY = currentPos.y;

	// デッドゾーンの左端・右端
	const float dzLeft = config.deadZone.position.x;
	const float dzRight = config.deadZone.position.x + config.deadZone.size.x;
	const float dzTop = config.deadZone.position.y;
	const float dzBottom = config.deadZone.position.y + config.deadZone.size.y;

	// X軸: デッドゾーン外ならターゲットに追従
	if (diffX < dzLeft)
	{
		desiredX = targetPos.x - dzLeft;
	}
	else if (diffX > dzRight)
	{
		desiredX = targetPos.x - dzRight;
	}

	// Y軸: デッドゾーン外ならターゲットに追従
	if (diffY < dzTop)
	{
		desiredY = targetPos.y - dzTop;
	}
	else if (diffY > dzBottom)
	{
		desiredY = targetPos.y - dzBottom;
	}

	// 先読みオフセット（X軸方向のみ）
	if (std::abs(config.lookAhead) > 0.001f)
	{
		const float dir = (diffX > 0.0f) ? 1.0f : ((diffX < 0.0f) ? -1.0f : 0.0f);
		desiredX += dir * config.lookAhead;
	}

	// 垂直バイアス
	desiredY += config.verticalBias;

	// スムージング（指数的減衰）
	const float t = 1.0f - std::pow(1.0f - config.smoothing, dt * 60.0f);
	float newX = currentPos.x + (desiredX - currentPos.x) * t;
	float newY = currentPos.y + (desiredY - currentPos.y) * t;

	// ワールド境界クランプ
	if (config.worldBounds.has_value())
	{
		const auto& bounds = config.worldBounds.value();
		newX = std::clamp(newX,
			bounds.position.x,
			bounds.position.x + bounds.size.x);
		newY = std::clamp(newY,
			bounds.position.y,
			bounds.position.y + bounds.size.y);
	}

	return {newX, newY};
}

} // namespace sgc::platformer
