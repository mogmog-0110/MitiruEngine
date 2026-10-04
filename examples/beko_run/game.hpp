// game.hpp。ゲームの状態（GameMemory）と入口の宣言。
// 状態はすべて数値だけで構成した 1 つの struct。処理の中身は game.cpp に書く。
#pragma once
#include <cstdint>

#include <mitiru.hpp>
#include <mitiru/module/PodQueue.hpp>

#include "level.hpp"

using namespace mitiru;

// 敵 1 体の状態。vx は左右の速さ（＋で右、−で左）。alive の後ろに残る 3 byte は
// 明示的な pad にしてある (bool 1 個だけでは compiler が暗黙に詰める部分が一時オブジェクトへの代入時に
// 不定値になり、録画再生の bit-exact 照合に失敗する。GameMemory padding 検出の対象外になる)。
struct Foe { float x, y, t, vx; std::uint8_t alive; std::uint8_t _pad[3]; };

// 敵との横当たりで入れる「要求」。当たった瞬間には hurt/vy を書き換えず、次の update 冒頭で
// まとめて処理する (MsgQueue でよく使う形。宛先を持たない core::EventBus の代わり)。
struct DamageMsg { float knockVy; };

// フィールド順は「4 byte 単位のもの → bool 群」でまとめてある (compiler が挿入する padding を
// 0 にするため。bool を数値の間に混ぜると alignment のための埋めが発生する)。
struct BekoRun
{
	float px = 120.0f, py = GROUND_Y - PH * 0.5f;   // 赤べこの中心
	float vx = 0.0f, vy = 0.0f;
	Timer walk; int step = 0;
	Foe   foes[NFOE] = {};
	float camX = SCR_W * 0.5f;
	float clearT = 0.0f; float hurt = 0.0f; float t = 0.0f;
	int   coins = 0;
	// hurt のクールダウン中は再度ヒットとして扱わないため、同じフレーム中に複数の敵と重なっても
	// 実際に入れるのは最大 1 件 (容量 1 で足りる)。
	mitiru::MsgQueue<DamageMsg, 1> dmgQueue;

	bool  onGround = false, faceLeft = false;
	bool  cleared = false;
	bool  coinGot[NCOIN] = {};
	std::uint8_t _pad[3] = {};   // 末尾を 4 byte 境界に揃える明示 pad

	void update(Input in, Hud hud, float dt);   // 毎フレームの動き（game.cpp）
	void draw(Screen& s) const;                 // 毎フレームの描画（game.cpp）
	void draw(Canvas& c) const;                 // 同上 (game.cpp で drawBekoRun を共有)
	void pushHud(Hud hud) const;                // 状態を RML / RCSS の HUD へ送る（game.cpp）

	// 小さな補助処理（当たり判定と範囲制限）
	static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
	static bool box(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
		return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
	}
	bool hitBox(float bx, float by, float bw, float bh) const {   // 赤べこと矩形の重なり
		return box(px - PW * 0.5f, py - PH * 0.5f, PW, PH, bx, by, bw, bh);
	}
};
