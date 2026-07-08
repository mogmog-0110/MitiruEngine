// text — 文字をあつかう主な機能を、余白をとった区画で 1 つずつ見せる
// 実行すると: 上段に「枠のどこに文字を置くか」(整列) の大きな見本、
//             下段に 文字サイズ / 色 / 折り返し / はみ出しの省略 の見本が並ぶ
// 使う機能: drawTextInRect (整列) / drawTextWrapped (折り返し)
//           / drawTextClipped (省略) / text (左上へ 1 行)

#include <algorithm>   // std::min
#include <cmath>       // std::fmod
#include <string>      // std::string (動く文字の substr)

#include <mitiru.hpp>

#include "../common/chapter_hud.hpp"   // 章ラベルと共通パレット (theme::k... の色)

using namespace mitiru;

struct Text03
{
	float t = 0.0f;   // 経過秒 (動く文字デモに使う)

	void update(Input, float dt) { t += dt; }

	// 区画 1 つぶんの小見出し (枠の上に薄グレー) と枠線を描く共通ヘルパー。
	void cell(Screen& s, const Rect& box, const char* caption) const
	{
		s.text(caption, box.x(), box.y() - 24.0f, theme::kSubtle, 15.0f);
		s.drawRectFrame(box, theme::kFrame, 1.0f);
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "Text");

		// ── 上段: 枠の「どこ」に文字を置くか (整列) ──────────────────
		// 大きな枠を 3 つ並べ、整列名をその整列どおりの位置に置く。
		// 文字が枠のどこに付くのかが、名前と位置の両方で一目で分かる。
		constexpr float boxY = 92.0f, boxW = 380.0f, boxH = 200.0f;
		constexpr float boxX[3] = {40.0f, 450.0f, 860.0f};
		constexpr Screen::TextAlignH alignH[3] = {
			Screen::TextAlignH::Left, Screen::TextAlignH::Center, Screen::TextAlignH::Right};
		constexpr Screen::TextAlignV alignV[3] = {
			Screen::TextAlignV::Top, Screen::TextAlignV::Middle, Screen::TextAlignV::Bottom};
		constexpr const char* alignName[3] = {"Left Top", "Center Middle", "Right Bottom"};
		s.text("枠のどこに置くか  drawTextInRect(align)", 40.0f, 66.0f, theme::kSubtle, 15.0f);
		for (int i = 0; i < 3; ++i)
		{
			const Rect box{boxX[i], boxY, boxW, boxH};
			s.drawRectFrame(box, theme::kFrame, 1.0f);
			s.drawTextInRect(box, alignName[i], theme::kBlue, 26.0f, alignH[i], alignV[i], 16.0f, 14.0f);
		}

		// ── 下段: 文字そのものの見せ方 4 つ ─────────────────────────
		constexpr float cy = 362.0f, ch = 236.0f, cw = 280.0f;
		constexpr float fx[4] = {40.0f, 346.0f, 653.0f, 960.0f};

		// (1) 文字サイズ: 同じ語を小さい→大きいへ。fontSize の数値を変えるだけ。
		cell(s, Rect{fx[0], cy, cw, ch}, "文字サイズ  fontSize");
		constexpr float sizes[4] = {16.0f, 24.0f, 34.0f, 46.0f};
		float ty = cy + 14.0f;
		for (float sz : sizes)
		{
			s.drawTextInRect(Rect{fx[0] + 16.0f, ty, cw - 24.0f, sz * 1.4f}, "Mitiru",
			                 theme::kInk, sz, Screen::TextAlignH::Left, Screen::TextAlignV::Top);
			ty += sz * 1.4f + 6.0f;   // 次の行は今の文字の高さぶんだけ下げる
		}

		// (2) 色: 書き方は同じで、色 (第 3 引数) だけを変える。
		cell(s, Rect{fx[1], cy, cw, ch}, "色  Color");
		struct Sw { Color c; const char* name; };
		const Sw sw[5] = {{theme::kBlue, "Blue"}, {theme::kPink, "Pink"}, {theme::kGreen, "Green"},
		                  {theme::kOrange, "Orange"}, {theme::kInk, "Ink"}};
		float coy = cy + 12.0f;
		for (const auto& e : sw)
		{
			s.drawTextInRect(Rect{fx[1] + 16.0f, coy, cw - 24.0f, 34.0f}, e.name, e.c, 26.0f,
			                 Screen::TextAlignH::Left, Screen::TextAlignV::Middle);
			coy += 42.0f;
		}

		// (3) 折り返し: 長い文が枠の幅で単語ごとに折り返る (スペース区切り)。
		const Rect wrapBox{fx[2], cy, cw, ch};
		cell(s, wrapBox, "折り返し  drawTextWrapped");
		s.drawTextWrapped(wrapBox,
		                  "This long English sentence will not fit on one line, "
		                  "so it automatically wraps at each word break.",
		                  theme::kInk, 18.0f, 14.0f, 12.0f);

		// (4) はみ出しの省略: 枠に収まらない行は末尾が自動で "..." になる。
		//     ただの drawText と違い、枠の外へあふれて他と重ならない。
		const Rect clipBox{fx[3], cy, cw, ch};
		cell(s, clipBox, "はみ出しは省略  drawTextClipped");
		s.drawTextClipped(Rect{fx[3] + 14.0f, cy + 20.0f, cw - 28.0f, 30.0f},
		                  "This whole sentence is far too long to fit inside the box",
		                  theme::kOrange, 20.0f);
		s.drawTextClipped(Rect{fx[3] + 14.0f, cy + 76.0f, cw - 28.0f, 30.0f},
		                  "Short line fits", theme::kGreen, 20.0f);

		// ── 動く文字: 1 文字ずつ出す。text.substr(0, n) の n を時間で増やすだけ ──
		s.text("動く文字  1 文字ずつ出す", 40.0f, 610.0f, theme::kSubtle, 15.0f);
		const Rect animBox{40.0f, 634.0f, 1200.0f, 60.0f};
		s.drawRectFrame(animBox, theme::kFrame, 1.0f);
		static const std::string msg = "Hello! I appear one letter at a time.";
		const int   len   = static_cast<int>(msg.size());
		const float cycle = static_cast<float>(len) / 18.0f + 1.4f;   // 打ち終えて少し待ち、最初へ戻る
		const float phase = std::fmod(t, cycle);
		const int   n     = std::min(len, static_cast<int>(phase * 18.0f));   // 1 秒に 18 文字
		std::string shown = msg.substr(0, static_cast<std::size_t>(n));
		if (std::fmod(t, 1.0f) < 0.5f) { shown += "|"; }   // 点滅カーソル
		s.drawTextInRect(Rect{60.0f, animBox.y(), 1160.0f, animBox.height()}, shown.c_str(),
		                 theme::kInk, 28.0f, Screen::TextAlignH::Left, Screen::TextAlignV::Middle);
	}
};

// 実行:  mitiru_host.exe text/text.dll
MITIRU_GAME(Text03);
