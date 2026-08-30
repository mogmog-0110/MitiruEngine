// sprites。画像 (スプライト) を描く章。赤べこ (会津の張り子牛) を、拡大・回転・反転・半透明で見せ、歩かせる。
// 実行すると: 上段にスプライトの機能見本 (大きさ / 回転 / 左右反転 / 半透明)、下段で矢印キーで歩く赤べこ。
// 使う機能: Texture::fromFile (画像読み込み) / drawSprite (flipX・色/透明度) / pushRotation (回転) / in.move() / Timer

#include <mitiru.hpp>
#include <mitiru/render/Texture.hpp>   // 画像を直接渡して描く drawSprite のため
#include "../common/chapter_hud.hpp"   // 章の名前ラベル (左上) と操作帯 (下端) の共通ヘルパー

using namespace mitiru;

// 赤べこの絵は 4 コマ (歩行サイクル)。順に切り替えると、短い脚が前後に動いて歩いて見える。
// 画像 (Texture) は内部に可変長データを持つので、ゲームの状態 struct には入れられない。
// (状態 struct はポインタを持たない単純なデータの塊に保つ決まりのため。) そこで画像は、
// このファイル直下の変数へ開始時に一度だけ読み込む。読み込みに失敗しても value_or で
// 「空の画像」を入れるので、何も描かないだけで落ちない。
static const render::Texture kFrames[4] = {
	render::Texture::fromFile("sprites/assets/sprites/akabeko_0.png").value_or(render::Texture{}),
	render::Texture::fromFile("sprites/assets/sprites/akabeko_1.png").value_or(render::Texture{}),
	render::Texture::fromFile("sprites/assets/sprites/akabeko_2.png").value_or(render::Texture{}),
	render::Texture::fromFile("sprites/assets/sprites/akabeko_3.png").value_or(render::Texture{}),
};

constexpr float kSpeed   = 260.0f;   // 歩く速さ (1 秒あたりのピクセル)
constexpr float kWalk    = 0.575f;   // 歩く赤べこの大きさ (画像の何倍か。絵が高解像度なので 1 未満で縮小して使う)
constexpr float kStepSec = 0.13f;    // 歩行コマを 1 つ進める間隔 (秒)

// 上段の機能見本を並べる 4 つのマス (左端 x)。幅 293・高さ 196。
constexpr float       kCellY = 92.0f, kCellW = 293.0f, kCellH = 196.0f;
constexpr float       kCellX[4]   = {24.0f, 337.0f, 650.0f, 963.0f};
constexpr const char* kCellLbl[4] = {"scale", "rotate", "flipX", "alpha"};

struct Sprites06
{
	float cx = 640.0f, cy = 480.0f;   // 歩く赤べこの中心
	bool  faceLeft = false;           // 進む向き (左を向いているか)
	Timer walk;                       // 歩行アニメ用のタイマー (単純な値なので状態に置ける)
	int   step = 0;                   // 今の歩行コマ (0〜3)
	float t    = 0.0f;                // 経過秒 (回転の見本に使う)

	static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	float texW() const { return static_cast<float>(kFrames[0].width()); }
	float texH() const { return static_cast<float>(kFrames[0].height()); }

	// update だけが状態を書き換える (draw は状態を読んで描くだけ)。
	void update(Input in, float dt)
	{
		t += dt;
		const Stick m = in.move();     // 矢印キー・WASD・パッドを 1 つのベクトルにまとめてくれる
		cx += m.x * kSpeed * dt;
		cy += m.y * kSpeed * dt;
		if      (m.x < -0.1f) { faceLeft = true;  }   // 左へ動いたら左向き
		else if (m.x >  0.1f) { faceLeft = false; }   // 右へ動いたら右向き

		// 下段の歩ける範囲に収める (上段の見本と重ならないよう y は下側に限る)。
		const float halfW = texW() * kWalk * 0.5f, halfH = texH() * kWalk * 0.5f;
		cx = clampf(cx, halfW, 1280.0f - halfW);
		cy = clampf(cy, 380.0f, 720.0f - 34.0f - halfH);

		// 動いている間だけ歩行コマを順送り。止まっているときは休みの姿 (コマ 0) に戻す。
		const bool moving = (m.x * m.x + m.y * m.y) > 0.01f;
		if (moving) { if (walk.every(kStepSec, dt)) { step = (step + 1) % 4; } }
		else        { step = 0; }
	}

	// 赤べこ 1 匹を中心 (x,y)・大きさ scale で描く。flip で左右反転、tint で色/透明度、rotDeg で回転。
	void beko(Screen& s, int frame, float x, float y, float scale,
	          bool flip = false, Color tint = color::White, float rotDeg = 0.0f) const
	{
		const render::Texture& tex = kFrames[frame];
		const float w = static_cast<float>(tex.width()) * scale;
		const float h = static_cast<float>(tex.height()) * scale;
		const Rect  dst{x - w * 0.5f, y - h * 0.5f, w, h};
		const Rect  src{0.0f, 0.0f, static_cast<float>(tex.width()), static_cast<float>(tex.height())};
		if (rotDeg != 0.0f) { s.pushRotation(deg(rotDeg), x, y); }   // これから描く絵を x,y 中心に回す
		s.drawSprite(tex, dst, src, tint, flip);
		if (rotDeg != 0.0f) { s.popTransform(); }                    // 回転を元に戻す
	}

	// 機能見本のマス 1 つ (枠 + 上にラベル)。中の絵は draw() 側で描く。
	void cell(Screen& s, int i) const
	{
		s.drawRectFrame(Rect{kCellX[i], kCellY, kCellW, kCellH}, theme::kFrame, 1.0f);
		s.drawTextInRect(Rect{kCellX[i], kCellY + 8.0f, kCellW, 22.0f}, kCellLbl[i],
		                 theme::kInk, 16.0f, Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "Sprites");

		for (int i = 0; i < 4; ++i) { cell(s, i); }
		const float midY = kCellY + kCellH * 0.5f + 16.0f;   // マス内で絵を置く高さ

		// scale: 同じ絵を 小 と 大 で並べる (drawSprite の拡大率のちがい)。
		beko(s, 0, kCellX[0] + 96.0f,  midY + 12.0f, 0.225f);
		beko(s, 0, kCellX[0] + 205.0f, midY,         0.425f);

		// rotate: 1 匹をゆっくり回す。
		beko(s, 0, kCellX[1] + kCellW * 0.5f, midY, 0.3f, false, color::White, t * 70.0f);

		// flipX: 右向きと左向きを向かい合わせに。
		beko(s, 0, kCellX[2] + 92.0f,  midY, 0.3f, false);   // 右向き
		beko(s, 0, kCellX[2] + 200.0f, midY, 0.3f, true);    // 左向き (flipX)

		// alpha: 色の透明度を下げて半透明に。
		beko(s, 0, kCellX[3] + kCellW * 0.5f, midY, 0.3f, false, color::White.withAlpha(0.35f));

		// ── 主役: 矢印キーで動く赤べこ。進む向きに反転し、短い脚が交互に動いて歩く。──
		beko(s, step, cx, cy, kWalk, faceLeft);

		chapterControls(s, "矢印キー: 赤べこを あるかせる　（左右で むきが かわる／脚が 交互に うごく）");
	}
};

// 実行:  mitiru_host.exe sprites/sprites.dll
MITIRU_GAME(Sprites06);
