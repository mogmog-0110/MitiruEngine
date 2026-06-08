// hello_game — Game-as-DLL のサバイバルゲーム (ADR 0005 参照実装、flat POD / ADR 0017)
//
// マウスで逃げて 30 秒生き延びる純粋なゲーム。HUD (HP / SURVIVE / 勝敗モーダル) は
// HTML/CSS で書き (assets/scene.html、zero-JS)、ゲーム世界は C++ が描く。debug/tool 系
// (タイムトラベル / replay / pause 等) はゲーム窓に出さない — inspector sub-window の責務。
//
// GameMemory は flat POD なので、host が録画再生・タイムトラベルの対象にできる
// (ADR 0017)。HP / 自機 X を probe で宣言してあるので `--inspect timetravel` で履歴を見られる。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <mitiru/core/FixedVec.hpp>
#include <mitiru/module/Game.hpp>

using namespace mitiru;

namespace hello_game
{

// ── gameplay 定数 ───────────────────────────────────────────────────────────
constexpr float kScreenW      = 1280.0f;
constexpr float kScreenH      = 720.0f;
constexpr float kPlayerSpeed  = 520.0f;   // マウス追従の最大速度
constexpr float kEnemySpeed   = 95.0f;
constexpr float kPlayerR      = 14.0f;
constexpr float kEnemyR       = 15.0f;
constexpr int   kMaxHp        = 100;
constexpr int   kHitDamage    = 10;
constexpr float kEnemyRespawn = 2.0f;
constexpr float kSurviveTime  = 30.0f;
constexpr int   kEnemyCount   = 4;
constexpr int   kTrailMax     = 10;
constexpr int   kHpBarCells   = 20;
constexpr int   kLowHp        = 30;       // これ以下で HP バーが赤 (HUD の hpLow)

struct Vec2 { float x, y; };

struct Enemy { Vec2 pos; float respawnIn; bool alive; };

enum class Outcome : std::uint8_t { None, Win, Lose };

// ── ゲームの状態 (flat POD — vector/string/pointer を持たない、ADR 0017) ──────
struct HelloGameMemory
{
	Vec2                                  player {kScreenW * 0.5f, kScreenH * 0.5f};
	mitiru::FixedVec<Enemy, kEnemyCount>  enemies;
	int                                   hp        {kMaxHp};
	float                                 remaining {kSurviveTime};
	bool                                  gameOver  {false};
	Outcome                               outcome   {Outcome::None};
	float                                 hitFlash  {0.0f};
	int                                   hitCount  {0};
	mitiru::FixedVec<Vec2, kTrailMax>     trail;        // 足跡 (古い順)
	std::uint32_t                         rng   {1u};
	std::uint32_t                         frame {0};

	// HUD diff キャッシュ — 変化のない値の再 push を避ける
	int     lastHp       {-1};
	int     lastTimeInt  {-1};
	int     lastHitCount {-1};
	bool    lastGameOver {false};
	Outcome lastOutcome  {Outcome::None};
	bool    pushedMaxHp  {false};

	float random()
	{
		rng = rng * 1664525u + 1013904223u;
		return static_cast<float>((rng >> 8) & 0xFFFF) / 65535.0f;
	}

	// 画面端のどこかにランダム配置 (敵の出現位置)。
	Vec2 edgeSpawn()
	{
		switch (static_cast<int>(random() * 4.0f) & 3)
		{
			case 0:  return {random() * kScreenW, -kEnemyR};            // 上
			case 1:  return {random() * kScreenW, kScreenH + kEnemyR};  // 下
			case 2:  return {-kEnemyR, random() * kScreenH};            // 左
			default: return {kScreenW + kEnemyR, random() * kScreenH};  // 右
		}
	}

	void reset()
	{
		player = {kScreenW * 0.5f, kScreenH * 0.5f};
		hp = kMaxHp; remaining = kSurviveTime; gameOver = false; outcome = Outcome::None;
		hitFlash = 0.0f; hitCount = 0; trail.clear();
		enemies.clear();
		for (int i = 0; i < kEnemyCount; ++i) { enemies.push_back(Enemy{edgeSpawn(), 0.0f, true}); }
		lastHp = lastTimeInt = lastHitCount = -1;
		lastGameOver = false; lastOutcome = Outcome::None; pushedMaxHp = false;
	}

	// 起動時に 1 回だけ呼ばれる。
	void init() { reset(); }

	// 足跡を 1 つ積む (満杯なら最古を捨てて前へ詰める)。
	void pushTrail(Vec2 p)
	{
		if (trail.full())
		{
			for (std::uint32_t i = 1; i < trail.size(); ++i) { trail[i - 1] = trail[i]; }
			trail.count = trail.size() - 1;
		}
		trail.push_back(p);
	}

	// 毎フレーム呼ばれる。
	void update(Input in, Hud hud, float dt)
	{
		++frame;

		if (in.pressed(Key::Escape)) { hud.quit(); }

		if (gameOver)
		{
			if (in.action("game.restart")) { reset(); }
			pushHud(hud);
			return;
		}

		// 自機をマウスへ寄せる (最大 kPlayerSpeed)。
		const float toMx = in.mouseX() - player.x;
		const float toMy = in.mouseY() - player.y;
		const float dist = std::sqrt(toMx * toMx + toMy * toMy);
		const float step = kPlayerSpeed * dt;
		if (dist > 1.0f)
		{
			const float move = std::min(step, dist);
			player.x += toMx / dist * move;
			player.y += toMy / dist * move;
		}
		player.x = std::clamp(player.x, kPlayerR, kScreenW - kPlayerR);
		player.y = std::clamp(player.y, kPlayerR, kScreenH - kPlayerR);
		if (frame % 3 == 0) { pushTrail(player); }

		// 敵: 生きてれば自機を追う、死んでれば respawn を待つ。
		if (hitFlash > 0.0f) { hitFlash -= dt; }
		for (std::uint32_t i = 0; i < enemies.size(); ++i)
		{
			Enemy& e = enemies[i];
			if (!e.alive)
			{
				e.respawnIn -= dt;
				if (e.respawnIn <= 0.0f) { e.pos = edgeSpawn(); e.alive = true; }
				continue;
			}
			const float ex = player.x - e.pos.x, ey = player.y - e.pos.y;
			const float ed = std::sqrt(ex * ex + ey * ey);
			if (ed > 1.0f) { e.pos.x += ex / ed * kEnemySpeed * dt; e.pos.y += ey / ed * kEnemySpeed * dt; }

			// 当たり判定: 当たったら HP を減らし敵を一旦退場させる。
			const float reach = kPlayerR + kEnemyR;
			if (ed < reach)
			{
				hp -= kHitDamage; if (hp < 0) { hp = 0; }
				hitFlash = 0.25f; ++hitCount;
				e.alive = false; e.respawnIn = kEnemyRespawn;
			}
		}

		// 勝敗判定。
		remaining -= dt;
		if (hp <= 0)            { gameOver = true; outcome = Outcome::Lose; hp = 0; }
		else if (remaining <= 0.0f) { remaining = 0.0f; gameOver = true; outcome = Outcome::Win; }

		pushHud(hud);
	}

	// HUD (HTML) へ view.hud.* を push する。変化した値だけ送る。
	void pushHud(Hud hud)
	{
		if (!pushedMaxHp) { hud.set("view.hud.maxHp", kMaxHp); pushedMaxHp = true; }

		if (hp != lastHp)
		{
			lastHp = hp;
			hud.set("view.hud.hp", hp);
			hud.set("view.hud.hpLow", hp <= kLowHp);
			// テキスト HP バー (█ 埋め + ░ 空き、計 kHpBarCells セル)。
			const int filled = (hp * kHpBarCells + kMaxHp - 1) / kMaxHp;  // ceil
			std::string fill, empty;
			for (int k = 0; k < filled; ++k)            { fill  += "█"; }
			for (int k = filled; k < kHpBarCells; ++k)  { empty += "░"; }
			hud.set("view.hud.hpFill", fill.c_str());
			hud.set("view.hud.hpEmpty", empty.c_str());
		}

		const int timeInt = static_cast<int>(remaining + 0.999f);
		if (timeInt != lastTimeInt) { lastTimeInt = timeInt; hud.set("view.hud.time", timeInt); }

		if (hitCount != lastHitCount) { lastHitCount = hitCount; hud.set("view.hud.hitCount", hitCount); }

		if (gameOver != lastGameOver) { lastGameOver = gameOver; hud.set("view.hud.gameOver", gameOver); }

		if (outcome != lastOutcome)
		{
			lastOutcome = outcome;
			hud.set("view.hud.outcome",
			        outcome == Outcome::Win ? "win" : outcome == Outcome::Lose ? "lose" : "");
		}
	}

	// 毎フレーム呼ばれる。ゲーム世界 (足跡・敵・自機) を描く。HUD は HTML 側。
	void draw(Screen& s)
	{
		s.fillScreen(hitFlash > 0.0f ? hex(0x3A1414) : hex(0x14182A));

		// 足跡 (古いほど薄く)。
		for (std::uint32_t i = 0; i < trail.size(); ++i)
		{
			const float a = static_cast<float>(i + 1) / static_cast<float>(kTrailMax);
			auto c = color::Cyan; c.a = a * 0.4f;
			s.fillCircle(trail[i].x, trail[i].y, kPlayerR * 0.6f, c);
		}

		// 敵 (生存のみ)。
		for (std::uint32_t i = 0; i < enemies.size(); ++i)
		{
			if (enemies[i].alive) { s.fillCircle(enemies[i].pos.x, enemies[i].pos.y, kEnemyR, color::Red); }
		}

		// 自機。
		s.fillCircle(player.x, player.y, kPlayerR, color::Cyan);
	}
};

static_assert(std::is_trivially_copyable_v<HelloGameMemory>,
              "HelloGameMemory は flat POD (録画再生・タイムトラベルの単一 state、ADR 0017)");

// ── time-travel 観測 probe (ADR 0017) ───────────────────────────────────────
namespace
{
double hpProbe(const void* m)      { return static_cast<const HelloGameMemory*>(m)->hp; }
double playerXProbe(const void* m) { return static_cast<const HelloGameMemory*>(m)->player.x; }
}  // namespace

}  // namespace hello_game

// GameMemory の構造を host に申告する (ADR 0018)。AI が窓を開かず全状態を構造的に読める。
// 要素 struct を内側から先に: Vec2 → Enemy (Vec2 を含む) → HelloGameMemory。
MITIRU_REFLECT_STRUCT(hello_game::Vec2, x, y);
MITIRU_REFLECT_STRUCT(hello_game::Enemy, pos, respawnIn, alive);
MITIRU_REFLECT(hello_game::HelloGameMemory,
	player, enemies, hp, remaining, gameOver, hitFlash, hitCount, frame);

// これ 1 つで DLL の入口 + time-travel 観測が出来る。`--inspect timetravel` で HP 履歴を見られる。
MITIRU_GAME_SERIES(hello_game::HelloGameMemory,
	{ "hp", "HP",       &hello_game::hpProbe,      static_cast<double>(hello_game::kLowHp), 1 },
	{ "x",  "Player X", &hello_game::playerXProbe, 0.0, 0 });
