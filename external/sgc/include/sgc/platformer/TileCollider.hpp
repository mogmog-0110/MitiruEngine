#pragma once

/// @file TileCollider.hpp
/// @brief AABB vs タイルマップ衝突判定
///
/// AABB（軸平行境界ボックス）とタイルマップの衝突判定を提供する。
/// スウィープテストによる連続衝突検出を実装する。
///
/// @code
/// using namespace sgc::platformer;
/// sgc::Rectf aabb{10.0f, 10.0f, 16.0f, 16.0f};
/// sgc::Vec2f velocity{100.0f, 50.0f};
/// auto query = [](int tx, int ty) { return ty > 10; };
/// auto result = sweepAABB(aabb, velocity, query, 16.0f, 1.0f / 60.0f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "sgc/math/Rect.hpp"
#include "sgc/math/Vec2.hpp"

namespace sgc::platformer
{

/// @brief スウィープ衝突判定結果
struct SweepResult
{
	bool hit = false;                ///< 衝突が発生したか
	sgc::Vec2f normal{};             ///< 衝突面の法線
	float toi = 1.0f;               ///< 衝突までの時間割合 [0, 1]
	sgc::Vec2<int> tileCoord{};     ///< 衝突したタイルの座標
};

/// @brief タイル問い合わせ関数型
///
/// タイル座標(x, y)に固体タイルがあるかを返す。
using TileQuery = std::function<bool(int tileX, int tileY)>;

/// @brief AABBとタイルマップのスウィープ衝突テスト
///
/// AABBが速度ベクトル方向に移動する際、最初にぶつかるタイルを検出する。
/// ブロードフェーズで移動範囲のタイルを列挙し、各タイルとAABBの
/// スウィープテストを行う。
///
/// @param aabb AABB矩形（位置は左上、サイズは幅×高さ）
/// @param velocity 速度ベクトル（ピクセル/秒）
/// @param query タイル固体判定関数
/// @param tileSize タイル1辺のサイズ（ピクセル）
/// @param dt デルタタイム（秒）
/// @return スウィープ結果
[[nodiscard]] inline SweepResult sweepAABB(
	const sgc::Rectf& aabb,
	const sgc::Vec2f& velocity,
	const TileQuery& query,
	float tileSize,
	float dt)
{
	SweepResult result;
	result.toi = 1.0f;

	const float dx = velocity.x * dt;
	const float dy = velocity.y * dt;

	// 移動量がゼロなら衝突なし
	if (std::abs(dx) < 1e-6f && std::abs(dy) < 1e-6f)
	{
		return result;
	}

	// ブロードフェーズ: 移動範囲のタイル範囲を算出
	const float minX = std::min(aabb.position.x, aabb.position.x + dx);
	const float maxX = std::max(aabb.position.x + aabb.size.x, aabb.position.x + aabb.size.x + dx);
	const float minY = std::min(aabb.position.y, aabb.position.y + dy);
	const float maxY = std::max(aabb.position.y + aabb.size.y, aabb.position.y + aabb.size.y + dy);

	const int tMinX = static_cast<int>(std::floor(minX / tileSize));
	const int tMaxX = static_cast<int>(std::floor(maxX / tileSize));
	const int tMinY = static_cast<int>(std::floor(minY / tileSize));
	const int tMaxY = static_cast<int>(std::floor(maxY / tileSize));

	// 各タイルとのスウィープテスト
	for (int ty = tMinY; ty <= tMaxY; ++ty)
	{
		for (int tx = tMinX; tx <= tMaxX; ++tx)
		{
			if (!query(tx, ty))
			{
				continue;
			}

			// タイルのAABB
			const float tileLeft = static_cast<float>(tx) * tileSize;
			const float tileTop = static_cast<float>(ty) * tileSize;
			const float tileRight = tileLeft + tileSize;
			const float tileBottom = tileTop + tileSize;

			// ミンコフスキー拡張: AABBサイズ分タイルを拡大
			const float eLeft = tileLeft - aabb.size.x;
			const float eTop = tileTop - aabb.size.y;
			const float eRight = tileRight;
			const float eBottom = tileBottom;

			// 点（AABB左上）vs 拡張矩形 のスウィープテスト
			const float px = aabb.position.x;
			const float py = aabb.position.y;

			float tEntry = 0.0f;
			float tExit = 1.0f;
			sgc::Vec2f entryNormal{};

			// X軸
			if (std::abs(dx) > 1e-6f)
			{
				const float invDx = 1.0f / dx;
				const float t1 = (eLeft - px) * invDx;
				const float t2 = (eRight - px) * invDx;
				const float tEntryX = std::min(t1, t2);
				const float tExitX = std::max(t1, t2);

				if (tEntryX > tEntry)
				{
					tEntry = tEntryX;
					entryNormal = {(dx > 0.0f) ? -1.0f : 1.0f, 0.0f};
				}
				tExit = std::min(tExit, tExitX);
			}
			else
			{
				// 静止: 範囲外なら衝突なし
				if (px <= eLeft || px >= eRight)
				{
					continue;
				}
			}

			// Y軸
			if (std::abs(dy) > 1e-6f)
			{
				const float invDy = 1.0f / dy;
				const float t1 = (eTop - py) * invDy;
				const float t2 = (eBottom - py) * invDy;
				const float tEntryY = std::min(t1, t2);
				const float tExitY = std::max(t1, t2);

				if (tEntryY > tEntry)
				{
					tEntry = tEntryY;
					entryNormal = {0.0f, (dy > 0.0f) ? -1.0f : 1.0f};
				}
				tExit = std::min(tExit, tExitY);
			}
			else
			{
				if (py <= eTop || py >= eBottom)
				{
					continue;
				}
			}

			// 衝突判定
			if (tEntry < tExit && tEntry >= 0.0f && tEntry < result.toi)
			{
				result.hit = true;
				result.toi = tEntry;
				result.normal = entryNormal;
				result.tileCoord = {tx, ty};
			}
		}
	}

	return result;
}

} // namespace sgc::platformer
