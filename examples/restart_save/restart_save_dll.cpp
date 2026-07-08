// restart_save — 描いた絵を「状態の塊」として もどす/やりなおす と セーブ/ロード する。
// 実行すると: マウスで線を描き、上のボタンで もどす / やりなおす / セーブ / ロード / さいしょから。
// 関連 API: Hud::save / load / requestRestart / マウス (in.mouseX / mouseY / mouseDown)
//   undo/redo と セーブ/ロードは同じ仕組み ―「状態を丸ごと控えて戻す」。undo/redo は 1 手ごとに
//   控えて戻す版、セーブ/ロードはその状態をまるごとファイル (save/slot0.msav) に写す版。

#include <cstddef>   // std::size_t
#include <cstdint>   // std::uint8_t

#include <mitiru.hpp>
#include <mitiru/core/FixedVec.hpp>    // 点と区切りを貯める固定長の配列

#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;

constexpr float kScreenW = 1280.0f, kScreenH = 720.0f;
constexpr int   kMaxPts     = 2500;   // 描ける点の上限
constexpr int   kMaxStrokes = 200;    // 描ける線 (ひと筆) の上限 = もどせる手数

// 線の 1 点。newStroke=1 なら「新しいひと筆の描き始め」(前の点とはつながない)。
struct Pt { float x = 0.0f, y = 0.0f; std::uint8_t newStroke = 0; };

// 画面上のボタン。5 個を右詰めで一列に置く。
struct Btn { float x, y, w, h; const char* label; };
constexpr float kBtnY = 18.0f, kBtnH = 44.0f, kBtnW = 138.0f, kStep = 150.0f;
constexpr float kBar0 = kScreenW - 20.0f - 5.0f * kBtnW - 4.0f * 12.0f;
constexpr Btn kUndo  = { kBar0 + 0.0f * kStep, kBtnY, kBtnW, kBtnH, "もどす" };
constexpr Btn kRedo  = { kBar0 + 1.0f * kStep, kBtnY, kBtnW, kBtnH, "やりなおす" };
constexpr Btn kSave  = { kBar0 + 2.0f * kStep, kBtnY, kBtnW, kBtnH, "セーブ" };
constexpr Btn kLoad  = { kBar0 + 3.0f * kStep, kBtnY, kBtnW, kBtnH, "ロード" };
constexpr Btn kReset = { kBar0 + 4.0f * kStep, kBtnY, kBtnW, kBtnH, "さいしょから" };
constexpr float kCanvasTop = 82.0f, kCanvasBot = kScreenH - 58.0f;   // 描ける範囲 (ボタンと操作帯を避ける)

// 「セーブした / ロードした」表示だけの一時値。ゲームの状態 (下の struct) には入れない ― 状態に
// 入れるとセーブで一緒にファイルへ書かれ、ロードで復元されて「ロードしたのにセーブしたと出る」バグ
// になる。保存したいのは絵だけ。見せるだけの値は状態と分ける、が save/load の勘どころ。
static float       sFlash    = 0.0f;         // 表示の残り時間
static const char* sFlashMsg = "";           // 何をしたか (セーブした / ロードした)
static Color       sFlashCol = theme::kInk;  // その色
static void flash(const char* msg, Color col) { sFlash = 1.2f; sFlashMsg = msg; sFlashCol = col; }

struct Paint15
{
	FixedVec<Pt, kMaxPts>      pts;    // 描いた線の点 ― これ全部が絵の状態
	FixedVec<int, kMaxStrokes> ends;   // 各ひと筆の終わりの点数 = もどす/やりなおすの区切り
	int   liveStrokes = 0;             // いま見えているひと筆の数 (もどすで減らし、やりなおすで増やす)
	bool  drawing     = false;         // いまマウスで描いている最中か
	bool  prevDown    = false;
	float lastX = 0.0f, lastY = 0.0f;

	static bool  hit(const Btn& b, float mx, float my) { return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h; }
	static bool  inCanvas(float my) { return my >= kCanvasTop && my <= kCanvasBot; }
	static float sq(float v) { return v * v; }
	std::size_t  livePts() const { return liveStrokes > 0 ? static_cast<std::size_t>(ends[liveStrokes - 1]) : 0; }

	void update(Input in, Hud hud, float dt)
	{
		const float mx = in.mouseX(), my = in.mouseY();
		const bool  down = in.mouseDown(0);

		if (down && !prevDown)   // 押した瞬間: ボタンか、キャンバスかで分ける
		{
			if      (hit(kUndo, mx, my))  { if (liveStrokes > 0) { --liveStrokes; } }                              // 1 手戻す
			else if (hit(kRedo, mx, my))  { if (liveStrokes < static_cast<int>(ends.size())) { ++liveStrokes; } }  // 1 手進める
			else if (hit(kSave, mx, my))  { hud.save("slot0"); flash("セーブした", theme::kGreen); } // 今の絵をまるごとファイルへ
			else if (hit(kLoad, mx, my))  { hud.load("slot0"); flash("ロードした", theme::kBlue); }  // ファイルから絵をまるごと戻す
			else if (hit(kReset, mx, my)) { hud.requestRestart(); sFlash = 0.0f; }     // 状態をまっさらに作り直してもらう
			else if (inCanvas(my))
			{
				// 新しいひと筆。もどして戻っていたなら、その先 (やりなおす分) を捨ててから描き足す。
				ends.truncate(static_cast<std::size_t>(liveStrokes));
				pts.truncate(livePts());
				(void)pts.push_back({mx, my, 1});
				drawing = true; lastX = mx; lastY = my;
			}
		}
		else if (down && drawing && inCanvas(my))   // ドラッグ中: 少し動いたら点を足す
		{
			if (sq(mx - lastX) + sq(my - lastY) > 9.0f && !pts.full())
			{
				(void)pts.push_back({mx, my, 0});
				lastX = mx; lastY = my;
			}
		}
		else if (!down && prevDown && drawing)   // 離した = ひと筆の完成。区切りを記録して 1 手にする
		{
			if (!ends.full()) { (void)ends.push_back(static_cast<int>(pts.size())); }
			liveStrokes = static_cast<int>(ends.size());
			drawing = false;
		}
		prevDown = down;
		if (sFlash > 0.0f) { sFlash -= dt; }
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);

		// 見えているひと筆までの点を描く (描いてる最中は今の筆も出す)。同じひと筆の連続する点を
		// 線でつなぎ、各点に小さな丸を置いて継ぎ目を滑らかにする。
		const std::size_t n = drawing ? pts.size() : livePts();
		for (std::size_t i = 0; i < n; ++i)
		{
			s.fillCircle(pts[i].x, pts[i].y, 1.8f, theme::kInk);
			if (pts[i].newStroke == 0 && i > 0)
			{
				s.drawLine(Vec2{pts[i - 1].x, pts[i - 1].y}, Vec2{pts[i].x, pts[i].y}, theme::kInk, 3.2f);
			}
		}

		drawBtn(s, kUndo,  theme::kSubtle);
		drawBtn(s, kRedo,  theme::kSubtle);
		drawBtn(s, kSave,  theme::kGreen);
		drawBtn(s, kLoad,  theme::kBlue);
		drawBtn(s, kReset, theme::kRed);

		chapterTitle(s, "Restart & Save");
		if (sFlash > 0.0f) { s.text(sFlashMsg, 24.0f, 92.0f, sFlashCol, 22); }
		chapterControls(s, "マウスで絵を描く　ボタン: もどす / やりなおす / セーブ / ロード / さいしょから");
	}

	void drawBtn(Screen& s, const Btn& b, Color c) const
	{
		s.drawRect(b.x, b.y, b.w, b.h, c.withAlpha(0.12f));
		s.drawRectFrame(Rect{b.x, b.y, b.w, b.h}, c, 1.5f);
		s.drawTextInRect(Rect{b.x, b.y, b.w, b.h}, b.label, c, 18.0f,
		                 Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
	}
};

// 実行:  mitiru_host.exe restart_save/restart_save.dll
MITIRU_GAME(Paint15);
