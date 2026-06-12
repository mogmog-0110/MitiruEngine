#pragma once

/// @file Collide2D.hpp
/// @brief タイルマップに対する AABB 移動解決 (moveAndCollide)。全 2D ゲームが手書きする
///        衝突スイープを関数 1 個に。物理エンジンではない (反発・質量・スロープなし)。
/// @details
/// 定番の軸分離スイープ: X 軸を動かして当たったら壁面に密着 → Y 軸も同様。
/// 移動量が大きくてもスイープ範囲の全タイルを走査するので 1 フレームで貫通しない。
///
/// - 座標系は y 下向き正 (screen 系)。hitDown = 着地。
/// - solid(tileX, tileY) はそのタイルが壁なら true。範囲外の扱いも solid 側が決める。
/// - 負座標タイルも正しく解決する (添字は std::floor — 整数除算の負方向切り捨てを踏まない)。
/// - w/h がタイルより大きい場合は跨る全行・全列を走査する。
/// - ぴったり接触は「壁」: その向きに動こうとすると変位 0 で hit フラグが立つ
///   (接地中に毎フレーム重力を足しても沈まず hitDown が立ち続ける)。
///   ただし移動量 0 の軸は走査しない (dy=0 で接地していても hitDown は立てない)。
/// - 開始時点で既にめり込んでいる solid は対象外 (押し出さない)。配置側の責務。
/// - 決定論: 浮動小数の演算順・走査順は固定。同じ入力なら結果は bit 一致 (リプレイ前提)。
///
/// 使用例 (プラットフォーマー、GameMemory の座標を渡して結果を書き戻すだけ):
/// @code
///   g.vy += kGravity;
///   const auto r = mitiru::moveAndCollide(g.px, g.py, 12.0f, 14.0f, g.vx, g.vy, 16.0f,
///       [&](int tx, int ty) { return g.stage.solid(tx, ty); });  // 範囲外の扱いも stage 側で
///   g.px = r.x;
///   g.py = r.y;
///   if (r.hitDown || r.hitUp) { g.vy = 0.0f; }
///   g.onGround = r.hitDown;
/// @endcode

#include <cmath>

namespace mitiru
{

/// @brief moveAndCollide の結果。x/y は解決後の位置 (rect 左上)。
struct MoveResult
{
	float x = 0.0f;
	float y = 0.0f;
	bool  hitLeft  = false;  ///< 左面で止まった (dx<0 が遮られた)
	bool  hitRight = false;  ///< 右面で止まった (dx>0 が遮られた)
	bool  hitUp    = false;  ///< 上面で止まった (dy<0 が遮られた = 天井)
	bool  hitDown  = false;  ///< 下面で止まった (dy>0 が遮られた = 着地)
};

namespace collide2d_detail
{
	/// @brief v が属するタイル添字。負座標も floor で正しく丸める。
	inline int tileFloor(float v, float tileSize) noexcept
	{
		return static_cast<int>(std::floor(v / tileSize));
	}

	/// @brief 半開区間 [lo, hi) の終端 hi が跨ぐ最後のタイル。境界ちょうどなら手前タイル。
	inline int lastTileBefore(float hi, float tileSize) noexcept
	{
		const int t = tileFloor(hi, tileSize);
		return (static_cast<float>(t) * tileSize >= hi) ? t - 1 : t;
	}

	/// @brief 左端 (上端) が lo 以上にある最初のタイル。
	inline int firstTileAtOrAfter(float lo, float tileSize) noexcept
	{
		const int t = tileFloor(lo, tileSize);
		return (static_cast<float>(t) * tileSize < lo) ? t + 1 : t;
	}

	/// @brief X 軸スイープ。遮られたら壁面に密着した x を、自由なら x+dx を返す。
	template <typename SolidFn>
	inline float sweepX(float x, float y, float w, float h, float dx,
	                    float tileSize, SolidFn& solid, bool& hitLeft, bool& hitRight)
	{
		const int ty0 = tileFloor(y, tileSize);
		const int ty1 = lastTileBefore(y + h, tileSize);
		if (dx > 0.0f)
		{
			// 右移動: 右端より先にある solid 列の左面を近い順に探す
			const float startRight = x + w;
			const int   txFirst    = firstTileAtOrAfter(startRight, tileSize);
			const int   txLast     = lastTileBefore(startRight + dx, tileSize);
			for (int tx = txFirst; tx <= txLast; ++tx)
				for (int ty = ty0; ty <= ty1; ++ty)
					if (solid(tx, ty))
					{
						hitRight = true;
						return static_cast<float>(tx) * tileSize - w;
					}
		}
		else
		{
			// 左移動: 左端より手前にある solid 列の右面を近い順に探す
			const int txFirst = tileFloor(x, tileSize) - 1;
			const int txLast  = tileFloor(x + dx, tileSize);
			for (int tx = txFirst; tx >= txLast; --tx)
				for (int ty = ty0; ty <= ty1; ++ty)
					if (solid(tx, ty))
					{
						hitLeft = true;
						return static_cast<float>(tx + 1) * tileSize;
					}
		}
		return x + dx;
	}

	/// @brief Y 軸スイープ。x は X 解決後の値を渡す。
	template <typename SolidFn>
	inline float sweepY(float x, float y, float w, float h, float dy,
	                    float tileSize, SolidFn& solid, bool& hitUp, bool& hitDown)
	{
		const int tx0 = tileFloor(x, tileSize);
		const int tx1 = lastTileBefore(x + w, tileSize);
		if (dy > 0.0f)
		{
			// 落下: 下端より先にある solid 行の上面 = 床
			const float startBottom = y + h;
			const int   tyFirst     = firstTileAtOrAfter(startBottom, tileSize);
			const int   tyLast      = lastTileBefore(startBottom + dy, tileSize);
			for (int ty = tyFirst; ty <= tyLast; ++ty)
				for (int tx = tx0; tx <= tx1; ++tx)
					if (solid(tx, ty))
					{
						hitDown = true;
						return static_cast<float>(ty) * tileSize - h;
					}
		}
		else
		{
			// 上昇: 上端より手前にある solid 行の下面 = 天井
			const int tyFirst = tileFloor(y, tileSize) - 1;
			const int tyLast  = tileFloor(y + dy, tileSize);
			for (int ty = tyFirst; ty >= tyLast; --ty)
				for (int tx = tx0; tx <= tx1; ++tx)
					if (solid(tx, ty))
					{
						hitUp = true;
						return static_cast<float>(ty + 1) * tileSize;
					}
		}
		return y + dy;
	}
}  // namespace collide2d_detail

/// @brief AABB (x, y, w, h) を (dx, dy) 動かし、solid タイルで止める。X → Y の軸分離。
/// @param solid (tileX, tileY) → そのタイルが壁なら true。ラムダ / 関数オブジェクト可。
/// @return 解決後の位置と、どの面で止まったかのフラグ。
template <typename SolidFn>
inline MoveResult moveAndCollide(float x, float y, float w, float h,
                                 float dx, float dy, float tileSize, SolidFn&& solid)
{
	MoveResult r{};
	r.x = x + dx;
	r.y = y + dy;
	if (!(tileSize > 0.0f)) { return r; }  // 不正 tileSize: 添字が壊れるので衝突なし扱い
	if (dx != 0.0f) { r.x = collide2d_detail::sweepX(x, y, w, h, dx, tileSize, solid, r.hitLeft, r.hitRight); }
	if (dy != 0.0f) { r.y = collide2d_detail::sweepY(r.x, y, w, h, dy, tileSize, solid, r.hitUp, r.hitDown); }
	return r;
}

/// @brief solid 判定の関数ポインタ形 (C 流)。ctx に任意のユーザーデータを渡す。
using TileSolidFn = bool (*)(int tileX, int tileY, void* ctx);

/// @brief 関数ポインタ + ctx 版。挙動はテンプレート版と同一。
inline MoveResult moveAndCollide(float x, float y, float w, float h,
                                 float dx, float dy, float tileSize,
                                 TileSolidFn solid, void* ctx)
{
	return moveAndCollide(x, y, w, h, dx, dy, tileSize,
	                      [&](int tx, int ty) { return solid(tx, ty, ctx); });
}

}  // namespace mitiru
