// vtext の実装。縦書きと、縦書き・横書きのルビを 1 画面で見せる

#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>

#include "../common/chapter_hud.hpp"   // 章ラベルと共通パレット (theme::k... の色)

using namespace mitiru;

struct VText
{
	float t = 0.0f;   // 経過秒 (update が進めるだけで、描画は使わない)

	void update(Input, float dt) { t += dt; }

	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "縦書き");

		// 右の枠: 行は右から左へ進む。ルビは「親文字《るび》」と書く。漢字の直前に付くほか、｜で親文字の始まりを示せる
		const Rect story{660.0f, 84.0f, 580.0f, 500.0f};
		s.drawRectFrame(story, theme::kFrame, 1.0f);
		s.drawTextVertical(story,
		                   "吾輩《わがはい》は猫である。名前はまだ無い。\n"
		                   "どこで生《うま》れたかとんと見当《けんとう》がつかぬ。何でも薄暗いじめじめした所で"
		                   "ニャーニャー泣いていた事だけは記憶している。",
		                   theme::kInk, 34.0f, module::TextWriting::VerticalRl, true, 24.0f, 24.0f);

		// 左の枠: 2 桁の数字と「!?」は 1 字分に横組みで収まり、欧文は横向きになり、小書き仮名と句読点は右上へ寄る
		const Rect sample{40.0f, 84.0f, 580.0f, 500.0f};
		s.drawRectFrame(sample, theme::kFrame, 1.0f);
		s.drawTextVertical(sample,
		                   "令和26年10月28日、Mitiru の新しいバージョンが出た!?\n"
		                   "「きゃっ」と叫んで、ゲームを遊ぶ。\n"
		                   "ラーメンとコーヒーをどうぞ。",
		                   theme::kBlue, 34.0f, module::TextWriting::VerticalRl, true, 24.0f, 24.0f);

		// 下の枠: 横書きのルビは drawTextRuby で頼む。drawTextInRect は《》を書いたとおりに描く
		const Rect band{40.0f, 600.0f, 1200.0f, 100.0f};
		s.drawRectFrame(band, theme::kFrame, 1.0f);
		s.drawTextRuby(band, "｜横書き《よこがき》のルビも、親文字《おやもじ》の上に半分の大きさで付く。", theme::kInk, 30.0f, 24.0f, 12.0f);
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};

MITIRU_REFLECT_AUTO(VText);

MITIRU_ASSERT_NO_PADDING(VText);
MITIRU_GAME(VText);
