// sprites.hpp。画像を読み込み、石畳を描画する。
// 画像は可変長データなので状態 struct には入れず、ここで一度だけ読み込む。
#pragma once
#include <algorithm>

#include <mitiru.hpp>
#include <mitiru/render/Texture.hpp>

#include "level.hpp"

using namespace mitiru;

// 画像を読み込む。失敗時は value_or で空画像を返す。
inline render::Texture loadTex(const char* p) {
	return render::Texture::fromFile(p).value_or(render::Texture{});
}

inline const render::Texture kBeko[4] = {
	loadTex("beko_run/assets/sprites/akabeko_0.png"), loadTex("beko_run/assets/sprites/akabeko_1.png"),
	loadTex("beko_run/assets/sprites/akabeko_2.png"), loadTex("beko_run/assets/sprites/akabeko_3.png"),
};
inline const render::Texture kKoboshi = loadTex("beko_run/assets/sprites/koboshi.png");      // 敵
inline const render::Texture kGoal    = loadTex("beko_run/assets/sprites/goal_nobori.png");  // ゴールの幟
inline const render::Texture kStone   = loadTex("beko_run/assets/sprites/stone.png");        // 石畳タイル
inline const render::Texture kCoin    = loadTex("beko_run/assets/sprites/coin.png");          // 古銭

// 画像まるごとを指す src 矩形。
inline Rect whole(const render::Texture& t) {
	return Rect{0.0f, 0.0f, static_cast<float>(t.width()), static_cast<float>(t.height())};
}

// 石畳ブロックを 1 枚描く。横方向にタイルを敷き、地面の厚みは暗い石で埋める。
// Screen / Canvas の両方に実体化する (ADR 0025、game.cpp の drawBekoRun と同じ理由)。
template <class Surface>
inline void drawStone(Surface& s, const Plat& p) {
	const float srcW = static_cast<float>(kStone.width()), srcH = static_cast<float>(kStone.height());
	const float bandH = std::min(p.h, 56.0f);
	const float tileW = srcW * (bandH / srcH);          // 縦横比を保ったタイル幅
	for (float x = p.x; x < p.x + p.w - 0.5f; x += tileW) {
		const float w = std::min(tileW, p.x + p.w - x);
		s.drawSprite(kStone, Rect{x, p.y, w, bandH}, Rect{0.0f, 0.0f, srcW * (w / tileW), srcH});
	}
	if (p.h > bandH) { s.drawRect(Rect{p.x, p.y + bandH, p.w, p.h - bandH}, hex(0x6E635A)); }
}
