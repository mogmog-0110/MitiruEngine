// game.cpp。ゲーム本体。毎フレーム update で動かし、draw で描く。
#include "game.hpp"
#include "sprites.hpp"

#include <cmath>
#include <cstdio>
#include <type_traits>

// 状態を RML / RCSS の HUD（assets/ui/main.rml）へ送る。{{ }} と data-class が受け取る。
void BekoRun::pushHud(Hud hud) const
{
	hud.set("view.coins", coins);
	hud.set("view.total", NCOIN);
	hud.set("view.cleared", cleared ? 1 : 0);
}

void BekoRun::update(Input in, Hud hud, float dt)
{
	if (t <= 0.0f) {   // 初回フレームだけ敵をデータから配置（最初は左へ歩き出す）。t はまだ 0 のまま
		// 一時オブジェクトの代入 (foes[i] = {...}) は bool の後ろの padding が不定のまま
		// コピーされ、録画と再生で GameMemory の byte が一致しなくなる。field ごとに書く。
		for (int i = 0; i < NFOE; ++i) {
			Foe& f = foes[i];
			f.x = kFoes[i].x; f.y = kFoes[i].y; f.t = static_cast<float>(i) * 0.7f; f.vx = -FOE_SPEED; f.alive = true;
		}
	}
	t += dt;
	if (cleared) { clearT += dt; pushHud(hud); return; }
	if (hurt > 0.0f) { hurt -= dt; }

	// 前フレームで積まれた被弾を処理する (MsgQueue の慣用句。当たった瞬間ではなく次の update
	// 冒頭でまとめて反映するので、同フレーム中に複数の敵と重なっても一度しか跳ねない)。
	dmgQueue.drain([&](const DamageMsg& m) {
		if (hurt <= 0.0f) { hurt = 1.2f; if (vy > m.knockVy) { vy = m.knockVy; } }
	});

	// 縦：ジャンプ → 重力 → 足場に着地。縦を先に解くと薄い段にもきちんと乗れる。
	if (onGround && in.pressed(Key::Space)) { vy = -JUMP; }
	vy += GRAV * dt;
	py += vy * dt;
	onGround = false;
	for (int i = 0; i < NPLAT; ++i) {   // 上から乗る／下から頭ぶつけ
		const Plat& p = kPlats[i];
		if (hitBox(p.x, p.y, p.w, p.h)) {
			if (vy > 0.0f) { py = p.y - PH * 0.5f; vy = 0.0f; onGround = true; }
			else if (vy < 0.0f) { py = p.y + p.h + PH * 0.5f; vy = 0.0f; }
		}
	}

	// 横：移動（矢印/WASD/パッドを 1 本にまとめた move().x）とブロックの側面
	const float mx = in.move().x;
	vx = mx * SPEED;
	if (mx < -0.1f) { faceLeft = true; } else if (mx > 0.1f) { faceLeft = false; }
	px += vx * dt;
	for (int i = 0; i < NPLAT; ++i) {   // ブロックの側面で止める
		const Plat& p = kPlats[i];
		if (hitBox(p.x, p.y, p.w, p.h)) {
			if (vx > 0.0f) { px = p.x - PW * 0.5f; } else if (vx < 0.0f) { px = p.x + p.w + PW * 0.5f; }
		}
	}
	px = clampf(px, PW * 0.5f, WORLD_W - PW * 0.5f);

	// 歩行アニメ（地面を動く時だけコマ送り、空中は 1 枚）
	if (onGround && std::fabs(vx) > 10.0f) { if (walk.every(0.12f, dt)) { step = (step + 1) % 4; } }
	else if (!onGround) { step = 2; } else { step = 0; }

	// 敵。上から踏めば倒す、横から当たると戻される。
	const float kh = static_cast<float>(kKoboshi.height()) * 0.36f;
	const float kw = static_cast<float>(kKoboshi.width()) * 0.36f;
	for (int i = 0; i < NFOE; ++i) {
		Foe& f = foes[i]; if (!f.alive) { continue; } f.t += dt;
		// 左右に往復。初期位置から FOE_RANGE を超えたら折り返す。
		f.x += f.vx * dt;
		if (f.x < kFoes[i].x - FOE_RANGE) { f.x = kFoes[i].x - FOE_RANGE; f.vx = -f.vx; }
		else if (f.x > kFoes[i].x + FOE_RANGE) { f.x = kFoes[i].x + FOE_RANGE; f.vx = -f.vx; }
		const float fx = f.x - kw * 0.32f, fy = f.y - kh * 0.72f, fw = kw * 0.64f, fh = kh * 0.72f;
		if (hitBox(fx, fy, fw, fh)) {
			if (vy > 0.0f && (py + PH * 0.5f) < f.y - kh * 0.35f) { f.alive = false; vy = -JUMP * 0.6f; }  // 上から踏む
			else if (hurt <= 0.0f) { (void)dmgQueue.push(DamageMsg{ -260.0f }); }                          // 横当たり: 次フレームで跳ねる
		}
	}

	// 古銭を集める
	for (int i = 0; i < NCOIN; ++i) {
		if (coinGot[i]) { continue; }
		if (hitBox(kCoins[i].x - COIN_DISP * 0.5f, kCoins[i].y - COIN_DISP * 0.5f, COIN_DISP, COIN_DISP)) {
			coinGot[i] = true; ++coins;
		}
	}

	if (px > GOAL_X) { cleared = true; }                          // ゴール判定
	camX = clampf(px, SCR_W * 0.5f, WORLD_W - SCR_W * 0.5f);       // カメラ横追従
	pushHud(hud);                                                 // 古銭カウンタ等を HUD へ
}

// Screen と Canvas は同名メソッドを持つ (ADR 0025) ので、描画本体は 1 本のテンプレートで
// 両方に実体化できる。draw(Screen&) が既存の Mode A 経路、draw(Canvas&) が POD コマンド経路。
template <class Surface>
static void drawBekoRun(const BekoRun& g, Surface& s)
{
	// Canvas 経路 (ADR 0025): id を登録すると以後の drawSprite(texture,...) が
	// アドレス直運びの Sprite ではなく id ベースの SpriteRectById を積む (H-1/H-4 継続分)。
	// id は kBeko 等の読み込み元ファイル名 (sprites.hpp / assets/sprites/*.png) と一致させる。
	if constexpr (std::is_same_v<Surface, mitiru::Canvas>)
	{
		s.registerTexture(kGoal, "goal_nobori");
		s.registerTexture(kCoin, "coin");
		s.registerTexture(kKoboshi, "koboshi");
		s.registerTexture(kStone, "stone");
		s.registerTexture(kBeko[0], "akabeko_0");
		s.registerTexture(kBeko[1], "akabeko_1");
		s.registerTexture(kBeko[2], "akabeko_2");
		s.registerTexture(kBeko[3], "akabeko_3");
	}
	s.drawGradientRect(Rect{0.0f, 0.0f, SCR_W, SCR_H}, hex(0xBFE0F5), hex(0xF3F6FA));   // 空
	s.applyCamera(g.camX, SCR_H * 0.5f);   // camX を画面中央に = 横スクロール

	// ゴールの幟。石畳より先に描くと、地面が竿の根元を覆って刺さって見える。
	const float gw = static_cast<float>(kGoal.width()) * 0.9f, gh = static_cast<float>(kGoal.height()) * 0.9f;
	s.drawSprite(kGoal, Rect{GOAL_X - gw * 0.35f, GROUND_Y + 8.0f - gh, gw, gh}, whole(kGoal));

	for (int i = 0; i < NPLAT; ++i) { drawStone(s, kPlats[i]); }   // 石畳

	// 古銭（上下に揺れ、横幅を伸縮させて回転風に）
	for (int i = 0; i < NCOIN; ++i) {
		if (g.coinGot[i]) { continue; }
		const float bob = std::sin(g.t * 3.0f + static_cast<float>(i)) * 5.0f;
		const float cw = COIN_DISP * (0.35f + 0.65f * std::fabs(std::cos(g.t * 2.4f + static_cast<float>(i))));
		s.drawSprite(kCoin, Rect{kCoins[i].x - cw * 0.5f, kCoins[i].y - COIN_DISP * 0.5f + bob, cw, COIN_DISP}, whole(kCoin));
	}

	// 敵（ゆらゆら弾む）
	// ADR 0035 O2/O3 追記: foes[i].x/y は `MITIRU_REFLECT_AUTO` で "foes.<i>.x"/"foes.<i>.y" に
	// 平坦化される (Foe が x/y を持つ struct ではなく、直接の scalar 2 個のため)。単一の
	// name+".x"/".y" 規約では拾えないので `beginObject(name, fieldX, fieldY)` の明示版を使う。
	const float kw = static_cast<float>(kKoboshi.width()) * 0.36f, kh = static_cast<float>(kKoboshi.height()) * 0.36f;
	for (int i = 0; i < NFOE; ++i) {
		const Foe& f = g.foes[i]; if (!f.alive) { continue; }
		char nameBuf[16], fxBuf[24], fyBuf[24];
		if constexpr (std::is_same_v<Surface, mitiru::Canvas>) {
			std::snprintf(nameBuf, sizeof(nameBuf), "foes.%d", i);
			std::snprintf(fxBuf, sizeof(fxBuf), "foes.%d.x", i);
			std::snprintf(fyBuf, sizeof(fyBuf), "foes.%d.y", i);
			s.beginObject(nameBuf, fxBuf, fyBuf);
		}
		const float bob = std::sin(f.t * 3.0f) * 4.0f;
		s.drawSprite(kKoboshi, Rect{f.x - kw * 0.5f, f.y - kh + bob, kw, kh}, whole(kKoboshi));
		if constexpr (std::is_same_v<Surface, mitiru::Canvas>) { s.endObject(); }
	}

	// 赤べこ（進む向きに反転、被弾中は点滅）
	// ADR 0035 O2: 分岐エディタのシーンビューが枠を出せるよう、player の描画区間だけ
	// beginObject/endObject で reflect フィールド名をタグ付けする (Canvas 経路のみ。
	// Screen には無いメソッドなので if constexpr で分ける、registerTexture と同じ手法)。
	// px/py は分離 scalar (vec2 構造でない) なので明示 fieldX/fieldY 版を使う (ADR 0035 O3 追記)。
	if constexpr (std::is_same_v<Surface, mitiru::Canvas>) { s.beginObject("beko", "px", "py"); }
	const render::Texture& tex = kBeko[g.step];
	const float bw = static_cast<float>(tex.width()) * 0.44f, bh = static_cast<float>(tex.height()) * 0.44f;
	const float a = (g.hurt > 0.0f && (static_cast<int>(g.hurt * 20.0f) % 2 == 0)) ? 0.45f : 1.0f;
	s.drawSprite(tex, Rect{g.px - bw * 0.5f, g.py - bh * 0.5f, bw, bh}, whole(tex),
	             color::White.withAlpha(a), g.faceLeft);
	if constexpr (std::is_same_v<Surface, mitiru::Canvas>) { s.endObject(); }

	s.endCamera();
	// 古銭カウンタと「クリア！」は RML / RCSS の HUD（assets/ui/main.rml）が描く。
}

void BekoRun::draw(Screen& s) const { drawBekoRun(*this, s); }
void BekoRun::draw(Canvas& c) const { drawBekoRun(*this, c); }
