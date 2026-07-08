// input — 入力の「今」を見せる章。キーボード / マウス / パッドの状態を、画面を 3 つに分けて光らせる。
//
// ゲームを動かす土台 (host) が毎フレーム、キー・マウス・パッドの状態を 1 つにまとめて渡してくれる。
// ゲーム側は、その渡された入力 (Input) を見るだけでよい。
#include <cstdint>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"       // 章ラベル (左上) と操作帯 (下端) の共通部品
using namespace mitiru;

// 配色
constexpr Color kFaint  = Color{0.11f, 0.11f, 0.12f, 0.16f};   // 目盛り線のごく薄い色
constexpr Color kKeyOff = hex(0xFFFFFF);                       // 押していないキーの面 (白)
constexpr Color kPanel  = Color{1.0f, 1.0f, 1.0f, 0.55f};      // 区画の下地 (淡い白カード)

// 3 つの区画の位置と大きさ (x, y, 幅, 高さ)
constexpr Rect  kKbBox{56.0f, 78.0f, 1168.0f, 232.0f};   // 上: キーボード (画面幅いっぱい)
constexpr Rect  kMsBox{56.0f, 336.0f, 572.0f, 332.0f};   // 左下: マウス
constexpr Rect  kPdBox{652.0f, 336.0f, 572.0f, 332.0f};  // 右下: パッド
constexpr Vec2  kStick{802.0f, 505.0f};  constexpr float kStickR = 88.0f;   // スティックを描く丸領域

// キーボード図の 1 キー。dir が 1..4 なら矢印 (上/下/左/右) を描き、cap は WASD の文字 (矢印キーは空文字)。
struct KeyCell { Key k; float x, y, w, h; int dir; const char* cap; };
constexpr KeyCell kKeys[] = {   // WASD は左側、矢印キーは右側に逆 T 字型、Space は下段中央
	{Key::W, 392, 108, 56, 56, 0, "W"},   {Key::A, 328, 172, 56, 56, 0, "A"},
	{Key::S, 392, 172, 56, 56, 0, "S"},   {Key::D, 456, 172, 56, 56, 0, "D"},
	{Key::Up,   832, 108, 56, 56, 1, ""}, {Key::Left,  768, 172, 56, 56, 3, ""},
	{Key::Down, 832, 172, 56, 56, 2, ""}, {Key::Right, 896, 172, 56, 56, 4, ""},
	{Key::Space, 330, 236, 620, 46, 0, ""}};

// パッドの 4 つのボタン。Xbox 配置 (A=緑で下 / B=赤で右 / X=青で左 / Y=黄で上) をひし形に並べる。
struct PadCell { Pad b; float cx, cy; Color col; const char* cap; };
constexpr PadCell kPad[] = {
	{Pad::Y, 1072, 459, theme::kAmber, "Y"}, {Pad::X, 1026, 505, theme::kBlue, "X"},
	{Pad::B, 1118, 505, theme::kRed,   "B"}, {Pad::A, 1072, 551, theme::kGreen, "A"}};

// ゲームの状態は、この構造体 1 つにまとめる。ポインタを持たない単純な構造体なので、
// host がそのままコピーして記録したり、あとで巻き戻したりできる。
struct Input04
{
	float         mx = 0.0f, my = 0.0f;  std::uint8_t mbtn = 0;   // マウス位置 / 押下ボタン (bit0=左 1=右 2=中)
	Vec2          trail[40] = {};  int head = 0, tn = 0;          // マウスの通った跡を貯める輪っか状のバッファ
	std::uint32_t keyBits = 0, padBits = 0;                       // 押下中のキー / パッドボタン (1 ビット = 1 個)
	bool          pad = false;  float lsx = 0.0f, lsy = 0.0f;     // パッド接続の有無 / 左スティックの傾き(+y=上)

	// update だけが状態を書き換える (draw は状態を読んで描くだけ)。
	// この章は時間経過を使わないので、前フレームからの秒数 dt は受け取るが使わない。
	void update(Input in, float /*dt*/)
	{
		mx = in.mouseX(); my = in.mouseY();
		trail[head] = Vec2{mx, my}; head = (head + 1) % 40; if (tn < 40) { ++tn; }

		// マウスボタンの押下を 1 つの数にまとめる (左=1, 右=2, 中=4)。
		mbtn = 0;
		if (in.mouseDown(0)) { mbtn |= 1; }   // 左
		if (in.mouseDown(1)) { mbtn |= 2; }   // 右
		if (in.mouseDown(2)) { mbtn |= 4; }   // 中

		// in.down(key) はそのキーが押されている間だけ true (離すとすぐ false に戻る)。
		// 押されているキーを keyBits に 1 ビットずつ記録する (i 番目のキー = i ビット目)。
		keyBits = 0;
		int i = 0;
		for (const KeyCell& c : kKeys) { if (in.down(c.k)) { keyBits |= (1u << i); } ++i; }

		pad = in.padConnected(); lsx = in.leftStick().x; lsy = in.leftStick().y;
		padBits = 0;
		int p = 0;
		for (const PadCell& c : kPad) { if (in.padDown(c.b)) { padBits |= (1u << p); } ++p; }
	}

	// 画面座標 (1280x720) を、マウス区画の内側 (枠から 22px) に縮めて対応づける。
	static Vec2 toBox(float x, float y)
	{
		const float u = x < 0.0f ? 0.0f : (x > 1280.0f ? 1280.0f : x);
		const float v = y < 0.0f ? 0.0f : (y > 720.0f ? 720.0f : y);
		return Vec2{kMsBox.x() + 22.0f + u / 1280.0f * (kMsBox.width() - 44.0f),
		            kMsBox.y() + 22.0f + v / 720.0f * (kMsBox.height() - 44.0f)};
	}

	// 矢印キーの中に、その向きの三角形を描く。
	void arrow(Screen& s, const Rect& r, int dir, Color c) const
	{
		const float x = r.x() + r.width() * 0.5f, y = r.y() + r.height() * 0.5f, e = 12.0f;
		if      (dir == 1) { s.drawTriangle({x, y - e}, {x - e, y + e}, {x + e, y + e}, c); }   // 上
		else if (dir == 2) { s.drawTriangle({x, y + e}, {x - e, y - e}, {x + e, y - e}, c); }   // 下
		else if (dir == 3) { s.drawTriangle({x - e, y}, {x + e, y - e}, {x + e, y + e}, c); }   // 左
		else               { s.drawTriangle({x + e, y}, {x - e, y - e}, {x - e, y + e}, c); }   // 右
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "Input");

		// 3 つの区画の下地カードと枠を描く。
		for (const Rect& b : {kKbBox, kMsBox, kPdBox})
		{ s.drawRoundedRect(b, kPanel, 14.0f); s.drawRoundedRectFrame(b, theme::kFrame, 14.0f, 1.5f); }

		// キーボード図: 押されているキーだけ青く光る (離すと消える)。
		int i = 0;
		for (const KeyCell& c : kKeys)
		{
			const Rect r{c.x, c.y, c.w, c.h};
			const bool lit = (keyBits >> i++) & 1u;
			s.drawRoundedRect(r, lit ? theme::kBlue : kKeyOff, 8.0f);
			s.drawRoundedRectFrame(r, lit ? theme::kBlue : theme::kFrame, 8.0f, 1.5f);
			const Color gc = lit ? kKeyOff : theme::kInk;
			if (c.dir) { arrow(s, r, c.dir, gc); }
			else if (c.cap[0]) { s.drawTextInRect(r, c.cap, gc, 22.0f, Screen::TextAlignH::Center, Screen::TextAlignV::Middle); }
		}

		// マウス区画: 動いた跡を薄い点で引き、いまの位置に丸を置く (どちらも区画内に収める)。
		const Vec2 m = toBox(mx, my);
		for (int j = 0; j < tn; ++j)
		{
			const int idx = (head - 1 - j + 80) % 40;         // 新しい点から順にたどる (+80 は負にしないため)
			const float fade = 1.0f - float(j) / float(tn);   // 古い点ほど薄く小さく
			const Vec2 t = toBox(trail[idx].x, trail[idx].y);
			Color tc = theme::kPink;
			tc.a = 0.30f * fade;
			s.fillCircle(t.x, t.y, 2.0f + 3.0f * fade, tc);
		}
		// 押されているボタンで丸の色が変わり、少し大きくなる。
		Color mk = theme::kBlue;                      // 何も押していないとき
		if      (mbtn & 1) { mk = theme::kOrange; }   // 左
		else if (mbtn & 2) { mk = theme::kPink; }     // 右
		else if (mbtn & 4) { mk = theme::kGreen; }    // 中
		s.fillCircle(m.x, m.y, mbtn ? 10.0f : 6.0f, mk);

		// パッド区画: 左スティックの傾きを、丸い領域の中の点で示す。
		s.drawCircleFrame(kStick, kStickR, theme::kFrame, 1.5f);
		s.line(kStick.x - kStickR, kStick.y, kStick.x + kStickR, kStick.y, kFaint, 1.0f);   // 横の目盛り
		s.line(kStick.x, kStick.y - kStickR, kStick.x, kStick.y + kStickR, kFaint, 1.0f);   // 縦の目盛り
		s.fillCircle(kStick.x + lsx * kStickR * 0.82f, kStick.y - lsy * kStickR * 0.82f, 11.0f, pad ? theme::kGreen : theme::kFrame);

		// 顔ボタン (A/B/X/Y): 押されているボタンだけ塗りつぶす。
		int p = 0;
		for (const PadCell& c : kPad)
		{
			const bool lit = (padBits >> p++) & 1u;
			const Color col = pad ? c.col : theme::kFrame;
			s.fillCircle(c.cx, c.cy, 22.0f, lit ? col : kKeyOff);
			s.drawCircleFrame({c.cx, c.cy}, 22.0f, col, 1.5f);
			s.drawTextInRect(Rect{c.cx - 22.0f, c.cy - 22.0f, 44.0f, 44.0f}, c.cap, lit ? kKeyOff : (pad ? col : theme::kSubtle),
			                 18.0f, Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
		}

		// パッドが繋がっていないときは、薄い色で一言そえる。
		if (!pad)
			s.drawTextInRect(Rect{kPdBox.x(), kStick.y + kStickR + 16.0f, kPdBox.width(), 22.0f}, "パッド みつからない",
			                 theme::kSubtle, 15.0f, Screen::TextAlignH::Center, Screen::TextAlignV::Middle);

		chapterControls(s, "キー: おすと図が光る　マウス: うごかす／クリック　パッド: スティックとボタン");
	}
};

// この構造体を DLL の入口に結びつける (これ 1 行でゲームとして読み込めるようになる)。
MITIRU_GAME(Input04);
