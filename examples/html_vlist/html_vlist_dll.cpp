// html_vlist。10 万行の一覧を、見えている 8 行だけ UI に送る例。
//   一覧の上でホイールを回す・行をクリックする・十字キー (PageUp / PageDown / Home / End も) で選ぶと、C++ が
//   「先頭の行の番号」と「選んでいる行」を動かして、その範囲の行だけを hud.set で送り直す。UI の行は常に 8 個。
//   右の大きな字は、選んでいる行の中身 (C++ が行の番号から作る)。
// この章で使う仕組み: ui::VirtualList (mitiru/ui/VirtualList.hpp) / hud.set / RML の data-for / dispatch
#include <cstdio>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/ui/VirtualList.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)
using namespace mitiru;

namespace
{

constexpr int kRowCount = 100000;
constexpr int kVisibleRows = 8;
constexpr const char* kNames[] = { "ポーション", "どくけし", "まほうの みず", "やくそう", "ふるい かぎ", "ひかりの かけら" };
constexpr int kNameCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

// 行の中身は番号から作る。10 万個ぶんの値をどこにも持たない
const char* itemName(int i) { return kNames[(i * 7 + i / 6) % kNameCount]; }
int itemQty(int i) { return (i * 37) % 99 + 1; }

} // namespace

struct HtmlVlist
{
	ui::VirtualList list;      // 先頭の番号と選んでいる行。int だけなので、巻き戻すと一覧の位置も戻る
	int picked = -1;           // 最後にクリックした行
	int lastRowCalls = 0;      // 直前のフレームで行を作った回数 (見えている行数と同じになる)
	int _pad = 0;              // 暗黙のパディングを残さない (状態をバイト単位で比べるため)

	void init()
	{
		list = ui::VirtualList{};
		list.rows = kVisibleRows;
		list.setCount(kRowCount);
	}

	void update(Input in, Hud hud, float)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		const int clicked = list.handle(in, "inv");
		if (clicked >= 0) { picked = clicked; }

		int calls = 0;
		list.push(hud, "inv", [&](int i, ui::RowWriter& row) {
			row.set("name", itemName(i)).set("qty", itemQty(i));
			++calls;
		});
		lastRowCalls = calls;

		hud.set("view.inv_total", kRowCount);
		hud.set("view.inv_first", list.first);
		hud.set("view.inv_cursor", list.cursor);
		hud.set("view.inv_shown", calls);
		hud.set("view.inv_picked", picked);
	}

	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.drawGradientRect(Rect{0.0f, 0.0f, 1280.0f, 720.0f}, hex(0xF7F9FC), hex(0xE4EAF2));
		char text[96];
		std::snprintf(text, sizeof(text), "#%d  %s  x%d", list.cursor, itemName(list.cursor), itemQty(list.cursor));
		s.drawTextInRect(Rect{600.0f, 280.0f, 600.0f, 80.0f}, text, theme::kInk, 44.0f,
		                 Surface::TextAlignH::Left, Surface::TextAlignV::Middle);
		chapterTitle(s, "仮想リスト");
		chapterControls(s, "ホイール / 十字キー / クリック: 一覧を動かす　PageUp・PageDown・Home・End　Esc: おわる");
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};

// 実行:  mitiru_host.exe html_vlist/html_vlist.dll
MITIRU_REFLECT_AUTO(HtmlVlist);

MITIRU_ASSERT_NO_PADDING(HtmlVlist);
MITIRU_GAME(HtmlVlist);
