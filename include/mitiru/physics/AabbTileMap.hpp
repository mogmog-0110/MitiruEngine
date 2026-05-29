#pragma once

/// @file AabbTileMap.hpp
/// @brief AABB を タイルマップ + 動的 extras に対して X→Y 分離 sweep で動かす。
/// @details 全プラットフォーマーが似たような moveBody を自前実装する pattern を共通化。
///          v1: solid + jump-through + extras。slope は follow-up (v2)。

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include <sgc/math/Rect.hpp>

namespace mitiru::physics
{

/// @brief moveAabbInTileMap の設定。tileSolid は必須、jumpThrough は省略可、extras は動的箱/扉用。
struct TileMapMoveOpts
{
	float tileW = 16.0f;
	float tileH = 16.0f;
	/// @brief 必須: (tx,ty) のタイルが solid なら true。範囲外でも問題なし (game が境界を判断)。
	std::function<bool(int tx, int ty)> tileSolid;
	/// @brief 省略可: 上からのみ着地する jump-through 床なら true。
	std::function<bool(int tx, int ty)> tileJumpThrough;
	/// @brief 動的に追加される solid 矩形 (drawer box / door 等)。
	std::vector<sgc::Rectf> extras;
};

/// @brief sweep の結果。
struct MoveResult
{
	sgc::Rectf out;             ///< 解決後の AABB
	bool collidedX = false;     ///< X 方向に何かにぶつかった
	bool collidedY = false;     ///< Y 方向に何かにぶつかった
	bool landed    = false;     ///< Y 方向に下向きでぶつかった (= 接地)
};

namespace detail
{
	inline int floorDiv(float v, float div) noexcept
	{
		return static_cast<int>(std::floor(v / div));
	}

	/// @brief rect が占める tile index 範囲 [tx0..tx1, ty0..ty1] (inclusive)。
	inline void tileRangeForRect(const sgc::Rectf& r, float tileW, float tileH,
	                             int& tx0, int& ty0, int& tx1, int& ty1) noexcept
	{
		const float right  = r.x() + r.width()  - 1e-4f;  // 1 px の sliver で隣タイルを誤拾いしないよう僅かに縮める
		const float bottom = r.y() + r.height() - 1e-4f;
		tx0 = floorDiv(r.x(),    tileW);
		ty0 = floorDiv(r.y(),    tileH);
		tx1 = floorDiv(right,    tileW);
		ty1 = floorDiv(bottom,   tileH);
	}
}

/// @brief AABB を (dx, dy) 動かす。X→Y 分離で X collide → snap → Y collide → snap → return。
inline MoveResult moveAabbInTileMap(const sgc::Rectf& in, float dx, float dy,
                                    const TileMapMoveOpts& opts)
{
	using detail::tileRangeForRect;
	MoveResult r;
	r.out = in;

	auto isSolidTile = [&](int tx, int ty) -> bool
	{
		return opts.tileSolid && opts.tileSolid(tx, ty);
	};
	auto isJumpThrough = [&](int tx, int ty) -> bool
	{
		return opts.tileJumpThrough && opts.tileJumpThrough(tx, ty);
	};

	// ── X 軸 ─────────────────────────────────────────────────────────
	{
		const sgc::Rectf trial{r.out.x() + dx, r.out.y(), r.out.width(), r.out.height()};
		// swept = in と trial の union (tile を tunneling しないよう全 sweep 範囲を scan)
		const sgc::Rectf swept{
			std::min(r.out.x(), trial.x()),
			trial.y(),
			r.out.width() + std::fabs(dx),
			trial.height()
		};
		int tx0, ty0, tx1, ty1;
		tileRangeForRect(swept, opts.tileW, opts.tileH, tx0, ty0, tx1, ty1);
		const float bodyStartRight = r.out.x() + r.out.width();
		const float bodyStartLeft  = r.out.x();

		if (dx > 0.0f)
		{
			float snapRight = trial.x() + trial.width();
			for (int ty = ty0; ty <= ty1; ++ty)
			for (int tx = tx0; tx <= tx1; ++tx)
			{
				if (!isSolidTile(tx, ty)) { continue; }
				const float left = tx * opts.tileW;
				if (left >= bodyStartRight - 1e-4f && left < snapRight) { snapRight = left; }
			}
			for (const auto& e : opts.extras)
			{
				// extras は trial の vertical 範囲と重なるもののみ
				if (!(e.y() < trial.y() + trial.height() && e.y() + e.height() > trial.y())) { continue; }
				if (e.x() >= bodyStartRight - 1e-4f && e.x() < snapRight) { snapRight = e.x(); }
			}
			const float newRight = std::min(trial.x() + trial.width(), snapRight);
			const float newX     = newRight - trial.width();
			if (newX < trial.x() - 1e-4f) { r.collidedX = true; }
			r.out = sgc::Rectf{newX, r.out.y(), r.out.width(), r.out.height()};
		}
		else if (dx < 0.0f)
		{
			float snapLeft = trial.x();
			for (int ty = ty0; ty <= ty1; ++ty)
			for (int tx = tx0; tx <= tx1; ++tx)
			{
				if (!isSolidTile(tx, ty)) { continue; }
				const float right = (tx + 1) * opts.tileW;
				if (right <= bodyStartLeft + 1e-4f && right > snapLeft) { snapLeft = right; }
			}
			for (const auto& e : opts.extras)
			{
				if (!(e.y() < trial.y() + trial.height() && e.y() + e.height() > trial.y())) { continue; }
				const float er = e.x() + e.width();
				if (er <= bodyStartLeft + 1e-4f && er > snapLeft) { snapLeft = er; }
			}
			const float newX = std::max(trial.x(), snapLeft);
			if (newX > trial.x() + 1e-4f) { r.collidedX = true; }
			r.out = sgc::Rectf{newX, r.out.y(), r.out.width(), r.out.height()};
		}
	}

	// ── Y 軸 ─────────────────────────────────────────────────────────
	{
		const sgc::Rectf trial{r.out.x(), r.out.y() + dy, r.out.width(), r.out.height()};
		const sgc::Rectf swept{
			trial.x(),
			std::min(r.out.y(), trial.y()),
			trial.width(),
			r.out.height() + std::fabs(dy)
		};
		int tx0, ty0, tx1, ty1;
		tileRangeForRect(swept, opts.tileW, opts.tileH, tx0, ty0, tx1, ty1);
		const float bodyStartBottom = r.out.y() + r.out.height();
		const float bodyStartTop    = r.out.y();

		if (dy > 0.0f)
		{
			float snapBottom = trial.y() + trial.height();
			for (int ty = ty0; ty <= ty1; ++ty)
			for (int tx = tx0; tx <= tx1; ++tx)
			{
				if (isSolidTile(tx, ty))
				{
					const float top = ty * opts.tileH;
					if (top >= bodyStartBottom - 1e-4f && top < snapBottom) { snapBottom = top; }
				}
				else if (isJumpThrough(tx, ty))
				{
					const float top = ty * opts.tileH;
					if (bodyStartBottom <= top + 1e-4f && top < snapBottom) { snapBottom = top; }
				}
			}
			for (const auto& e : opts.extras)
			{
				if (!(e.x() < r.out.x() + r.out.width() && e.x() + e.width() > r.out.x())) { continue; }
				if (e.y() >= bodyStartBottom - 1e-4f && e.y() < snapBottom) { snapBottom = e.y(); }
			}
			const float newBottom = std::min(trial.y() + trial.height(), snapBottom);
			const float newY      = newBottom - trial.height();
			if (newY < trial.y() - 1e-4f) { r.collidedY = true; r.landed = true; }
			r.out = sgc::Rectf{r.out.x(), newY, r.out.width(), r.out.height()};
		}
		else if (dy < 0.0f)
		{
			float snapTop = trial.y();
			for (int ty = ty0; ty <= ty1; ++ty)
			for (int tx = tx0; tx <= tx1; ++tx)
			{
				if (!isSolidTile(tx, ty)) { continue; }
				const float bottom = (ty + 1) * opts.tileH;
				if (bottom <= bodyStartTop + 1e-4f && bottom > snapTop) { snapTop = bottom; }
			}
			for (const auto& e : opts.extras)
			{
				if (!(e.x() < r.out.x() + r.out.width() && e.x() + e.width() > r.out.x())) { continue; }
				const float eb = e.y() + e.height();
				if (eb <= bodyStartTop + 1e-4f && eb > snapTop) { snapTop = eb; }
			}
			const float newY = std::max(trial.y(), snapTop);
			if (newY > trial.y() + 1e-4f) { r.collidedY = true; }
			r.out = sgc::Rectf{r.out.x(), newY, r.out.width(), r.out.height()};
		}
	}

	return r;
}

}  // namespace mitiru::physics
