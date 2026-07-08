// motion — 動きに緩急をつける「イージング」の見本。1 つの曲線が位置・大きさ・回転・透明度をどう動かすか
// 実行すると: 4 種のイージング (列) が、位置 / 大きさ / 回転 / 透明度 (行) を同時に動かし続ける
// 関連 API: mitiru::vn::Easing (linear / easeInCubic / easeOutCubic / easeInOutCubic)

#include <algorithm>   // std::min
#include <cmath>       // std::fmod

#include <mitiru.hpp>
#include <mitiru/vn/EasingFunctions.hpp>   // イージング関数 (linear / easeInCubic / easeOutCubic ...)

#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 + 共通の配色

using namespace mitiru;

// イージング関数 1 つと、その表示名。列ごとに 1 種類を受け持つ。
// イージングは 0→1 の進み具合を受け取り、緩急をつけた 0→1 を返す関数。
using EaseFn = float (*)(float) noexcept;
struct Column { const char* name; EaseFn ease; };
constexpr Column kCols[4] = {
	{"Linear",    &vn::Easing::linear},         // 等速 (緩急なし)
	{"EaseInOut", &vn::Easing::easeInOutCubic},  // ゆっくり始まり、速くなり、またゆっくり止まる
	{"EaseIn",    &vn::Easing::easeInCubic},      // ゆっくり始まって、だんだん速く
	{"EaseOut",   &vn::Easing::easeOutCubic},     // 速く始まって、だんだん遅く
};

// 4 つの行 = 動かすプロパティ。中心 y と、左端に出す名前。
constexpr float       kRowY[4]   = {150.0f, 292.0f, 436.0f, 584.0f};
constexpr const char* kRowLbl[4] = {"Position", "Scale", "Rotation", "Opacity"};

// 各列の配置。左の 165px は行ラベル用にあけ、そこから幅 250 の列を 272 間隔で 4 本並べる。
constexpr float kColX = 165.0f, kColW = 250.0f, kColGap = 272.0f;

constexpr float kDur  = 1.6f;   // 0 → 1 まで動く秒数
constexpr float kHold = 0.5f;   // 動き切ったあと、次の周回まで止めて見せる秒数

struct Motion
{
	float t = 0.0f;   // 経過秒。状態はこれ 1 つだけ

	void update(Input in, float dt)
	{
		if (in.pressed(Key::Space)) { t = 0.0f; }   // 最初からやり直す
		t += dt;
	}

	// いまの進み具合 (0 → 1)。動き切ったら、次の周回が始まるまで 1 のまま止める。
	float progress() const
	{
		const float phase = std::fmod(t, kDur + kHold);
		return std::min(phase / kDur, 1.0f);
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "Motion");

		const float p = progress();

		for (int r = 0; r < 4; ++r)   // 行ラベルは左端に 1 回ずつ
		{
			s.text(kRowLbl[r], 14.0f, kRowY[r] - 8.0f, theme::kSubtle, 15);
		}

		for (int c = 0; c < 4; ++c)
		{
			const float left   = kColX + kColGap * static_cast<float>(c);
			const float center = left + kColW * 0.5f;
			const float e      = kCols[c].ease(p);   // この列のイージングを通した進み具合

			s.drawTextInRect(Rect{left, 56.0f, kColW, 26.0f}, kCols[c].name, theme::kInk, 18.0f,
			                 Screen::TextAlignH::Center, Screen::TextAlignV::Middle);

			drawPosition(s, left,   kRowY[0], e, c);
			drawScale(s,    center, kRowY[1], e);
			drawRotation(s, center, kRowY[2], e);
			drawOpacity(s,  center, kRowY[3], e);
		}

		chapterControls(s, "SPACE: はじめから");
	}

	// 位置: 横のレールの上を丸が進む。等間隔の時間で刻んだ目盛りも重ねる —
	// 目盛りが詰まっている所ほど動きが遅く、まばらな所ほど速い (速さの変化が形で見える)。
	void drawPosition(Screen& s, float left, float y, float e, int c) const
	{
		const float x0 = left + 18.0f, len = kColW - 36.0f;
		s.drawLine(Vec2{x0, y}, Vec2{x0 + len, y}, theme::kFrame, 2.0f);
		for (int k = 0; k <= 20; ++k)   // 等間隔の時間ごとに、丸が来る位置へ目盛りを打つ
		{
			const float tx = x0 + len * kCols[c].ease(static_cast<float>(k) / 20.0f);
			s.drawLine(Vec2{tx, y - 6.0f}, Vec2{tx, y + 6.0f}, theme::kSubtle.withAlpha(0.45f), 1.0f);
		}
		s.fillCircle(x0 + len * e, y, 9.0f, theme::kBlue);
	}

	// 大きさ: 枠の中で、四角形が小さく → 大きく育つ。
	void drawScale(Screen& s, float cx, float cy, float e) const
	{
		s.drawRectFrame(Rect{cx - 46.0f, cy - 46.0f, 92.0f, 92.0f}, theme::kFrame, 1.5f);
		const float side = 10.0f + e * 72.0f;
		s.drawRect(cx - side * 0.5f, cy - side * 0.5f, side, side, theme::kInk);
	}

	// 回転: 四角形が 0 → 135 度まわる (斜めに傾いて見えるので回転が分かりやすい)。
	void drawRotation(Screen& s, float cx, float cy, float e) const
	{
		s.pushRotation(deg(e * 135.0f), cx, cy);
		s.drawRect(cx - 31.0f, cy - 31.0f, 62.0f, 62.0f, theme::kInk);
		s.popTransform();
	}

	// 透明度: 四角形が透明 → 不透明へ浮かび上がる (薄い枠で位置が分かるようにしておく)。
	void drawOpacity(Screen& s, float cx, float cy, float e) const
	{
		const Rect box{cx - 31.0f, cy - 31.0f, 62.0f, 62.0f};
		s.drawRectFrame(box, theme::kFrame, 1.0f);
		s.drawRect(box, theme::kInk.withAlpha(e));
	}
};

MITIRU_GAME(Motion);
