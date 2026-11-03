// html_svg は、SVG のアイコンを UI に出す例。UI の文書 (assets/ui/main.rml) が <svg src="icons/..."/> と書くだけで、
// 32dp と 96dp の 2 つの大きさに描き直され、設定の UI の倍率にも追従する。C++ は紙白の背景を描くだけで、UI には何も送らない。
// SVG は opt-in の機能なので、mitiru.toml の features に "svg" を書いた build で見える (docs/UI_SVG.md)。
#include <cstdint>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 + 共通パレット
using namespace mitiru;

struct HtmlSvg
{
	std::int32_t frame = 0;   // 何も反映しない。状態が空だと aggregate の反射が成り立たないので置いている

	void update(Input in, Hud hud, float)
	{
		if (in.cancelPressed()) { hud.quit(); }   // ESC で終わる
		++frame;
	}
	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "SVG のアイコン");
		chapterControls(s, "ESC: おわる");
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};
// 実行:  mitiru_host.exe html_svg/html_svg.dll
MITIRU_REFLECT_AUTO(HtmlSvg);

MITIRU_ASSERT_NO_PADDING(HtmlSvg);
MITIRU_GAME(HtmlSvg);
