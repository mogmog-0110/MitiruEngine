// showcase_platformer — 旗艦サンプル (ADR 0005 / 0007 / 0012 / 0013 の参照実装)
//
// 散在する engine capability が 1 本のジャンルの読めるゲームに組み上がることを示す:
//   gameplay:  横スクロール / 重力 / ジャンプ / AABB↔タイル衝突 / 踏みつけ / ゴール
//   collision: physics::moveAabbInTileMap
//   camera:    camera::FollowCam (deadzone + lookahead + ease + clamp)
//   juice:     juice::Shake / HitStop / Particles
//   side fx:   FrameIntents::soundIntents (host が鳴らす。game は mixer を持たない)
//   HUD:       view.hud.* push → zero-JS scene.html (data-m-*)
//   determinism: GameMemory は flat POD で、host が memorySize で replay に記録する
//                → Player::diffState が「どの frame から分岐したか」を特定できる (ADR 0013)
//
// engine code は一切追加していない。既存ヘッダを組むだけ。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <sgc/math/Rect.hpp>

#include <mitiru/module/Game.hpp>
#include <mitiru/physics/AabbTileMap.hpp>
#include <mitiru/camera/FollowCam.hpp>
#include <mitiru/juice/Juice.hpp>

namespace showcase
{

// ── 定数 ────────────────────────────────────────────────────────────────────
constexpr float kTile      = 32.0f;
constexpr float kRunSpeed  = 230.0f;
constexpr float kGravity   = 1500.0f;
constexpr float kMaxFall   = 920.0f;
constexpr float kJumpVel   = 560.0f;
constexpr float kBounceVel = 380.0f;   // 踏みつけ後の小ジャンプ
constexpr float kEnemySpeed = 70.0f;
constexpr float kPlayerW   = 22.0f;
constexpr float kPlayerH   = 30.0f;
constexpr float kEnemyW    = 26.0f;
constexpr float kEnemyH    = 26.0f;

constexpr float kDefaultScreenW = 1280.0f;
constexpr float kDefaultScreenH = 720.0f;

// ── レベル (const data、state ではない) ─────────────────────────────────────
// '#'=solid, '='=jump-through 床, 'P'=player spawn, 'E'=enemy spawn, 'G'=goal, '.'=空
constexpr int kCols = 48;
constexpr int kRows = 16;
constexpr const char* kLevel[kRows] = {
	"................................................",
	"................................................",
	"................................................",
	"..........................========..............",
	"................................................",
	"...............===........................G.....",
	"..........E.........................######......",
	".....P........#####........E....................",
	"#####....##..........###...........###..........",
	"....#....##.......................##............",
	"....#....##....E.........###....................",
	"....#....#####......#####.....#####.....E.......",
	"....#..............................#............",
	"....#######....######....##########.....########",
	"..............#.....................##..........",
	"################################################",
};

// 行長に依存せず安全に読む (短い行は '.' 扱い)。直読み (kLevel[ty][tx]) は行が
// kCols 未満だと過読みになるので、全アクセスはこの関数経由にする。
[[nodiscard]] inline char tileAt(int tx, int ty) noexcept
{
	if (tx < 0 || ty < 0 || ty >= kRows) { return '.'; }
	const char* row = kLevel[ty];
	return (tx < static_cast<int>(std::strlen(row))) ? row[tx] : '.';
}
[[nodiscard]] inline bool isSolid(int tx, int ty) noexcept { return tileAt(tx, ty) == '#'; }
[[nodiscard]] inline bool isJumpThrough(int tx, int ty) noexcept
{
	const char c = tileAt(tx, ty);
	return c == '=';
}

// ── データ (flat POD — heap pointer を持たない。host が memcpy 記録する) ──────
struct Enemy
{
	float x = 0.0f, y = 0.0f;
	float vx = -kEnemySpeed;
	bool  alive = false;
};

struct PlatformerMemory
{
	float px = 0.0f, py = 0.0f;     // player AABB top-left (world)
	float pvx = 0.0f, pvy = 0.0f;
	bool  onGround = false;
	int   facing   = 1;

	static constexpr int kMaxEnemies = 12;
	Enemy enemies[kMaxEnemies] {};
	int   enemyCount = 0;

	float goalX = 0.0f, goalY = 0.0f;
	float spawnX = 0.0f, spawnY = 0.0f;  // restart 用

	int   score      = 0;
	int   stompCount = 0;
	int   state      = 0;            // 0=playing, 1=won, 2=lost

	mitiru::juice::Shake   shake;
	mitiru::juice::HitStop hitStop;
	mitiru::camera::FollowCam cam;

	float screenW = kDefaultScreenW;
	float screenH = kDefaultScreenH;
	std::uint32_t frame = 0;
	bool  initialized = false;

	// HUD diff cache (毎フレーム push せず変化時のみ)
	int   lastScore = -1;
	int   lastStomp = -1;
	int   lastState = -1;

	// エンジンが呼ぶ入口 (実装はヘルパ定義の後)。
	void init();
	void update(mitiru::Input in, mitiru::Hud hud, float dt);
	void draw(mitiru::Screen& screen);
};

// flat POD であること (= GameMemory を host が memcpy 記録できる不変条件) は、末尾の
// MITIRU_GAME_RECORDABLE が compile time に保証する。非 POD メンバを足すと compile error。

// 視覚専用 scratch (vector 内包 = flat でないので GameMemory には入れない。
// replay では同じ event 列から決定論的に再生される)。
mitiru::juice::Particles g_particles{384};

// HUD への値送りと効果音は hud.set / hud.play を使う (Game.hpp、固定長スロット詰めは隠れる)。

[[nodiscard]] inline sgc::Rectf playerRect(const PlatformerMemory& m)
{
	return sgc::Rectf{m.px, m.py, kPlayerW, kPlayerH};
}
[[nodiscard]] inline bool aabbOverlap(const sgc::Rectf& a, const sgc::Rectf& b)
{
	return a.x() < b.x() + b.width() && a.x() + a.width() > b.x() &&
	       a.y() < b.y() + b.height() && a.y() + a.height() > b.y();
}

// ── レベルから初期状態を組む (init / restart 共通) ──────────────────────────
void resetLevel(PlatformerMemory& m)
{
	m.enemyCount = 0;
	for (int ty = 0; ty < kRows; ++ty)
	for (int tx = 0; tx < kCols; ++tx)
	{
		const char c = tileAt(tx, ty);
		const float wx = tx * kTile;
		const float wy = ty * kTile;
		if (c == 'P') { m.spawnX = wx + (kTile - kPlayerW) * 0.5f; m.spawnY = wy + (kTile - kPlayerH); }
		else if (c == 'G') { m.goalX = wx; m.goalY = wy; }
		else if (c == 'E' && m.enemyCount < PlatformerMemory::kMaxEnemies)
		{
			Enemy& e = m.enemies[m.enemyCount++];
			e.x = wx + (kTile - kEnemyW) * 0.5f;
			e.y = wy + (kTile - kEnemyH);
			e.vx = -kEnemySpeed;
			e.alive = true;
		}
	}
	m.px = m.spawnX; m.py = m.spawnY;
	m.pvx = m.pvy = 0.0f;
	m.onGround = false;
	m.facing = 1;
	m.score = 0;
	m.stompCount = 0;
	m.state = 0;
	m.cam.snapToTarget();
}

void initGame(PlatformerMemory& m)
{
	m.cam.cfg.deadzoneHalfW = 80.0f;
	m.cam.cfg.deadzoneHalfH = 60.0f;
	m.cam.cfg.lookaheadX    = 60.0f;
	m.cam.cfg.ease          = 8.0f;
	m.cam.cfg.clamp         = true;
	m.cam.cfg.worldBounds   = sgc::Rectf{0.0f, 0.0f, kCols * kTile, kRows * kTile};
	resetLevel(m);
	m.cam.setTarget(m.px + kPlayerW * 0.5f, m.py + kPlayerH * 0.5f);
	m.cam.snapToTarget();
	m.initialized = true;
}

void stepGame(PlatformerMemory& m, mitiru::Input in, mitiru::Hud hud, float dt)
{
	using mitiru::Key;
	if (in.pressed(Key::Escape)) { hud.quit(); }

	// restart (ゲームオーバー/クリア時): R キー or CEF ボタン (action "game.restart")
	const bool wantRestart = in.pressed(Key::R) || in.action("game.restart");

	if (m.state != 0)
	{
		if (wantRestart) { resetLevel(m); }
		m.shake.update(dt);
		g_particles.update(dt);
		// HUD は下の push 区間で更新
	}
	else
	{
		// hit-stop 中は gameplay を止める (juice だけ進める)
		const bool frozen = m.hitStop.active();
		m.hitStop.update(dt);

		if (!frozen)
		{
			const bool left  = in.down(Key::Left) || in.down(Key::A);
			const bool right = in.down(Key::Right) || in.down(Key::D);
			const bool jumpPressed = in.pressed(Key::Up) ||
			                         in.pressed(Key::Space) ||
			                         in.pressed(Key::W);

			m.pvx = (right ? kRunSpeed : 0.0f) - (left ? kRunSpeed : 0.0f);
			if (right) { m.facing = 1; }
			else if (left) { m.facing = -1; }

			if (jumpPressed && m.onGround) { m.pvy = -kJumpVel; m.onGround = false; hud.play("jump"); }

			m.pvy += kGravity * dt;
			if (m.pvy > kMaxFall) { m.pvy = kMaxFall; }

			// player を tilemap に対して sweep
			mitiru::physics::TileMapMoveOpts opts;
			opts.tileW = kTile; opts.tileH = kTile;
			opts.tileSolid       = [](int tx, int ty) { return isSolid(tx, ty); };
			opts.tileJumpThrough = [](int tx, int ty) { return isJumpThrough(tx, ty); };
			const auto res = mitiru::physics::moveAabbInTileMap(
				playerRect(m), m.pvx * dt, m.pvy * dt, opts);
			m.px = res.out.x(); m.py = res.out.y();
			m.onGround = res.landed;
			if (res.collidedY) { m.pvy = 0.0f; }

			// enemies: 単純パトロール (壁/端で反転)
			for (int i = 0; i < m.enemyCount; ++i)
			{
				Enemy& e = m.enemies[i];
				if (!e.alive) { continue; }
				const float nx = e.x + e.vx * dt;
				// 進行方向直下に床が無い or 正面が solid なら反転
				const int footTx = static_cast<int>((nx + (e.vx < 0 ? 0.0f : kEnemyW)) / kTile);
				const int footTy = static_cast<int>((e.y + kEnemyH + 1.0f) / kTile);
				const int faceTx = static_cast<int>((nx + (e.vx < 0 ? 0.0f : kEnemyW)) / kTile);
				const int faceTy = static_cast<int>((e.y + kEnemyH * 0.5f) / kTile);
				if (!isSolid(footTx, footTy) || isSolid(faceTx, faceTy)) { e.vx = -e.vx; }
				else { e.x = nx; }
			}

			// 衝突判定: 踏みつけ vs 横当たり
			const sgc::Rectf pr = playerRect(m);
			for (int i = 0; i < m.enemyCount; ++i)
			{
				Enemy& e = m.enemies[i];
				if (!e.alive) { continue; }
				const sgc::Rectf er{e.x, e.y, kEnemyW, kEnemyH};
				if (!aabbOverlap(pr, er)) { continue; }
				const bool fromAbove = m.pvy > 0.0f && (pr.y() + pr.height()) < (er.y() + kEnemyH * 0.6f);
				if (fromAbove)
				{
					e.alive = false;
					m.pvy = -kBounceVel;
					m.score += 100;
					++m.stompCount;
					g_particles.burst(e.x + kEnemyW * 0.5f, e.y + kEnemyH * 0.5f,
					                  14, 180.0f, 0.45f, 4.0f, sgc::Colorf{0.78f, 0.0f, 0.17f, 1.0f});
					m.shake.pushTrauma(0.45f);
					m.hitStop.trigger(0.05f);
					hud.play("stomp");
				}
				else
				{
					m.state = 2;  // 横から触れた = lose
					m.shake.pushTrauma(0.7f);
					hud.play("hit");
				}
			}

			// ゴール到達
			const sgc::Rectf goal{m.goalX, m.goalY, kTile, kTile * 2.0f};
			if (m.state == 0 && aabbOverlap(pr, sgc::Rectf{m.goalX, m.goalY - kTile, kTile, kTile * 2.0f}))
			{
				m.state = 1;  // win
				hud.play("goal");
			}

			// 落下死 (world 下端より下)
			if (m.py > kRows * kTile + 64.0f) { m.state = 2; hud.play("hit"); }
		}

		// camera 追従
		m.cam.cfg.viewW = m.screenW;
		m.cam.cfg.viewH = m.screenH;
		m.cam.cfg.worldBounds = sgc::Rectf{0.0f, 0.0f, kCols * kTile, kRows * kTile};
		m.cam.setTarget(m.px + kPlayerW * 0.5f, m.py + kPlayerH * 0.5f);
		m.cam.setFacing(static_cast<float>(m.facing));
		m.cam.update(dt);

		m.shake.update(dt);
		g_particles.update(dt);
		++m.frame;
	}

	// ── HUD push (変化時のみ、zero-JS scene.html が bind) ─────────────────
	if (m.score != m.lastScore) { hud.set("view.hud.score", m.score); m.lastScore = m.score; }
	if (m.stompCount != m.lastStomp) { hud.set("view.hud.stomps", m.stompCount); m.lastStomp = m.stompCount; }
	if (m.state != m.lastState)
	{
		hud.set("view.hud.over", m.state != 0);
		hud.set("view.hud.outcome", m.state == 1 ? "win" : (m.state == 2 ? "lose" : ""));
		m.lastState = m.state;
	}
}

void drawGame(PlatformerMemory& m, mitiru::Screen& screen)
{
	m.screenW = static_cast<float>(screen.width());
	m.screenH = static_cast<float>(screen.height());

	const auto tl = m.cam.viewTopLeft();
	const auto sh = m.shake.offset();
	const float camX = tl.x + sh.x;
	const float camY = tl.y + sh.y;

	// 空 (全画面)
	screen.drawRect(sgc::Rectf{0.0f, 0.0f, m.screenW, m.screenH},
	                 sgc::Colorf{0.62f, 0.78f, 0.90f, 1.0f});
	// 遠景パララックス (cam を 0.35x で減衰させた帯 = 奥行き、texture 不要)
	const float hillY = m.screenH * 0.55f - camY * 0.35f;
	screen.drawRect(sgc::Rectf{0.0f, hillY, m.screenW, m.screenH},
	                 sgc::Colorf{0.55f, 0.72f, 0.62f, 1.0f});

	// tilemap (cam 減算、solid=ink / jump-through=帯)
	for (int ty = 0; ty < kRows; ++ty)
	for (int tx = 0; tx < kCols; ++tx)
	{
		const char c = tileAt(tx, ty);
		if (c != '#' && c != '=') { continue; }
		const float dx = tx * kTile - camX;
		const float dy = ty * kTile - camY;
		if (dx + kTile < 0 || dx > m.screenW) { continue; }  // 簡易カリング
		if (c == '#') { screen.drawRect(sgc::Rectf{dx, dy, kTile, kTile}, sgc::Colorf{0.18f, 0.20f, 0.24f, 1.0f}); }
		else          { screen.drawRect(sgc::Rectf{dx, dy + 2.0f, kTile, 6.0f}, sgc::Colorf{0.40f, 0.30f, 0.20f, 1.0f}); }
	}

	// ゴール (旗)
	screen.drawRect(sgc::Rectf{m.goalX - camX + kTile * 0.4f, m.goalY - camY - kTile, 4.0f, kTile * 2.0f},
	                 sgc::Colorf{0.10f, 0.10f, 0.10f, 1.0f});
	screen.drawRect(sgc::Rectf{m.goalX - camX + kTile * 0.4f, m.goalY - camY - kTile, kTile * 0.5f, kTile * 0.4f},
	                 sgc::Colorf{0.95f, 0.78f, 0.10f, 1.0f});

	// enemies
	for (int i = 0; i < m.enemyCount; ++i)
	{
		const Enemy& e = m.enemies[i];
		if (!e.alive) { continue; }
		screen.drawRect(sgc::Rectf{e.x - camX, e.y - camY, kEnemyW, kEnemyH},
		                 sgc::Colorf{0.78f, 0.0f, 0.17f, 1.0f});
	}

	// player
	screen.drawRect(sgc::Rectf{m.px - camX, m.py - camY, kPlayerW, kPlayerH},
	                 sgc::Colorf{0.10f, 0.10f, 0.12f, 1.0f});
	// 目 (facing)
	const float eyeX = m.px - camX + (m.facing > 0 ? kPlayerW - 8.0f : 4.0f);
	screen.drawRect(sgc::Rectf{eyeX, m.py - camY + 7.0f, 4.0f, 4.0f},
	                 sgc::Colorf{0.95f, 0.95f, 0.95f, 1.0f});

	// 踏みつけ particles
	g_particles.draw(screen, camX, camY);
}

// ── 入口メソッドの実装 (ヘルパ定義の後) ─────────────────────────────────────
void PlatformerMemory::init() { initGame(*this); }
void PlatformerMemory::update(mitiru::Input in, mitiru::Hud hud, float dt) { stepGame(*this, in, hud, dt); }
void PlatformerMemory::draw(mitiru::Screen& screen) { drawGame(*this, screen); }

}  // namespace showcase

// 録画再生 (replay-as-test) 対象として宣言。flat POD でないと compile error になる。
// registerGame が memorySize を自動申告し replay の単一 state channel に GameMemory を記録 → diffState。
MITIRU_GAME_RECORDABLE(showcase::PlatformerMemory)
