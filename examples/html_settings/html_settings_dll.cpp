// html_settings。エンジン同梱の設定画面 (mitiru:settings.rml) を、ゲームの画面に重ねて開く例。
//   玉が Space で跳ね、Shift でダッシュする。右上の「設定」(パッドは Start) で設定画面が開き、音量・画面・見やすさ・
//   キー割り当てを変えられる。変えた値は host がすぐ反映して保存する。割り当てを変えると、その入力で玉が動く。
// この章で使う仕組み: <include src="mitiru:settings.rml"/> / hud.set("view.settings_open") / MITIRU_ACTIONS / in.actionPressed
#include <cmath>     // std::fabs
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)
using namespace mitiru;

// 操作の表 (下の MITIRU_ACTIONS) と同じ並び。番号で in.actionPressed(Act::Jump) と読む。
enum class Act { Jump, Dash };

struct HtmlSettings
{
	float y = 0.0f;               // 玉の地面からの高さ
	float vy = 0.0f;              // 上向きの速さ
	int   jumps = 0;              // 跳ねた回数 (割り当てた入力が効いたかを見るための印)
	int   dashes = 0;             // ダッシュした回数
	bool  settingsOpen = false;   // 設定画面を出しているか。出し入れはゲームが決め、画面の中身は host が送る
	std::uint8_t _pad[3] = {};    // 暗黙の詰め物を残さない (状態をバイト単位で比べるため)

	void update(Input in, Hud hud, float dt)
	{
		// 「設定」のボタンか Start で開き、閉じるボタンか戻る (Esc / B) で閉じる。閉じるボタンの action は settings.close
		if (in.action("menu.settings") || in.padPressed(Pad::Start)) { settingsOpen = true; }
		if (in.action("settings.close") || in.cancelPressed()) { settingsOpen = false; }

		// 画面を開いている間は玉を動かさない (キー割り当てをキャプチャしている間は、host が入力を止める)
		if (!settingsOpen)
		{
			if (in.actionPressed(Act::Jump) && y == 0.0f) { vy = 520.0f; ++jumps; }
			if (in.actionPressed(Act::Dash)) { ++dashes; }
		}
		vy -= 1400.0f * dt;
		y += vy * dt;
		if (y < 0.0f) { y = 0.0f; vy = 0.0f; }

		hud.set("view.settings_open", settingsOpen);
		hud.set("view.jumps", jumps);
		hud.set("view.dashes", dashes);
	}

	// 玉と、跳ねた回数 (青) とダッシュした回数 (橙) の印。どれも C++ が描く
	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.drawGradientRect(Rect{0.0f, 0.0f, 1280.0f, 720.0f}, hex(0xF7F9FC), hex(0xE4EAF2));
		s.drawRect(Rect{0.0f, 540.0f, 1280.0f, 3.0f}, theme::kFrame);
		s.fillCircle(640.0f, 520.0f - y, 40.0f, theme::kBlue);
		for (int i = 0; i < jumps && i < 12; ++i) { s.fillCircle(560.0f + 20.0f * static_cast<float>(i), 600.0f, 7.0f, theme::kBlue); }
		for (int i = 0; i < dashes && i < 12; ++i) { s.fillCircle(560.0f + 20.0f * static_cast<float>(i), 630.0f, 7.0f, theme::kOrange); }
		chapterTitle(s, "設定画面");
		chapterControls(s, "Space: とぶ　Shift: ダッシュ　Start: せってい　設定の「操作」でキーを変えられる");
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};

// 実行:  mitiru_host.exe html_settings/html_settings.dll
MITIRU_REFLECT_AUTO(HtmlSettings);

MITIRU_ASSERT_NO_PADDING(HtmlSettings);
MITIRU_ACTIONS(R"json({ "actions": [
  { "id": "jump", "label": "ジャンプ", "slot": "key:Space", "reads": ["key:Space", "pad:A"], "defaults": ["key:Space", "pad:A"] },
  { "id": "dash", "label": "ダッシュ", "slot": "key:Shift", "reads": ["key:Shift", "pad:RB"], "defaults": ["key:Shift", "pad:RB"] }
] })json");
MITIRU_GAME(HtmlSettings);
