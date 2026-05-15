#pragma once

/// @file Collision2D.hpp
/// @brief 2D衝突判定ユーティリティ関数群

#include <optional>
#include <cmath>
#include <algorithm>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>

namespace mitiru::util
{
	/// @brief 矩形同士の重なり判定（AABB）
	/// @param a 矩形A
	/// @param b 矩形B
	/// @return 重なっていればtrue
	[[nodiscard]] inline bool overlapsRectRect(const sgc::Rectf& a, const sgc::Rectf& b) noexcept
	{
		return a.x() < b.x() + b.width()
			&& a.x() + a.width() > b.x()
			&& a.y() < b.y() + b.height()
			&& a.y() + a.height() > b.y();
	}

	/// @brief 円同士の重なり判定
	/// @param c1 円1の中心座標
	/// @param r1 円1の半径
	/// @param c2 円2の中心座標
	/// @param r2 円2の半径
	/// @return 重なっていればtrue
	[[nodiscard]] inline bool overlapsCircleCircle(
		sgc::Vec2f c1, float r1,
		sgc::Vec2f c2, float r2) noexcept
	{
		const float dx = c2.x - c1.x;
		const float dy = c2.y - c1.y;
		const float distSq = dx * dx + dy * dy;
		const float radiusSum = r1 + r2;
		return distSq < radiusSum * radiusSum;
	}

	/// @brief 円と矩形の重なり判定
	/// @param center 円の中心座標
	/// @param radius 円の半径
	/// @param rect 矩形
	/// @return 重なっていればtrue
	[[nodiscard]] inline bool overlapsCircleRect(
		sgc::Vec2f center, float radius,
		const sgc::Rectf& rect) noexcept
	{
		// 円の中心を矩形の範囲にクランプし、その点との距離で判定
		const float clampedX = std::clamp(center.x, rect.x(), rect.x() + rect.width());
		const float clampedY = std::clamp(center.y, rect.y(), rect.y() + rect.height());
		const float dx = center.x - clampedX;
		const float dy = center.y - clampedY;
		return (dx * dx + dy * dy) < (radius * radius);
	}

	/// @brief 点が矩形内にあるか判定
	/// @param p 判定する点
	/// @param rect 矩形
	/// @return 矩形内であればtrue
	[[nodiscard]] inline bool pointInRect(sgc::Vec2f p, const sgc::Rectf& rect) noexcept
	{
		return p.x >= rect.x()
			&& p.x <= rect.x() + rect.width()
			&& p.y >= rect.y()
			&& p.y <= rect.y() + rect.height();
	}

	/// @brief 点が円内にあるか判定
	/// @param p 判定する点
	/// @param center 円の中心座標
	/// @param radius 円の半径
	/// @return 円内であればtrue
	[[nodiscard]] inline bool pointInCircle(
		sgc::Vec2f p,
		sgc::Vec2f center,
		float radius) noexcept
	{
		const float dx = p.x - center.x;
		const float dy = p.y - center.y;
		return (dx * dx + dy * dy) < (radius * radius);
	}

	/// @brief AABB同士の押し出しベクトルを計算する
	/// @param mover 移動する側の矩形
	/// @param obstacle 障害物の矩形
	/// @return 重なっている場合は最小の押し出しベクトル、重なっていなければnullopt
	[[nodiscard]] inline std::optional<sgc::Vec2f> resolveAABB(
		const sgc::Rectf& mover,
		const sgc::Rectf& obstacle) noexcept
	{
		// 各軸の重なり量を計算
		const float overlapLeft   = (mover.x() + mover.width()) - obstacle.x();
		const float overlapRight  = (obstacle.x() + obstacle.width()) - mover.x();
		const float overlapTop    = (mover.y() + mover.height()) - obstacle.y();
		const float overlapBottom = (obstacle.y() + obstacle.height()) - mover.y();

		// いずれかの軸で重なりがなければ衝突していない
		if (overlapLeft <= 0.0f || overlapRight <= 0.0f ||
			overlapTop <= 0.0f || overlapBottom <= 0.0f)
		{
			return std::nullopt;
		}

		// 最小の重なり軸を選択して押し出す
		float minOverlap = overlapLeft;
		sgc::Vec2f pushOut{ -overlapLeft, 0.0f };

		if (overlapRight < minOverlap)
		{
			minOverlap = overlapRight;
			pushOut = sgc::Vec2f{ overlapRight, 0.0f };
		}
		if (overlapTop < minOverlap)
		{
			minOverlap = overlapTop;
			pushOut = sgc::Vec2f{ 0.0f, -overlapTop };
		}
		if (overlapBottom < minOverlap)
		{
			pushOut = sgc::Vec2f{ 0.0f, overlapBottom };
		}

		return pushOut;
	}

} // namespace mitiru::util
