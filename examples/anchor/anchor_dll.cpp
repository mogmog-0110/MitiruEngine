// anchor (錨) — Game-as-DLL サンプル / 制約パズル
//
// ルール 1 行: 「あんたは、過去に触ったオブジェクトに向かってしか動けない」。
//   - トークンは常にいずれかの anchor の上にいる。
//   - クリックで anchor へ dash する。dash できる先は
//       (a) 既に "remember" した anchor (= return at will)、または
//       (b) 現在地から reach 半径内の新しい anchor (触れると remember される)。
//   - それ以外 (遠くて未踏) は locked。記憶が移動資源になる puzzle。
//
// 全 anchor の gem を集めたら WIN。deterministic な hazard が anchor を巡回し、
// 到着先 / 足元に居合わせると 1 ミス。経路と記憶を計画して避ける。
//
//   gameplay (状態機械 / dash tween / reach 判定 / hazard 巡回) → C++ (このファイル)
//   HUD (GEMS / LIVES / 勝敗モーダル、手書き JS ゼロ)            → HTML/CSS (assets/scene.html)
//   手触り (dash 残像 / gem 粒子 / 画面シェイク / anchor 脈動)    → C++
//   効果音 (dash / collect / locked / hit / win / lose)           → hud.play(...)
//
// 状態は 1 個の flat POD (AnchorMemory) に集約。host が保持し hot-reload を跨ぐ。
// 入口は <mitiru/module/Game.hpp> の薄いラッパ — update / draw を書いて MITIRU_GAME のみ。

#include <algorithm>
#include <cmath>

#include <mitiru/module/Game.hpp>

namespace anchor
{

// ── 画面 / ルール定数 ───────────────────────────────────────────────────────
constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;

constexpr int   kAnchorCount = 9;
constexpr float kAnchorR     = 26.0f;   // anchor 円の半径 (クリック判定もこれ)
constexpr float kReach       = 360.0f;  // 新規 anchor へ届く距離
constexpr float kDashSpeed   = 3.4f;    // dash tween の進む速さ (1/秒、t=0→1)
constexpr int   kStartLives  = 3;
constexpr float kHazardPeriod = 1.15f;  // hazard が次の anchor へ移る間隔 (秒)
constexpr int   kMaxParticle = 48;

// ── 色 (Mitiru Saturn 系) ───────────────────────────────────────────────────
constexpr mitiru::Color kBg        = mitiru::hex(0x14161F);
constexpr mitiru::Color kRemember  = mitiru::rgb(95, 184, 201);  // 記憶済 anchor (シアン)
constexpr mitiru::Color kReachable = mitiru::rgb(242, 193, 78);  // 届く新規 anchor (黄)
constexpr mitiru::Color kLocked    = mitiru::rgb(70, 74, 92);    // 未踏で遠い (暗)
constexpr mitiru::Color kPlayer    = mitiru::color::White;
constexpr mitiru::Color kGem       = mitiru::rgb(120, 226, 140);
constexpr mitiru::Color kHazard    = mitiru::rgb(226, 59, 79);
constexpr mitiru::Color kWeb       = mitiru::rgb(60, 110, 122);  // 記憶の網 (線)

// ── 状態 (全部 flat POD) ─────────────────────────────────────────────────────
struct Particle { float x, y, vx, vy, life; float r, g, b; };

struct AnchorMemory
{
	// anchor 場 (位置は init で deterministic に配置)
	float ax[kAnchorCount] {};
	float ay[kAnchorCount] {};
	int   remembered[kAnchorCount] {};  // 1 = 触れた (return at will)
	int   gem[kAnchorCount] {};          // 1 = gem 未回収

	// プレイヤー
	int   current {0};        // 今いる anchor
	int   dashing {0};        // 1 = dash 中
	int   target {0};         // dash 先 anchor
	float dashT {0.0f};       // 0→1
	float fromX {0.0f}, fromY {0.0f};
	float px {0.0f}, py {0.0f};   // 描画用の現在座標

	// 進行
	int   gemsLeft {0};
	int   lives {kStartLives};
	int   state {0};          // 0=playing 1=win 2=lose

	// hazard (deterministic 巡回)
	int   hazardAnchor {0};
	float hazardTimer {0.0f};
	int   hazardStep {0};

	// 手触り
	Particle particles[kMaxParticle] {};
	float    shake {0.0f};
	float    deniedPulse {0.0f};  // locked クリックの赤フラッシュ
	float    trailX[10] {};
	float    trailY[10] {};
	int      trailHead {0};
	int      trailCount {0};

	// HUD 差分キャッシュ
	int lastGems {-1}, lastLives {-1}, lastState {-1};

	unsigned rng {0x9E3779B9u};
	unsigned frame {0};

	void init();
	void update(mitiru::Input in, mitiru::Hud hud, float dt);
	void draw(mitiru::Screen& screen);
};

// ── RNG (粒子方向用) ─────────────────────────────────────────────────────────
inline float rnd(AnchorMemory& m)   // [0,1)
{
	m.rng = m.rng * 1664525u + 1013904223u;
	return static_cast<float>((m.rng >> 8) & 0xFFFFFF) / 16777216.0f;
}

inline float dist(float ax, float ay, float bx, float by)
{
	const float dx = ax - bx, dy = ay - by;
	return std::sqrt(dx * dx + dy * dy);
}

// 先行宣言 (updateHazard / arrive が被弾処理を使う)。
void loseLifeAt(AnchorMemory& m, mitiru::Hud hud, float x, float y);

// ── world セットアップ ──────────────────────────────────────────────────────
// anchor は手配置 (推測座標でなく意図した相対レイアウト)。中央 start から
// 外周へ広がり、対角は reach 圏外 = 記憶を繋いで橋を架ける必要がある配置。
void layoutAnchors(AnchorMemory& m)
{
	const float cx = kScreenW * 0.5f, cy = kScreenH * 0.5f;
	const float px[kAnchorCount] = { cx, cx - 300, cx + 300, cx - 150, cx + 150,
	                                 cx - 470, cx + 470, cx - 250, cx + 250 };
	const float py[kAnchorCount] = { cy, cy - 60,  cy - 60,  cy - 250, cy - 250,
	                                 cy + 120, cy + 120, cy + 250, cy + 250 };
	for (int i = 0; i < kAnchorCount; ++i) { m.ax[i] = px[i]; m.ay[i] = py[i]; }
}

void resetGame(AnchorMemory& m)
{
	layoutAnchors(m);
	for (int i = 0; i < kAnchorCount; ++i) { m.remembered[i] = 0; m.gem[i] = 1; }
	m.current = 0;
	m.remembered[0] = 1;
	m.gem[0] = 0;                      // start anchor の gem は最初から回収済
	m.gemsLeft = kAnchorCount - 1;
	m.px = m.ax[0]; m.py = m.ay[0];
	m.dashing = 0; m.dashT = 0.0f;
	m.lives = kStartLives; m.state = 0;
	m.hazardStep = 0; m.hazardAnchor = kAnchorCount - 1; m.hazardTimer = 0.0f;
	for (auto& p : m.particles) { p.life = 0.0f; }
	m.shake = 0.0f; m.deniedPulse = 0.0f;
	m.trailCount = 0; m.trailHead = 0;
	m.lastGems = -1; m.lastLives = -1; m.lastState = -1;
}

// ── 粒子 ────────────────────────────────────────────────────────────────────
void spawnBurst(AnchorMemory& m, float x, float y, mitiru::Color c, int count)
{
	int spawned = 0;
	for (auto& p : m.particles)
	{
		if (p.life > 0.0f) { continue; }
		const float a  = rnd(m) * 6.2832f;
		const float sp = 90.0f + rnd(m) * 160.0f;
		p.x = x; p.y = y; p.vx = std::cos(a) * sp; p.vy = std::sin(a) * sp;
		p.life = 0.5f + rnd(m) * 0.4f;
		p.r = c.r; p.g = c.g; p.b = c.b;
		if (++spawned >= count) { break; }
	}
}

void updateParticles(AnchorMemory& m, float dt)
{
	for (auto& p : m.particles)
	{
		if (p.life <= 0.0f) { continue; }
		p.life -= dt * 1.5f;
		p.x += p.vx * dt; p.y += p.vy * dt;
		p.vx *= 0.96f; p.vy *= 0.96f;
	}
}

// ── hazard 巡回 (deterministic) ──────────────────────────────────────────────
// 固定の巡回順を timer で進める。frame 駆動なので replay で bit-exact。
void updateHazard(AnchorMemory& m, mitiru::Hud hud, float dt)
{
	static const int kPatrol[kAnchorCount] = { 8, 6, 2, 4, 1, 7, 5, 3, 0 };
	m.hazardTimer += dt;
	if (m.hazardTimer < kHazardPeriod) { return; }
	m.hazardTimer -= kHazardPeriod;
	m.hazardStep = (m.hazardStep + 1) % kAnchorCount;
	m.hazardAnchor = kPatrol[m.hazardStep];
	// hazard が「足元の anchor」に乗ってきたら被弾 (dash 中でなければ)。
	if (!m.dashing && m.hazardAnchor == m.current) { loseLifeAt(m, hud, m.ax[m.current], m.ay[m.current]); }
}

// ── ミス処理 ────────────────────────────────────────────────────────────────
void loseLifeAt(AnchorMemory& m, mitiru::Hud hud, float x, float y)
{
	--m.lives;
	m.shake = 1.0f;
	spawnBurst(m, x, y, kHazard, 12);
	if (m.lives <= 0) { m.lives = 0; m.state = 2; hud.play("lose", 0.8f); }
	else              { hud.play("hit", 0.7f); }
}

// ── dash ────────────────────────────────────────────────────────────────────
void arrive(AnchorMemory& m, mitiru::Hud hud)
{
	m.current = m.target;
	m.dashing = 0;
	m.px = m.ax[m.current]; m.py = m.ay[m.current];
	if (!m.remembered[m.current]) { m.remembered[m.current] = 1; }

	if (m.gem[m.current])          // gem 回収
	{
		m.gem[m.current] = 0;
		--m.gemsLeft;
		spawnBurst(m, m.px, m.py, kGem, 9);
		hud.play("collect", 0.6f);
	}
	// 到着先に hazard が居たら被弾。
	if (m.hazardAnchor == m.current && m.state == 0) { loseLifeAt(m, hud, m.px, m.py); }

	if (m.gemsLeft == 0 && m.state == 0) { m.state = 1; hud.play("win", 0.9f); }
}

void updateDash(AnchorMemory& m, mitiru::Hud hud, float dt)
{
	m.dashT += dt * kDashSpeed;
	const float t = std::clamp(m.dashT, 0.0f, 1.0f);
	const float e = t * t * (3.0f - 2.0f * t);   // smoothstep
	m.px = m.fromX + (m.ax[m.target] - m.fromX) * e;
	m.py = m.fromY + (m.ay[m.target] - m.fromY) * e;

	// 残像
	m.trailX[m.trailHead] = m.px; m.trailY[m.trailHead] = m.py;
	m.trailHead = (m.trailHead + 1) % 10;
	if (m.trailCount < 10) { ++m.trailCount; }

	if (t >= 1.0f) { arrive(m, hud); }
}

// クリックされた anchor を探して dash を試みる。
void tryDash(AnchorMemory& m, mitiru::Hud hud, float mx, float my)
{
	for (int j = 0; j < kAnchorCount; ++j)
	{
		if (dist(mx, my, m.ax[j], m.ay[j]) > kAnchorR + 6.0f) { continue; }
		if (j == m.current) { return; }
		const bool reachable = m.remembered[j] ||
		                       dist(m.ax[m.current], m.ay[m.current], m.ax[j], m.ay[j]) <= kReach;
		if (!reachable)
		{
			m.deniedPulse = 1.0f;            // locked: 届かない / 未踏
			hud.play("locked", 0.5f);
			return;
		}
		m.target = j; m.dashing = 1; m.dashT = 0.0f;
		m.fromX = m.px; m.fromY = m.py;
		m.trailCount = 0; m.trailHead = 0;
		hud.play("dash", 0.5f);
		return;
	}
}

// ── HUD へ送る (差分のみ) ────────────────────────────────────────────────────
void pushHud(AnchorMemory& m, mitiru::Hud hud)
{
	const int collected = (kAnchorCount - 1) - m.gemsLeft;
	if (collected != m.lastGems)
	{
		hud.set("view.hud.gems", collected);
		hud.set("view.hud.gemsTotal", kAnchorCount - 1);
		m.lastGems = collected;
	}
	if (m.lives != m.lastLives)
	{
		char hearts[40]; int n = 0;
		for (int i = 0; i < m.lives && n + 3 < static_cast<int>(sizeof(hearts)); ++i)
		{
			hearts[n++] = static_cast<char>(0xE2);   // ♥ = E2 99 A5 (UTF-8 直書き)
			hearts[n++] = static_cast<char>(0x99);
			hearts[n++] = static_cast<char>(0xA5);
		}
		hearts[n] = '\0';
		hud.set("view.hud.hearts", hearts);
		m.lastLives = m.lives;
	}
	if (m.state != m.lastState)
	{
		hud.set("view.hud.over", m.state != 0);
		hud.set("view.hud.outcome", m.state == 1 ? "win" : (m.state == 2 ? "lose" : ""));
		m.lastState = m.state;
	}
}

// ── 入口メソッド ─────────────────────────────────────────────────────────────
void AnchorMemory::init() { resetGame(*this); }

void AnchorMemory::update(mitiru::Input in, mitiru::Hud hud, float dt)
{
	++frame;
	if (in.pressed(mitiru::Key::Escape)) { hud.quit(); }
	if (in.action("game.restart"))       { resetGame(*this); }

	if (shake > 0.0f)       { shake = std::max(0.0f, shake - dt * 2.6f); }
	if (deniedPulse > 0.0f) { deniedPulse = std::max(0.0f, deniedPulse - dt * 3.0f); }
	updateParticles(*this, dt);
	pushHud(*this, hud);

	if (state != 0) { return; }     // 勝敗後は入力を止める (粒子と shake は続く)

	updateHazard(*this, hud, dt);
	if (state != 0) { return; }     // hazard 被弾で lose した場合

	if (dashing) { updateDash(*this, hud, dt); }
	else if (in.mousePressed(0)) { tryDash(*this, hud, in.mouseX(), in.mouseY()); }
}

// ── 描画ヘルパ ──────────────────────────────────────────────────────────────
inline void disc(mitiru::Screen& s, float x, float y, float r, mitiru::Color c, float ox, float oy)
{
	s.fillCircle(x + ox, y + oy, r, c);
}

void AnchorMemory::draw(mitiru::Screen& screen)
{
	float ox = 0.0f, oy = 0.0f;
	if (shake > 0.0f)
	{
		const float amp = shake * 8.0f;
		ox = std::sin(static_cast<float>(frame) * 1.7f) * amp;
		oy = std::cos(static_cast<float>(frame) * 2.3f) * amp;
	}

	screen.fillScreen(kBg);

	// 記憶の網 — remembered な anchor 同士を線で結ぶ (記憶 = 資源の可視化)。
	for (int i = 0; i < kAnchorCount; ++i)
	{
		if (!remembered[i]) { continue; }
		for (int j = i + 1; j < kAnchorCount; ++j)
		{
			if (!remembered[j]) { continue; }
			if (dist(ax[i], ay[i], ax[j], ay[j]) > kReach) { continue; }
			screen.drawLine(sgc::Vec2f{ax[i] + ox, ay[i] + oy},
			                sgc::Vec2f{ax[j] + ox, ay[j] + oy}, kWeb, 2.0f);
		}
	}

	// 現在地から reach 圏内の新規 anchor へ、到達可能を示す薄い線。
	if (!dashing && state == 0)
	{
		for (int j = 0; j < kAnchorCount; ++j)
		{
			if (j == current || remembered[j]) { continue; }
			if (dist(ax[current], ay[current], ax[j], ay[j]) > kReach) { continue; }
			screen.drawLine(sgc::Vec2f{px + ox, py + oy},
			                sgc::Vec2f{ax[j] + ox, ay[j] + oy},
			                mitiru::Color{kReachable.r, kReachable.g, kReachable.b, 0.28f}, 1.5f);
		}
	}

	// anchor 本体
	const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(frame) * 0.08f);
	for (int i = 0; i < kAnchorCount; ++i)
	{
		mitiru::Color c = kLocked;
		bool reachable = remembered[i] ||
		                 (!dashing && dist(ax[current], ay[current], ax[i], ay[i]) <= kReach);
		if (remembered[i])      { c = kRemember; }
		else if (reachable)     { c = kReachable; }

		// 届く新規 anchor は脈動させて「ここ押せる」を伝える。
		float r = kAnchorR;
		if (reachable && !remembered[i]) { r += pulse * 4.0f; }
		disc(screen, ax[i], ay[i], r, c, ox, oy);
		disc(screen, ax[i], ay[i], r * 0.55f, mitiru::Color{kBg.r, kBg.g, kBg.b, 1.0f}, ox, oy);  // 中抜き = リング

		// gem (残っている anchor の中心に小さな菱形 = 回した四角)
		if (gem[i])
		{
			screen.drawRectRotated(sgc::Rectf{ax[i] - 7.0f + ox, ay[i] - 7.0f + oy, 14.0f, 14.0f}, kGem, 45.0f);
		}
	}

	// hazard (巡回中の anchor を赤く満たす + リング)
	disc(screen, ax[hazardAnchor], ay[hazardAnchor], kAnchorR * 0.62f, kHazard, ox, oy);

	// dash 残像
	for (int i = 0; i < trailCount; ++i)
	{
		const int idx = (trailHead - 1 - i + 20) % 10;
		const float t = 1.0f - static_cast<float>(i + 1) / 11.0f;
		disc(screen, trailX[idx], trailY[idx], 7.0f * t, mitiru::Color{1, 1, 1, t * 0.30f}, ox, oy);
	}

	// 粒子
	for (const auto& p : particles)
	{
		if (p.life <= 0.0f) { continue; }
		const float a = std::clamp(p.life, 0.0f, 1.0f);
		screen.drawRect(p.x - 3.0f + ox, p.y - 3.0f + oy, 6.0f, 6.0f, mitiru::Color{p.r, p.g, p.b, a});
	}

	// プレイヤートークン
	disc(screen, px, py, 12.0f, kPlayer, ox, oy);

	// locked クリックの赤フラッシュ (画面四隅うっすら)
	if (deniedPulse > 0.0f)
	{
		screen.drawRect(0, 0, kScreenW, kScreenH,
		                mitiru::Color{kHazard.r, kHazard.g, kHazard.b, deniedPulse * 0.12f});
	}
}

}  // namespace anchor

// これ 1 行で DLL の入口。flat POD なので replay-as-test 対象として宣言。
MITIRU_GAME_RECORDABLE(anchor::AnchorMemory)
