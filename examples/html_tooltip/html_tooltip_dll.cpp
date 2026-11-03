// html_tooltip。ボタンに title="説明" と書くだけで、説明が出る例。
//   左のパネルの色見本とボタンにマウスを止めると、要素の下に説明が出る。十字キーやパッドでフォーカスを動かしても、
//   止まったところで同じ説明が出る。右の玉は選んだ色で、「つかう」を押すたびに残りが減る。
//   説明の出し入れは host がやる。ゲームが書くのは RML の title だけで、残りの数のように変わる文は data-attr-title で送る。
// この章で使う仕組み: RML の title 属性 / data-attr-title / m-tooltip (mitiru:components.rcss) / in.action
#include <cstdint>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)
using namespace mitiru;

constexpr std::uint32_t kColors[] = { 0xE8338A, 0x0A84FF, 0x1FA654, 0xFF9500, 0x7C5CFF, 0x1D1D1F };
constexpr int kColorCount = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));

struct HtmlTooltip
{
	int color = 0;    // 選んでいる色
	int stock = 3;    // 「つかう」の残り
	int uses = 0;     // 使った回数 (玉のまわりの点)

	void update(Input in, Hud hud, float)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.action("pick.color"))
		{
			const int id = in.actionPayloadInt("pick.color", "id", -1);
			if (id >= 0 && id < kColorCount) { color = id; }
		}
		if (in.action("use") && stock > 0) { --stock; ++uses; }
		hud.set("view.color", color);
		hud.set("view.stock", stock);
	}

	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.drawGradientRect(Rect{0.0f, 0.0f, 1280.0f, 720.0f}, hex(0xF7F9FC), hex(0xE4EAF2));
		s.fillCircle(850.0f, 360.0f, 120.0f, hex(kColors[color]));
		for (int i = 0; i < uses && i < 12; ++i) { s.fillCircle(790.0f + 12.0f * static_cast<float>(i), 520.0f, 5.0f, theme::kInk); }
		chapterTitle(s, "ツールチップ");
		chapterControls(s, "マウスを止める / 十字キーで動かす: 説明が出る　A: 押す　Esc: おわる");
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};

// 実行:  mitiru_host.exe html_tooltip/html_tooltip.dll
MITIRU_REFLECT_AUTO(HtmlTooltip);

MITIRU_ASSERT_NO_PADDING(HtmlTooltip);
MITIRU_GAME(HtmlTooltip);
