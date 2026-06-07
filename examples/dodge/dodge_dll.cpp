// dodge — 初心者向けの C++ だけで作るサンプル (よけゲーム)
//
// HTML を一切使わず、C++ だけでゲームになる最小例。覚えることは 3 つ:
//   1. 状態は struct にまとめて持つ          (DodgeGame)
//   2. update で毎フレーム動かす            (入力を読む・位置を進める・当たり判定)
//   3. draw で画面に描く                    (四角と文字)
// void* も生ポインタも出てこない。<mitiru/module/Game.hpp> のラッパが隠している。

#include <string>

#include <mitiru/module/Game.hpp>

using namespace mitiru;

// ── 画面と大きさ ────────────────────────────────────────────────────────────
constexpr float kScreenW   = 1280.0f;
constexpr float kScreenH   = 720.0f;
constexpr int   kMaxBlocks = 24;        // 同時に出せるブロックの上限
constexpr float kPlayerW   = 56.0f;
constexpr float kPlayerH   = 24.0f;
constexpr float kPlayerY   = 660.0f;    // 自機は画面下に固定
constexpr float kBlock     = 44.0f;

// 落ちてくるブロック 1 個。
struct Block { float x, y; bool alive; };

// ── ゲームの状態 (ぜんぶここ) ───────────────────────────────────────────────
struct DodgeGame
{
	float    playerX  = kScreenW * 0.5f;  // 自機の左右位置
	Block    blocks[kMaxBlocks] {};
	int      score    = 0;                // 生き残った時間 (0.1 秒 = 1 点)
	float    timeAlive = 0.0f;
	bool     over     = false;
	float    spawnIn  = 0.0f;             // 次のブロックを出すまでの秒
	unsigned rng      = 0x1234567u;       // かんたんな乱数

	// 0..1 のかんたんな乱数。
	float random()
	{
		rng = rng * 1664525u + 1013904223u;
		return static_cast<float>((rng >> 8) & 0xFFFF) / 65535.0f;
	}

	// 最初の状態に戻す。
	void reset()
	{
		playerX = kScreenW * 0.5f;
		timeAlive = 0.0f; score = 0; over = false; spawnIn = 0.0f;
		for (Block& b : blocks) { b.alive = false; }
	}

	// 起動時に 1 回だけ呼ばれる。
	void init() { reset(); }

	// 毎フレーム呼ばれる。dt は前フレームからの経過秒。
	void update(Input in, float dt)
	{
		if (over)
		{
			// ゲームオーバー中: Space か Enter でやり直し。
			if (in.pressed(Key::Space) || in.pressed(Key::Enter)) { reset(); }
			return;
		}

		// 自機を左右に動かす。
		const float speed = 520.0f * dt;
		if (in.down(Key::Left))  { playerX -= speed; }
		if (in.down(Key::Right)) { playerX += speed; }
		if (playerX < kPlayerW * 0.5f)            { playerX = kPlayerW * 0.5f; }
		if (playerX > kScreenW - kPlayerW * 0.5f) { playerX = kScreenW - kPlayerW * 0.5f; }

		// ときどき新しいブロックを上から出す。
		spawnIn -= dt;
		if (spawnIn <= 0.0f)
		{
			spawnIn = 0.55f;
			for (Block& b : blocks)
			{
				if (!b.alive) { b.alive = true; b.x = 40.0f + random() * (kScreenW - 80.0f); b.y = -kBlock; break; }
			}
		}

		// ブロックを落とす + 自機に当たったかを見る。
		const float fall = 330.0f * dt;
		for (Block& b : blocks)
		{
			if (!b.alive) { continue; }
			b.y += fall;
			if (b.y > kScreenH) { b.alive = false; continue; }   // 下に抜けたら消す

			// 矩形どうしの当たり判定はエンジンに任せる（手計算しない）。
			const sgc::Rectf playerBox{playerX - kPlayerW * 0.5f, kPlayerY, kPlayerW, kPlayerH};
			const sgc::Rectf blockBox{b.x, b.y, kBlock, kBlock};
			if (playerBox.intersects(blockBox)) { over = true; }
		}

		// 生き残った時間がスコア。
		timeAlive += dt;
		score = static_cast<int>(timeAlive * 10.0f);
	}

	// 毎フレーム呼ばれる。四角と文字を画面に描く。
	void draw(Screen& s)
	{
		s.fillScreen(hex(0x14182A));                                // 背景を塗る
		for (const Block& b : blocks)                               // 落ちてくるブロック
		{
			if (b.alive) { s.drawRect(b.x, b.y, kBlock, kBlock, color::Red); }
		}
		s.drawRectCentered(playerX, kPlayerY + kPlayerH * 0.5f,     // 自機
		                   kPlayerW, kPlayerH, color::Cyan);

		// スコアは C++ で直接描く (まだ HTML は使わない)。
		s.text("SCORE " + std::to_string(score), 24, 20, color::White, 24);

		if (over)
		{
			s.text("GAME OVER", kScreenW * 0.5f - 92, kScreenH * 0.5f - 36, color::White, 40);
			s.text("press SPACE to retry", kScreenW * 0.5f - 104, kScreenH * 0.5f + 14, color::Gray, 20);
		}
	}
};

// これ 1 行で DLL の入口が出来る。
MITIRU_GAME(DodgeGame)
