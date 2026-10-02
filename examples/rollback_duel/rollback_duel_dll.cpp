// rollback_duel。「ローカル 2 人対戦が、そのままロールバックのオンライン対戦になる」章
//   1 台のキーボードを 2 人で分けて使うテニス (1P = W/S、2P = ↑/↓)。ゲームは回線を知らない。
//   状態はすべてこの struct (GameMemory) にあり、update は入力だけで決まるので、
//   mitiru_rollback によって GameMemory が memcpy で保存・復元され、巻き戻しが行われても、2 人の画面はずれない。
// 遊び方: mitiru_host でそのまま 2 人で遊べる。ロールバック対戦の確かめ方は docs/ROLLBACK_NETCODE.md
// この章で使う関数: in.down (1P/2P のキー) / drawRectCentered / fillCircle / drawTextInRect

#include <cmath>
#include <cstdio>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

namespace
{
constexpr float kW = 1280.0f, kH = 720.0f;
constexpr float kPaddleH = 120.0f, kPaddleW = 18.0f, kPaddleX = 70.0f;
constexpr float kBallR = 11.0f;
constexpr float kPaddleSpeed = 520.0f;   // px/s
constexpr float kServeSpeed = 420.0f;
} // namespace

struct RollbackDuel
{
	float paddleY[2] = {kH * 0.5f, kH * 0.5f};
	float bx = kW * 0.5f, by = kH * 0.5f;
	float vx = kServeSpeed, vy = 140.0f;
	int score[2] = {0, 0};
	int rally = 0;   // 打ち返した回数 (速くなる)

	void init() { *this = RollbackDuel{}; }

	void serve(int towardPlayer)
	{
		bx = kW * 0.5f; by = kH * 0.5f;
		vx = towardPlayer == 0 ? -kServeSpeed : kServeSpeed;
		vy = (score[0] + score[1]) % 2 == 0 ? 140.0f : -140.0f;
		rally = 0;
	}

	void movePaddle(int p, bool up, bool down, float dt)
	{
		const float dir = (down ? 1.0f : 0.0f) - (up ? 1.0f : 0.0f);
		const float half = kPaddleH * 0.5f;
		paddleY[p] = std::fmin(std::fmax(paddleY[p] + dir * kPaddleSpeed * dt, half), kH - half);
	}

	/// 打ち返す: 当たった位置に応じてボールの縦向きを変え、打つたびに少し速くする
	void bounceOff(int p)
	{
		const float off = (by - paddleY[p]) / (kPaddleH * 0.5f);
		const float speed = kServeSpeed + 35.0f * static_cast<float>(++rally);
		vx = p == 0 ? speed : -speed;
		vy = off * speed * 0.75f;
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		movePaddle(0, in.down(Key::W), in.down(Key::S), dt);
		movePaddle(1, in.down(Key::Up), in.down(Key::Down), dt);

		bx += vx * dt;
		by += vy * dt;
		if (by < kBallR) { by = kBallR; vy = -vy; }
		if (by > kH - kBallR) { by = kH - kBallR; vy = -vy; }
		for (int p = 0; p < 2; ++p)
		{
			const float px = p == 0 ? kPaddleX : kW - kPaddleX;
			const bool facing = p == 0 ? vx < 0.0f : vx > 0.0f;
			const bool hit = std::fabs(bx - px) < kPaddleW * 0.5f + kBallR && std::fabs(by - paddleY[p]) < kPaddleH * 0.5f + kBallR;
			if (facing && hit) { bounceOff(p); }
		}
		if (bx < -kBallR) { ++score[1]; serve(0); }
		if (bx > kW + kBallR) { ++score[0]; serve(1); }
	}

	void draw(Screen& s) const
	{
		s.fillScreen(hex(0x14202B));
		for (float y = 20.0f; y < kH; y += 48.0f) { s.drawRectCentered(kW * 0.5f, y, 6.0f, 24.0f, hex(0x2E4456)); }
		s.drawRectCentered(kPaddleX, paddleY[0], kPaddleW, kPaddleH, hex(0x4C95F2));
		s.drawRectCentered(kW - kPaddleX, paddleY[1], kPaddleW, kPaddleH, hex(0xE5484D));
		s.fillCircle(bx, by, kBallR, hex(0xF5E6C8));

		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", score[0]);
		s.drawTextInRect({kW * 0.5f - 220.0f, 40.0f, 160.0f, 80.0f}, buf, hex(0x4C95F2), 64.0f);
		std::snprintf(buf, sizeof(buf), "%d", score[1]);
		s.drawTextInRect({kW * 0.5f + 60.0f, 40.0f, 160.0f, 80.0f}, buf, hex(0xE5484D), 64.0f);

		chapterTitle(s, "Rollback Duel");
		chapterControls(s, "1P: W / S　2P: ↑ / ↓　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe rollback_duel/rollback_duel.dll            (1 台で 2 人)
//        mitiru_rollback.exe rollback_duel/rollback_duel.dll --latency 6  (loopback のロールバック対戦で検証)
MITIRU_REFLECT_AUTO(RollbackDuel);

MITIRU_ASSERT_NO_PADDING(RollbackDuel);
MITIRU_GAME(RollbackDuel);
