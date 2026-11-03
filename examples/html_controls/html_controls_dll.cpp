// html_controls。エンジン同梱の UI 部品 (select / tabset / checkbox / radio / range / progress) を 1 画面に並べる例。
//   左のパネルで玉の数・速さ・大きさ・ふちどり・影を選ぶと、右で C++ が描く玉がそのとおりに変わる。
//   部品の見た目は mitiru:components.rcss にあり、配色は tokens.rcss の変数を書き直すだけで変わる。
// この章で使う仕組み: in.action / in.actionPayload / hud.set / UI 側の <select> <tabset> <input type="checkbox|radio|range"> <progress>
#include <cmath>     // std::cos / std::sin
#include <cstdio>    // std::snprintf
#include <cstdlib>   // std::atoi
#include <cstring>   // std::strcmp
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)
using namespace mitiru;

// UI の部品は dispatch(名前, ev.value) のように値を 1 つだけ送る。届く文字は数なら "40"、語なら "\"fast\""、
// 真偽なら "true" なので、語は引用符ごと比べる。
bool payloadIs(const char* json, const char* word)
{
	char quoted[32];
	std::snprintf(quoted, sizeof(quoted), "\"%s\"", word);
	return json != nullptr && std::strcmp(json, quoted) == 0;
}

struct HtmlControls
{
	int   count = 3;        // <select> かず: 1 / 3 / 5
	int   speed = 1;        // <input type="radio"> はやさ: 0 おそい / 1 ふつう / 2 はやい
	int   radius = 40;      // <input type="range"> おおきさ (半径): 20..80
	bool  outline = false;  // <input type="checkbox"> ふちどり
	bool  shadow = false;   // <input type="checkbox"> かげ
	std::uint8_t _pad[2] = {};   // 暗黙の詰め物を残さない (状態をバイト単位で比べるため)
	float lap = 0.0f;       // 玉が輪を 1 周する進み具合 (0..1)。<progress> に出す

	void update(Input in, Hud hud, float dt)
	{
		if (in.cancelPressed()) { hud.quit(); }   // ESC で終わる
		// 部品を触ると、その値が action で届く。状態を持つのは C++ 側で、UI は「こうしてほしい」と頼むだけ。
		if (in.action("set.count")) { const int v = std::atoi(in.actionPayload("set.count")); if (v == 1 || v == 3 || v == 5) { count = v; } }
		if (in.action("set.radius"))  { const int v = std::atoi(in.actionPayload("set.radius"));  if (v >= 20 && v <= 80) { radius = v; } }
		if (in.action("set.speed"))
		{
			const char* v = in.actionPayload("set.speed");
			if (payloadIs(v, "slow")) { speed = 0; }
			if (payloadIs(v, "normal")) { speed = 1; }
			if (payloadIs(v, "fast")) { speed = 2; }
		}
		if (in.action("set.outline")) { outline = std::strcmp(in.actionPayload("set.outline"), "true") == 0; }
		if (in.action("set.shadow"))  { shadow  = std::strcmp(in.actionPayload("set.shadow"),  "true") == 0; }

		const float lapsPerSecond[3] = { 0.05f, 0.12f, 0.3f };
		lap += dt * lapsPerSecond[speed];
		lap -= std::floor(lap);   // 0 以上 1 未満に戻す (大きな dt で 1 周以上進んでも)
		pushPanel(hud);
	}

	// パネルへ今の値を送る。select と range は data-attr-value、checkbox と radio は data-checked で受ける。
	void pushPanel(Hud hud) const
	{
		const char* speedWord[3] = { "slow", "normal", "fast" };
		hud.set("view.count", count);
		hud.set("view.radius", radius);
		hud.set("view.speed", speedWord[speed]);
		hud.set("view.outline", outline);
		hud.set("view.shadow", shadow);
		hud.set("view.lap", lap);
	}

	// 玉はすべて C++ が描く。輪の上を count 個が等間隔で回り、半径・ふちどり・影がパネルの値で決まる。
	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.drawGradientRect(Rect{0.0f, 0.0f, 1280.0f, 720.0f}, hex(0xF7F9FC), hex(0xE4EAF2));
		const float cx = 860.0f, cy = 360.0f, ring = 190.0f, r = static_cast<float>(radius);
		s.drawCircleFrame(Vec2{cx, cy}, ring, theme::kFrame, 2.0f);
		for (int i = 0; i < count; ++i)
		{
			const float a = (lap + static_cast<float>(i) / static_cast<float>(count)) * 6.2831853f;
			const float x = cx + std::cos(a) * ring, y = cy + std::sin(a) * ring;
			if (shadow) { s.fillCircle(x + 8.0f, y + 12.0f, r + (outline ? 6.0f : 0.0f), theme::kInk.withAlpha(0.16f)); }
			if (outline) { s.fillCircle(x, y, r + 6.0f, theme::kInk); }
			s.fillCircle(x, y, r, theme::kBlue);
		}
		chapterTitle(s, "UI の部品");
		chapterControls(s, "左のパネルの部品で選ぶと、C++ が描く玉がそのとおりに変わる　ESC: おわる");
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};

// 実行:  mitiru_host.exe html_controls/html_controls.dll
MITIRU_REFLECT_AUTO(HtmlControls);

MITIRU_ASSERT_NO_PADDING(HtmlControls);
MITIRU_GAME(HtmlControls);
