// breakout — Game-as-DLL 旗艦サンプル
//
// パドルでボールを返してブロックを全部壊す王道ブロック崩し。エンジンの売りを
// 1 本に組み上げた参照実装:
//   gameplay (物理 / 当たり判定 / 状態機械)  → C++ (このファイル)
//   HUD (SCORE / LIVES / LEVEL / 勝敗モーダル) → HTML/CSS (assets/scene.html、JS 無し)
//   手触り (粒子 / 画面シェイク / ボール残像)  → C++
//   効果音 (bounce / brick / down / win / lose) → hud.play(...) でホストにお願い
//
// 状態は 1 個の構造体 (BreakoutMemory) に集約 — ホストが保持し、ホットリロード
// (コードだけ差し替え) を跨いで生き残る。マウスを動かさない間はパドルが自動で
// ボールを追う (attract mode)。動かすと手動操作へ切り替わる。
//
// 入口は <mitiru/module/Game.hpp> の薄いラッパを使う。update / draw を書いて
// MITIRU_GAME(...) と書くだけ。void* や生ポインタは出てこない。

#include <algorithm>
#include <cmath>

#include <mitiru/module/Game.hpp>

namespace breakout
{

// ── 画面 / レイアウト定数 ───────────────────────────────────────────────────
constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;

constexpr int   kCols      = 11;
constexpr int   kRows      = 6;
constexpr int   kMaxBrick  = kCols * kRows;   // 66
constexpr float kFieldL    = 60.0f;
constexpr float kFieldR    = 1220.0f;
constexpr float kBrickTop  = 116.0f;
constexpr float kBrickH    = 28.0f;
constexpr float kBrickGap  = 8.0f;
constexpr float kBrickW    = (kFieldR - kFieldL - (kCols - 1) * kBrickGap) / kCols;
constexpr float kRowStride = kBrickH + kBrickGap;

constexpr float kPaddleW   = 150.0f;
constexpr float kPaddleH   = 16.0f;
constexpr float kPaddleY   = 678.0f;
constexpr float kBall      = 14.0f;          // 一辺 (レトロな四角ボール)
constexpr float kBallHalf  = kBall * 0.5f;

constexpr int   kMaxParticle = 64;
constexpr int   kTrail       = 8;
constexpr int   kMaxLevel    = 3;
constexpr int   kStartLives  = 3;

// ── 色 (mitiru::rgb / hex / 名前付き — sgc を知らなくてよい) ─────────────────
constexpr mitiru::Color kBg      = mitiru::hex(0x1B1D2A);    // 深いスレート
constexpr mitiru::Color kPaddle  = mitiru::rgb(232, 232, 236);
constexpr mitiru::Color kBallCol = mitiru::color::White;

// 6 行のブロック色 (上から赤→橙→黄→緑→青緑→藍)。
inline mitiru::Color rowColor(int row)
{
	switch (row)
	{
	case 0:  return mitiru::rgb(226, 59, 59);
	case 1:  return mitiru::rgb(239, 138, 59);
	case 2:  return mitiru::rgb(242, 193, 78);
	case 3:  return mitiru::rgb(95, 184, 95);
	case 4:  return mitiru::rgb(67, 176, 201);
	default: return mitiru::rgb(106, 123, 214);
	}
}

// ── ゲームの状態 (全部 flat POD) ────────────────────────────────────────────
struct Brick    { float x, y, w, h; int alive; int row; };
struct Particle { float x, y, vx, vy, life; float r, g, b; };

struct BreakoutMemory
{
	// 進行
	int   score {0};
	int   lives {kStartLives};
	int   level {1};
	int   state {0};            // 0=playing 1=win 2=lose
	int   bricksAlive {0};

	// パドル / ボール
	float paddleX {kScreenW * 0.5f};
	float ballX {kScreenW * 0.5f};
	float ballY {kPaddleY - 40.0f};
	float ballVX {0.0f};
	float ballVY {0.0f};

	// ブロック
	Brick bricks[kMaxBrick] {};

	// 手触り
	Particle particles[kMaxParticle] {};
	float    shake {0.0f};
	float    trailX[kTrail] {};
	float    trailY[kTrail] {};
	int      trailHead {0};
	int      trailCount {0};

	// 入力追従 — マウスを動かしたら手動操作、動かさない間は attract で自動プレイ。
	bool  mouseEngaged {false};
	float lastMouseX {-1.0f};
	float lastMouseY {-1.0f};

	// HUD 差分キャッシュ (変化時だけ送る)
	int   lastScore {-1};
	int   lastLives {-1};
	int   lastLevel {-1};
	int   lastState {-1};

	// 決定的 RNG (粒子方向用 LCG)
	unsigned rng {0x12345678u};
	unsigned frame {0};

	// エンジンが呼ぶ入口 (実装は下、ヘルパ定義の後)。
	void init();
	void update(mitiru::Input in, mitiru::Hud hud, float dt);
	void draw(mitiru::Screen& screen);
};
// flat POD であることは末尾の MITIRU_GAME_RECORDABLE が compile time に保証する。

// ── RNG ─────────────────────────────────────────────────────────────────────
inline float rnd(BreakoutMemory& m)   // [0,1)
{
	m.rng = m.rng * 1664525u + 1013904223u;
	return static_cast<float>((m.rng >> 8) & 0xFFFFFF) / 16777216.0f;
}

// ── world セットアップ ──────────────────────────────────────────────────────
void serveBall(BreakoutMemory& m)
{
	// パドル上から斜め上へ自動発射 (serve 待ちは作らず即プレイ — 観察しやすい)。
	const float speed = 380.0f + (m.level - 1) * 60.0f;
	const float dir   = rnd(m) < 0.5f ? -1.0f : 1.0f;
	const float ang   = 0.35f + rnd(m) * 0.25f;   // 鉛直からの傾き(rad)
	m.ballX  = m.paddleX;
	m.ballY  = kPaddleY - kBallHalf - 4.0f;
	m.ballVX = std::sin(ang) * speed * dir;
	m.ballVY = -std::cos(ang) * speed;
	m.trailCount = 0; m.trailHead = 0;
}

void buildLevel(BreakoutMemory& m)
{
	int n = 0;
	for (int row = 0; row < kRows; ++row)
	{
		for (int col = 0; col < kCols; ++col)
		{
			Brick& b = m.bricks[n++];
			b.x = kFieldL + col * (kBrickW + kBrickGap);
			b.y = kBrickTop + row * kRowStride;
			b.w = kBrickW; b.h = kBrickH;
			b.alive = 1; b.row = row;
		}
	}
	m.bricksAlive = kMaxBrick;
	m.paddleX = kScreenW * 0.5f;
	serveBall(m);
}

void resetGame(BreakoutMemory& m)
{
	m.score = 0; m.lives = kStartLives; m.level = 1; m.state = 0;
	for (auto& p : m.particles) { p.life = 0.0f; }
	m.shake = 0.0f;
	buildLevel(m);
	// HUD 再送を強制 (現値の逆を入れて差分を立てる)。
	m.lastScore = -1; m.lastLives = -1; m.lastLevel = -1; m.lastState = -1;
}

// ── 粒子 ────────────────────────────────────────────────────────────────────
void spawnParticles(BreakoutMemory& m, float x, float y, mitiru::Color c)
{
	int spawned = 0;
	for (auto& p : m.particles)
	{
		if (p.life > 0.0f) { continue; }
		const float a = rnd(m) * 6.2832f;
		const float sp = 120.0f + rnd(m) * 180.0f;
		p.x = x; p.y = y;
		p.vx = std::cos(a) * sp;
		p.vy = std::sin(a) * sp - 80.0f;
		p.life = 0.6f + rnd(m) * 0.3f;
		p.r = c.r; p.g = c.g; p.b = c.b;
		if (++spawned >= 7) { break; }
	}
}

void updateParticles(BreakoutMemory& m, float dt)
{
	for (auto& p : m.particles)
	{
		if (p.life <= 0.0f) { continue; }
		p.life -= dt * 1.6f;
		p.vy   += 520.0f * dt;          // 重力
		p.x    += p.vx * dt;
		p.y    += p.vy * dt;
	}
}

// ── パドル ──────────────────────────────────────────────────────────────────
void updatePaddle(BreakoutMemory& m, mitiru::Input in, float dt)
{
	// マウスを「動かしたら」手動操作へ。止まっているカーソルにパドルを奪われないよう、
	// 位置でなく移動量で判定する (起動直後やプレイヤーが触っていない間は attract 自動プレイ)。
	if (m.lastMouseX >= 0.0f)
	{
		const float moved = std::abs(in.mouseX() - m.lastMouseX) + std::abs(in.mouseY() - m.lastMouseY);
		if (moved > 1.5f) { m.mouseEngaged = true; }
	}
	m.lastMouseX = in.mouseX();
	m.lastMouseY = in.mouseY();

	const float half = kPaddleW * 0.5f;
	if (m.mouseEngaged)
	{
		m.paddleX = in.mouseX();                      // 手動: カーソルに追従
	}
	else
	{
		// attract: ボールに素早く追従 (瞬間移動はせず、わずかな遅れで返球角に変化を出す)。
		m.paddleX += (m.ballX - m.paddleX) * std::min(1.0f, dt * 16.0f);
	}
	m.paddleX = std::clamp(m.paddleX, kFieldL + half, kFieldR - half);
}

// ── ボール ──────────────────────────────────────────────────────────────────
void bounceOffPaddle(BreakoutMemory& m, mitiru::Hud hud)
{
	const float speed = std::sqrt(m.ballVX * m.ballVX + m.ballVY * m.ballVY);
	const float half  = kPaddleW * 0.5f;
	float off = (m.ballX - m.paddleX) / half;     // -1..1
	off = std::clamp(off, -1.0f, 1.0f);
	const float ang = off * 1.0472f;               // 最大 60°
	m.ballVX = std::sin(ang) * speed;
	m.ballVY = -std::cos(ang) * speed;             // 必ず上へ
	m.ballY  = kPaddleY - kBallHalf - 1.0f;
	hud.play("bounce", 0.5f);
}

void hitBrick(BreakoutMemory& m, Brick& b, bool flipX, mitiru::Hud hud)
{
	b.alive = 0;
	--m.bricksAlive;
	m.score += 10 * (kRows - b.row);               // 上の行ほど高得点
	spawnParticles(m, b.x + b.w * 0.5f, b.y + b.h * 0.5f, rowColor(b.row));
	m.shake = std::min(1.0f, m.shake + 0.22f);
	if (flipX) { m.ballVX = -m.ballVX; } else { m.ballVY = -m.ballVY; }
	hud.play("brick", 0.6f);
}

// ボール vs ブロック: 最初に重なった 1 個だけ処理 (素直で十分)。
void collideBricks(BreakoutMemory& m, mitiru::Hud hud)
{
	const float l = m.ballX - kBallHalf, r = m.ballX + kBallHalf;
	const float t = m.ballY - kBallHalf, btm = m.ballY + kBallHalf;
	for (auto& b : m.bricks)
	{
		if (!b.alive) { continue; }
		if (r < b.x || l > b.x + b.w || btm < b.y || t > b.y + b.h) { continue; }
		// 侵入量の小さい軸で反射 (角の挙動を安定させる)。
		const float overlapX = std::min(r, b.x + b.w) - std::max(l, b.x);
		const float overlapY = std::min(btm, b.y + b.h) - std::max(t, b.y);
		hitBrick(m, b, overlapX < overlapY, hud);
		break;
	}
}

void loseLife(BreakoutMemory& m, mitiru::Hud hud)
{
	--m.lives;
	m.shake = 1.0f;
	if (m.lives <= 0)
	{
		m.lives = 0;
		m.state = 2;                  // lose
		hud.play("lose", 0.8f);
	}
	else
	{
		hud.play("down", 0.7f);
		serveBall(m);
	}
}

void updateBall(BreakoutMemory& m, mitiru::Hud hud, float dt)
{
	m.ballX += m.ballVX * dt;
	m.ballY += m.ballVY * dt;

	// 壁
	if (m.ballX - kBallHalf < kFieldL) { m.ballX = kFieldL + kBallHalf; m.ballVX = -m.ballVX; hud.play("bounce", 0.35f); }
	if (m.ballX + kBallHalf > kFieldR) { m.ballX = kFieldR - kBallHalf; m.ballVX = -m.ballVX; hud.play("bounce", 0.35f); }
	if (m.ballY - kBallHalf < kBrickTop - 40.0f) { m.ballY = kBrickTop - 40.0f + kBallHalf; m.ballVY = -m.ballVY; hud.play("bounce", 0.35f); }

	// パドル (落下中だけ)
	const float half = kPaddleW * 0.5f;
	if (m.ballVY > 0.0f &&
	    m.ballY + kBallHalf >= kPaddleY &&
	    m.ballY - kBallHalf <= kPaddleY + kPaddleH &&
	    m.ballX >= m.paddleX - half - kBallHalf &&
	    m.ballX <= m.paddleX + half + kBallHalf)
	{
		bounceOffPaddle(m, hud);
	}

	// 落下
	if (m.ballY - kBallHalf > kScreenH) { loseLife(m, hud); return; }

	collideBricks(m, hud);

	// 残像
	m.trailX[m.trailHead] = m.ballX;
	m.trailY[m.trailHead] = m.ballY;
	m.trailHead = (m.trailHead + 1) % kTrail;
	if (m.trailCount < kTrail) { ++m.trailCount; }

	// 全消し → 次レベル or WIN
	if (m.bricksAlive == 0)
	{
		if (m.level >= kMaxLevel) { m.state = 1; hud.play("win", 0.9f); }
		else { ++m.level; buildLevel(m); }
	}
}

// ── HUD へ送る (差分のみ) ────────────────────────────────────────────────────
void pushHud(BreakoutMemory& m, mitiru::Hud hud)
{
	if (m.score != m.lastScore) { hud.set("view.hud.score", m.score); m.lastScore = m.score; }
	if (m.lives != m.lastLives)
	{
		// ハート列を UTF-8 バイト直書きで組む (源文字コードに依存しない)。♥ = E2 99 A5。
		char hearts[40]; int n = 0;
		for (int i = 0; i < m.lives && n + 3 < static_cast<int>(sizeof(hearts)); ++i)
		{
			hearts[n++] = static_cast<char>(0xE2);
			hearts[n++] = static_cast<char>(0x99);
			hearts[n++] = static_cast<char>(0xA5);
		}
		hearts[n] = '\0';
		hud.set("view.hud.hearts", hearts);
		m.lastLives = m.lives;
	}
	if (m.level != m.lastLevel) { hud.set("view.hud.level", m.level); m.lastLevel = m.level; }
	if (m.state != m.lastState)
	{
		hud.set("view.hud.over", m.state != 0);
		hud.set("view.hud.outcome", m.state == 1 ? "win" : (m.state == 2 ? "lose" : ""));
		m.lastState = m.state;
	}
}

// ── 描画ヘルパ ──────────────────────────────────────────────────────────────
// shake 中は全描画に同じ (ox, oy) オフセットを足したいので局所ヘルパでまとめる。
inline void rect(mitiru::Screen& s, float x, float y, float w, float h, mitiru::Color c, float ox, float oy)
{
	s.drawRect(x + ox, y + oy, w, h, c);
}

// ── 入口メソッドの実装 (ヘルパ定義の後) ─────────────────────────────────────
void BreakoutMemory::init() { resetGame(*this); }

void BreakoutMemory::update(mitiru::Input in, mitiru::Hud hud, float dt)
{
	++frame;
	if (in.pressed(mitiru::Key::Escape)) { hud.quit(); }
	if (in.action("game.restart"))       { resetGame(*this); }

	if (shake > 0.0f) { shake = std::max(0.0f, shake - dt * 2.4f); }
	updateParticles(*this, dt);
	pushHud(*this, hud);

	if (state != 0) { return; }       // 勝敗後は球を止める (粒子と shake は続く)

	updatePaddle(*this, in, dt);
	updateBall(*this, hud, dt);
}

void BreakoutMemory::draw(mitiru::Screen& screen)
{
	// 画面シェイク量を全描画にかけるオフセット。
	float ox = 0.0f, oy = 0.0f;
	if (shake > 0.0f)
	{
		const float amp = shake * 9.0f;
		ox = std::sin(static_cast<float>(frame) * 1.7f) * amp;
		oy = std::cos(static_cast<float>(frame) * 2.3f) * amp;
	}

	screen.fillScreen(kBg);   // 背景 (clear() は host 設定に上書きされるので fillScreen で確実に塗る)

	// ブロック
	for (const auto& b : bricks)
	{
		if (!b.alive) { continue; }
		const mitiru::Color c = rowColor(b.row);
		rect(screen, b.x, b.y, b.w, b.h, c, ox, oy);
		rect(screen, b.x, b.y, b.w, 3.0f, mitiru::Color{1, 1, 1, 0.30f}, ox, oy);   // 上辺ハイライト
	}

	// 粒子
	for (const auto& p : particles)
	{
		if (p.life <= 0.0f) { continue; }
		const float a = std::clamp(p.life, 0.0f, 1.0f);
		rect(screen, p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f, mitiru::Color{p.r, p.g, p.b, a}, ox, oy);
	}

	// ボール残像 (古いほど薄い)
	for (int i = 0; i < trailCount; ++i)
	{
		const int idx = (trailHead - 1 - i + kTrail * 2) % kTrail;
		const float t = 1.0f - static_cast<float>(i + 1) / static_cast<float>(kTrail + 1);
		const float sz = kBall * (0.5f + 0.4f * t);
		rect(screen, trailX[idx] - sz * 0.5f, trailY[idx] - sz * 0.5f,
		     sz, sz, mitiru::Color{1, 1, 1, t * 0.25f}, ox, oy);
	}

	// ボール
	rect(screen, ballX - kBallHalf, ballY - kBallHalf, kBall, kBall, kBallCol, ox, oy);

	// パドル
	rect(screen, paddleX - kPaddleW * 0.5f, kPaddleY, kPaddleW, kPaddleH, kPaddle, ox, oy);
	rect(screen, paddleX - kPaddleW * 0.5f, kPaddleY, kPaddleW, 3.0f, mitiru::Color{1, 1, 1, 0.5f}, ox, oy);
}

}  // namespace breakout

// これ 1 行で DLL の入口が出来る。flat POD なので録画再生 (replay-as-test) の対象として宣言。
MITIRU_GAME_RECORDABLE(breakout::BreakoutMemory)
