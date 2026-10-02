// html_hud は、C++ が持っている 1 つの数値を、UI の 6 つの表示へ同時に反映する例。
//   下のスライダー (これは C++ が描いている) を動かすと level という 1 つの値が変わり、
//   その値を UI へ送ると、上に重なった 6 つの表示 (ゲージ・数字・バー・pips・色・折れ線) が
//   一斉に同じ値へ動く。UI は RML / RCSS (HTML / CSS の方言) で書き、{{ level }} や data-class-... と
//   書いておくだけで、エンジンが C++ の値を UI へ自動で反映してくれる。スクリプトは書かない。
//   セットで examples/html_hud/assets/ui/main.rml を読むと、値を受け取って表示する側が分かる。
// この章で使う仕組みは、マウス入力 (Input) / C++ から UI へ値を送る hud.set(...) /
//                     UI 側の {{ }}・data-class・data-style・data-for・spark
#include <algorithm>   // std::clamp
#include <cstdio>      // std::snprintf (pips を JSON 配列に組む)
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 + 共通パレット
using namespace mitiru;

// スライダーの寸法 (論理解像度 1280x720)。トラックは画面の下に置き、その上に UI 表示の領域を空ける。
constexpr float kTrackL = 300.0f, kTrackR = 980.0f, kTrackY = 600.0f, kThumbR = 20.0f;

// この章が持つ状態は、たった 2 つ。level は「画面じゅうの表示に反映する唯一の数値」で、
// dragging は「今つまみをつかんでいるか」を表す。この level 1 つを UI の 6 つの表示すべてへ
// 送って見せることが、この章の主題。
struct HtmlHud
{
	float level    = 42.0f;   // 0..100。全ての表示ウィジェットが映す唯一の値
	bool  dragging = false;   // つまみをつかんでいる間 true
	std::uint8_t _pad[3] = {};   // 暗黙の詰め物を残さない (状態をバイト単位で比べるため)

	void update(Input in, Hud hud, float)
	{
		if (in.cancelPressed()) { hud.quit(); }   // ESC で終わる
		const float mx = in.mouseX(), my = in.mouseY();
		if (in.mousePressed(0) && onTrack(mx, my)) { dragging = true; }   // つまみ / トラックをつかむ
		if (!in.mouseDown(0)) { dragging = false; }
		if (dragging) { level = std::clamp((mx - kTrackL) / (kTrackR - kTrackL) * 100.0f, 0.0f, 100.0f); }
		pushHud(hud);
	}
	// マウスがトラック周辺 (つかみやすい広めの帯) にあるか。
	static bool onTrack(float x, float y)
	{
		return x >= kTrackL - 30.0f && x <= kTrackR + 30.0f && y >= kTrackY - 44.0f && y <= kTrackY + 44.0f;
	}
	// level を UI へ送る。送るのは level 本体と、level から作った pips の列 (10 個のうち N 個を点灯) の 2 つだけ。
	// pips も level だけから決まるので、実質的には「1 つの値」を送っているのと同じ。
	// UI 側はこの値を受け取って表示を更新するだけで、計算は C++ 側で終わっている。
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
	// 6 つの値表示は、上に重なった UI (RML / RCSS) が受け持つ。
	// つまり、「下のスライダー = 値を決める場所」「上の UI = その値を見せる場所」。
	template <class Surface>
	void drawImpl(Surface& s) const
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
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};
// 実行:  mitiru_host.exe html_hud/html_hud.dll
// inspector に映す状態を自動反射する。aggregate 型なので列挙不要 (D12)。
MITIRU_REFLECT_AUTO(HtmlHud);

MITIRU_ASSERT_NO_PADDING(HtmlHud);
MITIRU_GAME(HtmlHud);
