// html_menu。HTML の操作パネルを触ると、その操作が C++ に届き、C++ が描く図形が目に見えて変わる例。
//   左のパネルで色・形・数・傾き・間隔や、ふちどり・大きさ・影を選ぶと、右側に C++ が描いた図形が
//   その場で変わる。JavaScript は 1 行も書かない。
//   前章 html_hud が「C++ → HTML (値を見せる)」なら、この章は逆向きの「HTML → C++ (操作を伝える)」。
//   セットで examples/html_menu/assets/scene.html を読むと、操作を送り出す側が分かる。
// この章で使う仕組み: HTML の操作を受け取る in.action(...) / それに付いてきたデータを読む in.actionPayload(...) /
//                     C++ から HTML へ値を返す hud.set(...) / HTML 側の data-m-action・payload・repeat・confirm
#include <cmath>     // std::sqrt / std::fabs (さんかくの縁取りの計算)
#include <cstdint>   // std::uint32_t
#include <cstdio>    // std::snprintf (選択肢リストの JSON 組み立て)
#include <cstdlib>   // std::atoi
#include <cstring>   // std::strstr / std::strlen
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)
using namespace mitiru;

// 選べる色の一覧 (ただのデータ)。name はボタンの説明、rgb は図形の色 (パネルの色見本にも使う)。
// 形は 0=まる / 1=しかく / 2=さんかく の 3 種類。
struct Swatch { const char* name; std::uint32_t rgb; };
constexpr Swatch kColors[] = {
	{ "あお", 0x0A84FF }, { "もも", 0xE8338A }, { "みどり", 0x1FA654 },
	{ "だいだい", 0xFF9500 }, { "すみ", 0x33373E },
};
constexpr const char* kShapes[] = { "まる", "しかく", "さんかく" };
constexpr int kColorCount = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
constexpr int kShapeCount = static_cast<int>(sizeof(kShapes) / sizeof(kShapes[0]));

// HTML が送ってくる付随データ (例: {"id":2}) から、key の数値を 1 つ取り出す。
// 見つからなければ -1。届くのは自分の scene.html が送る形だけなので、簡単な探索で十分。
int payloadInt(const char* json, const char* key)
{
	char pat[16];
	std::snprintf(pat, sizeof(pat), "\"%s\":", key);
	const char* p = (json != nullptr) ? std::strstr(json, pat) : nullptr;
	return (p != nullptr) ? std::atoi(p + std::strlen(pat)) : -1;
}

struct HtmlMenu
{
	int  color = 0, shape = 0, count = 3;   // 選択中の色 / 形、描く個数 (1..5)
	int  tilt = 0, gap = 150;               // かたむき (度、0..45)、図形どうしの間隔 (90..180)
	bool outline = false, big = false, shadow = false;   // ふちどり / 大きく / 影

	void update(Input in, Hud hud, float)
	{
		if (in.cancelPressed()) { hud.quit(); }   // ESC で終わる
		// HTML パネルから届いた操作で、C++ の状態を書き換える。
		// 状態を持っているのはいつも C++ 側で、HTML は「こうしてほしい」と action で頼むだけ。
		if (in.action("pick.color")) { const int v = payloadInt(in.actionPayload("pick.color"), "id"); if (v >= 0 && v < kColorCount) { color = v; } }
		if (in.action("pick.shape")) { const int v = payloadInt(in.actionPayload("pick.shape"), "id"); if (v >= 0 && v < kShapeCount) { shape = v; } }
		if (in.action("set.count")) { const int v = payloadInt(in.actionPayload("set.count"), "v"); if (v >= 1 && v <= 5)   { count = v; } }
		if (in.action("set.tilt"))  { const int v = payloadInt(in.actionPayload("set.tilt"),  "v"); if (v >= 0 && v <= 45)  { tilt = v; } }
		if (in.action("set.gap"))   { const int v = payloadInt(in.actionPayload("set.gap"),   "v"); if (v >= 90 && v <= 180){ gap = v; } }
		if (in.action("toggle.outline")) { outline = !outline; }
		if (in.action("toggle.big"))     { big     = !big; }
		if (in.action("toggle.shadow"))  { shadow  = !shadow; }
		if (in.action("reset")) { color = 0; shape = 0; count = 3; tilt = 0; gap = 150; outline = false; big = false; shadow = false; }
		pushPanel(hud);
	}

	// パネルへ「選べる一覧」と「今の値」を送る。どれを選んでいるか (sel) の判断も C++ が行い、
	// HTML はその結果を見た目に反映するだけ。
	void pushPanel(Hud hud) const
	{
		char colors[384];
		int n = std::snprintf(colors, sizeof(colors), "[");
		for (int i = 0; i < kColorCount; ++i)
			n += std::snprintf(colors + n, sizeof(colors) - static_cast<std::size_t>(n),
			                   "%s{\"id\":%d,\"name\":\"%s\",\"hex\":\"%06X\",\"sel\":%d}",
			                   i ? "," : "", i, kColors[i].name, kColors[i].rgb, (i == color) ? 1 : 0);
		std::snprintf(colors + n, sizeof(colors) - static_cast<std::size_t>(n), "]");
		hud.set("view.colors", colors);

		char shapes[256];
		n = std::snprintf(shapes, sizeof(shapes), "[");
		for (int i = 0; i < kShapeCount; ++i)
			n += std::snprintf(shapes + n, sizeof(shapes) - static_cast<std::size_t>(n),
			                   "%s{\"id\":%d,\"name\":\"%s\",\"sel\":%d}", i ? "," : "", i, kShapes[i], (i == shape) ? 1 : 0);
		std::snprintf(shapes + n, sizeof(shapes) - static_cast<std::size_t>(n), "]");
		hud.set("view.shapes", shapes);
		hud.set("view.count", count);
		hud.set("view.tilt", tilt);
		hud.set("view.gap", gap);
		hud.set("view.outline", outline);
		hud.set("view.big", big);
		hud.set("view.shadow", shadow);
	}

	// 図形はすべて C++ が描く。パネルで選んだ値が、ここにそのまま反映される
	// (パネル操作 → C++ の状態 → この描画、という流れの終点)。
	void draw(Screen& s) const
	{
		s.drawGradientRect(Rect{0.0f, 0.0f, 1280.0f, 720.0f}, hex(0xF7F9FC), hex(0xE4EAF2));
		const Color c = hex(kColors[color].rgb);
		const float r = big ? 62.0f : 42.0f, g = static_cast<float>(gap), cy = 340.0f;
		const float x0 = 830.0f - g * static_cast<float>(count - 1) * 0.5f;   // パネル (左) を避け右側に一列
		const bool rot = (tilt != 0);   // 傾き 0 のときは回転をかけない (四角形をまっすぐな 1 枚として描ける)
		for (int i = 0; i < count; ++i)
		{
			const float x = x0 + g * static_cast<float>(i);
			if (shadow)   // 影: 図形を右下にずらして薄い墨色で先に描く。ふちどり時はその外形に合わせて広げる
			{
				const float sx = x + 9.0f, sy = cy + 13.0f;
				const Color sc = theme::kInk.withAlpha(0.16f);
				if (rot) { s.pushRotation(deg(static_cast<float>(tilt)), sx, sy); }
				if (outline) { drawOutline(s, sx, sy, r, 8.0f, sc); }
				else         { drawFill(s, sx, sy, r, sc); }
				if (rot) { s.popTransform(); }
			}
			if (rot) { s.pushRotation(deg(static_cast<float>(tilt)), x, cy); }   // 傾きを図形に適用
			if (outline) { drawOutline(s, x, cy, r, 8.0f, theme::kInk); }
			drawFill(s, x, cy, r, c);
			if (rot) { s.popTransform(); }
		}
		chapterTitle(s, "HTML Menu");
		chapterControls(s, "左のパネルの操作が、C++ が描く図形にそのまま反映される　ESC: おわる");
	}

	// 選択中の形で 1 つ描く (まる / しかく / さんかく)。
	void drawFill(Screen& s, float x, float y, float r, Color c) const
	{
		if (shape == 0)      { s.fillCircle(x, y, r, c); }
		else if (shape == 1) { s.drawRectCentered(x, y, r * 2.0f, r * 2.0f, c); }
		else                 { s.drawTriangle(Vec2{x, y - r}, Vec2{x - r, y + r}, Vec2{x + r, y + r}, c); }
	}

	// ふちどり: 図形を辺から均一に d だけ外へ広げて濃色で描く (後ろに敷く)。まる・しかくは
	// そのまま一回り大きくすればよい。さんかくは、内接円の中心から相似に広げると 3 辺が均一に
	// d だけ外へ出て、角も鋭いまま保たれる (単純に大きくすると辺ごとに太さが変わってしまう)。
	void drawOutline(Screen& s, float x, float y, float r, float d, Color c) const
	{
		if (shape == 0) { s.fillCircle(x, y, r + d, c); return; }
		if (shape == 1) { s.drawRectCentered(x, y, (r + d) * 2.0f, (r + d) * 2.0f, c); return; }
		const Vec2  v[3] = { {x, y - r}, {x - r, y + r}, {x + r, y + r} };
		auto len = [](Vec2 p, Vec2 q) { return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y)); };
		const float a = len(v[1], v[2]), b = len(v[2], v[0]), cc = len(v[0], v[1]), per = a + b + cc;
		const Vec2  inc{ (a * v[0].x + b * v[1].x + cc * v[2].x) / per, (a * v[0].y + b * v[1].y + cc * v[2].y) / per };
		const float area = 0.5f * std::fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y));
		const float rin = 2.0f * area / per;      // 内接円の半径
		const float k = (rin + d) / rin;          // 中心から k 倍に広げると、辺が d だけ外へ出る
		Vec2 g[3];
		for (int j = 0; j < 3; ++j) { g[j] = { inc.x + (v[j].x - inc.x) * k, inc.y + (v[j].y - inc.y) * k }; }
		s.drawTriangle(g[0], g[1], g[2], c);
	}
};

// 実行:  mitiru_host.exe html_menu/html_menu.dll
MITIRU_GAME(HtmlMenu);
