// html_hud — C++ が持っている 1 つの数値を、HTML の 6 つの表示へ同時に映す例。
//   下のスライダー (これは C++ が描いている) を動かすと level という 1 つの値が変わり、
//   その値を HTML へ送ると、上に重なった 6 つの表示 (ゲージ・数字・バー・pips・色・折れ線) が
//   一斉に同じ値へ動く。JavaScript は 1 行も書かない。HTML に data-m-... と書いておくだけで、
//   エンジンが「C++ の値を HTML へ自動で反映」してくれる。
//   セットで examples/html_hud/assets/scene.html を読むと、値を受け取って表示する側が分かる。
// この章で使う仕組み: マウス入力 (Input) / C++ から HTML へ値を送る hud.set(...) /
//                     HTML 側の data-m-text・style・class・repeat・spark-push
#include <algorithm>   // std::clamp
#include <cstdio>      // std::snprintf (pips を JSON 配列に組む)
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 + 共通パレット
using namespace mitiru;

// スライダーの寸法 (論理解像度 1280x720)。トラックは画面下、その上に HTML 表示の領域を空ける。
constexpr float kTrackL = 300.0f, kTrackR = 980.0f, kTrackY = 600.0f, kThumbR = 20.0f;

// この章が持つ状態は、たった 2 つ。level が「画面じゅうの表示が映す唯一の数値」で、
// dragging は「今つまみをつかんでいるか」。この level 1 つを HTML の 6 表示すべてへ
// 送って見せるのが、この章の主題。
struct HtmlHud
{
	float level    = 42.0f;   // 0..100。全ての表示ウィジェットが映す唯一の値
	bool  dragging = false;   // つまみをつかんでいる間 true

	void update(Input in, Hud hud, float)
	{
		if (in.cancelPressed()) { hud.quit(); }   // ESC で終わる
		const float mx = in.mouseX(), my = in.mouseY();
		if (in.mousePressed(0) && onTrack(mx, my)) { dragging = true; }   // つまみ / トラックをつかむ
		if (!in.mouseDown(0)) { dragging = false; }
		if (dragging) { level = std::clamp((mx - kTrackL) / (kTrackR - kTrackL) * 100.0f, 0.0f, 100.0f); }
		pushHud(hud);
	}
	// トラック周辺 (つかみやすい広めの帯) にマウスがあるか。
	static bool onTrack(float x, float y)
	{
		return x >= kTrackL - 30.0f && x <= kTrackR + 30.0f && y >= kTrackY - 44.0f && y <= kTrackY + 44.0f;
	}
	// level を HTML へ送る。送るのは level 本体と、level から作った pips の列 (10 個のうち N 個を点灯) の 2 つだけ。
	// pips も level だけから決まるので、実質「1 つの値」を送っているのと同じ。
	// HTML 側はこの値を受け取って表示を更新するだけで、計算は C++ 側で終わっている。
	void pushHud(Hud hud) const
	{
		const int v = static_cast<int>(level + 0.5f);
		hud.set("view.level", v);
		char json[192];
		int n = std::snprintf(json, sizeof(json), "[");
		for (int i = 0; i < 10; ++i)
		{
			n += std::snprintf(json + n, sizeof(json) - static_cast<std::size_t>(n),
			                   "%s{\"on\":%d}", i ? "," : "", (i * 10 < v) ? 1 : 0);
		}
		std::snprintf(json + n, sizeof(json) - static_cast<std::size_t>(n), "]");
		hud.set("view.pips", json);
	}
	// C++ が直接描くのは、背景とスライダー (レール + 塗り + つまみ) だけ。
	// 6 つの値表示は、上に重なった HTML/CSS が受け持つ。
	// つまり「下のスライダー = 値を決める場所」「上の HTML = その値を見せる場所」。
	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		const float thumbX = kTrackL + level / 100.0f * (kTrackR - kTrackL);
		s.drawRoundedRect(Rect{kTrackL, kTrackY - 7.0f, kTrackR - kTrackL, 14.0f}, theme::kFrame, 7.0f);     // トラック
		s.drawRoundedRect(Rect{kTrackL, kTrackY - 7.0f, thumbX - kTrackL, 14.0f}, theme::kBlue, 7.0f);       // 塗り (0→つまみ)
		s.fillCircle(thumbX, kTrackY, kThumbR + 5.0f, rgba(10, 132, 255, dragging ? 70 : 32));               // つまみの暈
		s.fillCircle(thumbX, kTrackY, kThumbR, theme::kBlue);                                                // つまみ
		s.drawRing(Vec2{thumbX, kTrackY}, kThumbR, kThumbR - 6.0f, rgba(255, 255, 255, 235));                // つまみの白縁
		chapterTitle(s, "HTML HUD");
		chapterControls(s, "スライダーを うごかす　ESC: おわる");
	}
};
// 実行:  mitiru_host.exe html_hud/html_hud.dll
MITIRU_GAME(HtmlHud);
