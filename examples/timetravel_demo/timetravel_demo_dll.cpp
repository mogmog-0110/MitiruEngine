// timetravel_demo — タイムトラベル inspector の模範サンプル (ADR 0017)
//
// 覚えることは 2 つだけ:
//   1. GameMemory を flat POD にする (vector/string の代わりに mitiru::FixedVec を使う)
//   2. MITIRU_GAME_SERIES で「GameMemory から HP を引く関数」を 1 つ宣言する
// これだけで host が過去フレームの GameMemory を自動で記録し、inspector の time-travel
// graph に HP 履歴を出す。graph をクリックすると host がゲームを *その瞬間へ巻き戻す*。
// 履歴を手で貯めたり JSON を組んだりするコードは一切要らない。
//
// 実行:  mitiru_host.exe timetravel_demo/timetravel_demo.dll --inspect timetravel

#include <algorithm>
#include <cstdint>
#include <string>

#include <mitiru/core/FixedVec.hpp>
#include <mitiru/module/Game.hpp>

using namespace mitiru;

// ── 画面と大きさ ────────────────────────────────────────────────────────────
constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;
constexpr int   kMaxHp   = 100;
constexpr int   kMaxHaz  = 16;        // 同時に出せる危険物の上限
constexpr float kPlayerR = 22.0f;

// 落ちてくる危険物 1 個 (flat POD)。
struct Hazard { float x, y, vy; };

// ── ゲームの状態 (flat POD — ポインタ・vector・string を一切持たない) ─────────
struct TtDemo
{
	float                            playerX = kScreenW * 0.5f;
	float                            playerY = kScreenH * 0.5f;
	mitiru::FixedVec<Hazard, kMaxHaz> hazards;   // std::vector<Hazard> の代わり
	int                              hp       = kMaxHp;
	float                            hitFlash = 0.0f;
	float                            spawnIn  = 0.0f;
	float                            regenAcc = 0.0f;
	std::uint32_t                    rng      = 0x1234567u;
	std::uint32_t                    frame    = 0;

	// 0..1 のかんたんな乱数。
	float random()
	{
		rng = rng * 1664525u + 1013904223u;
		return static_cast<float>((rng >> 8) & 0xFFFF) / 65535.0f;
	}

	// 起動時に 1 回だけ呼ばれる。
	void init()
	{
		playerX = kScreenW * 0.5f; playerY = kScreenH * 0.5f;
		hp = kMaxHp; hazards.clear();
		spawnIn = 0.0f; hitFlash = 0.0f; regenAcc = 0.0f;
	}

	// 毎フレーム呼ばれる。dt は前フレームからの経過秒。
	void update(Input in, float dt)
	{
		++frame;

		// 自機を上下左右に動かす。
		const float speed = 360.0f * dt;
		if (in.down(Key::Left))  { playerX -= speed; }
		if (in.down(Key::Right)) { playerX += speed; }
		if (in.down(Key::Up))    { playerY -= speed; }
		if (in.down(Key::Down))  { playerY += speed; }
		playerX = std::clamp(playerX, kPlayerR, kScreenW - kPlayerR);
		playerY = std::clamp(playerY, kPlayerR, kScreenH - kPlayerR);

		// ときどき危険物を上から出す (FixedVec が満杯なら出さない)。
		spawnIn -= dt;
		if (spawnIn <= 0.0f && !hazards.full())
		{
			spawnIn = 0.4f;
			Hazard h;
			h.x  = 40.0f + random() * (kScreenW - 80.0f);
			h.y  = -30.0f;
			h.vy = 180.0f + random() * 220.0f;
			hazards.push_back(h);
		}

		// 危険物を落とす + 当たり判定。当たったら HP を減らす。
		if (hitFlash > 0.0f) { hitFlash -= dt; }
		for (int i = 0; i < static_cast<int>(hazards.size()); )
		{
			Hazard& h = hazards[i];
			h.y += h.vy * dt;
			const float dx = h.x - playerX, dy = h.y - playerY;
			const float reach = kPlayerR + 18.0f;
			if (dx * dx + dy * dy < reach * reach)        // 自機に命中
			{
				hp -= 12; if (hp < 0) { hp = 0; }
				hitFlash = 0.25f;
				hazards.removeAt(i);                      // swap-remove (O(1))
				continue;
			}
			if (h.y > kScreenH + 30.0f) { hazards.removeAt(i); continue; }  // 下に抜けた
			++i;
		}

		// 被弾していない間はゆっくり回復 (HP graph が上下するように)。
		regenAcc += dt;
		if (regenAcc >= 0.5f) { regenAcc = 0.0f; if (hp < kMaxHp) { ++hp; } }
		if (hp == 0) { init(); }   // 力尽きたらリセット
	}

	// 毎フレーム呼ばれる。円と HP バーを描く。
	void draw(Screen& s)
	{
		s.fillScreen(hitFlash > 0.0f ? hex(0x3A1414) : hex(0x141826));
		for (const Hazard& h : hazards) { s.fillCircle(h.x, h.y, 18.0f, color::Red); }
		s.fillCircle(playerX, playerY, kPlayerR, color::Cyan);

		// HP バー (C++ で直接描く)。35 以下は赤 = inspector の danger ラインと一致。
		const float bw = 360.0f, bh = 22.0f, bx = 24.0f, by = 24.0f;
		s.drawRect(bx, by, bw, bh, hex(0x333845));
		const float frac = static_cast<float>(hp) / static_cast<float>(kMaxHp);
		s.drawRect(bx, by, bw * frac, bh, hp <= 35 ? color::Red : color::Green);
		s.text("HP " + std::to_string(hp), bx + bw + 14.0f, by - 2.0f, color::White, 22);

		s.text("move: arrows   ·   open the time-travel window with  --inspect timetravel",
		       24, kScreenH - 40, color::Gray, 18);
	}
};

// ── time-travel 観測 probe ──────────────────────────────────────────────────
// GameMemory から追跡したいスカラーを引く純関数。capture を持たない自由関数なので
// double(*)(const void*) へ変換でき、DLL 境界を安全に渡せる (ADR 0005)。
namespace
{
double hpProbe(const void* m)      { return static_cast<const TtDemo*>(m)->hp; }
double playerXProbe(const void* m) { return static_cast<const TtDemo*>(m)->playerX; }
}  // namespace

// これ 1 つで DLL の入口 + time-travel 観測が出来る (ADR 0017)。
// host が GameMemory ring × probe で HP / X 履歴を自動生成し、inspector に出す。
MITIRU_GAME_SERIES(TtDemo,
	{ "hp", "HP",       &hpProbe,      35.0, 1 },   // 35 を下抜けたら danger marker
	{ "x",  "Player X", &playerXProbe,  0.0, 0 });
