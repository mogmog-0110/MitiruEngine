// welcome。MitiruEngine の第一章。1 画面で「画像・文字・図形・UI 操作・動き」をまとめて見せる歓迎デモ。
// 実行すると: 左に額装した赤富士 (画像) とブランドロゴ、右に触って試せる小さな機能見本 (図形の並び・
//             ドラッグできるスライダー・押せるボタン) を清潔に並べる。手前の赤べこは矢印キーで歩き、
//             マウスには蛍がついてきて、ボタン/スライダー以外をクリックすると波紋が広がる。
// 使う機能: Texture::fromFile / drawSprite (画像) / drawGradientRect (背景) / drawRoundedRect・
//           fillCircle・drawTriangle (図形) / drawTextInRect (文字) / in.move() (矢印) /
//           in.mouseX/Y・mouseDown (マウスで UI 操作)

#include <mitiru.hpp>
#include <mitiru/render/Texture.hpp>   // 画像 (Texture) を読み込んで drawSprite で描くため

#include <algorithm>   // std::min — 蛍がマウスへ寄る割合の頭打ち
#include <cmath>       // std::sin / std::fmod — ゆれ・舞い散りに使う
#include <cstdio>      // std::snprintf — スライダーの値・ボタンの回数を文字にするため

#include "../common/chapter_hud.hpp"   // 共通パレット theme::k... (白系テーマの色) を使う

using namespace mitiru;

constexpr float kScreenW = 1280.0f;   // 画面は常に 1280x720
constexpr float kScreenH = 720.0f;

// 画像 (Texture) は内部に可変長のピクセルを持つので、ゲームの状態 struct には入れられない
// (状態は「まるごとコピーできる単純な値」だけに保つ決まりのため)。そこで画像はこのファイル直下の
// 変数へ開始時に一度だけ読み込む。読み込みに失敗しても value_or で「空の画像」になるだけで落ちない。
static const render::Texture kFuji =
	render::Texture::fromFile("welcome/assets/images/fuji.png").value_or(render::Texture{});

// ブランドロゴ (i の点が満ちゆく月のワードマーク)。左ゾーンの下に据えるタイトル代わり。
static const render::Texture kLogo =
	render::Texture::fromFile("welcome/assets/images/logo.png").value_or(render::Texture{});

// 手前で歩かせる赤べこ (会津の張り子牛)。4 コマの歩行アニメ。
static const render::Texture kBeko[4] = {
	render::Texture::fromFile("welcome/assets/sprites/akabeko_0.png").value_or(render::Texture{}),
	render::Texture::fromFile("welcome/assets/sprites/akabeko_1.png").value_or(render::Texture{}),
	render::Texture::fromFile("welcome/assets/sprites/akabeko_2.png").value_or(render::Texture{}),
	render::Texture::fromFile("welcome/assets/sprites/akabeko_3.png").value_or(render::Texture{}),
};

// ── 見た目の寸法 ───────────────────────────────────────────────────────────
constexpr float kArtScale  = 0.46f;   // 赤富士の表示倍率 (高解像度を 1 未満で縮小 = くっきり)
constexpr float kMatte     = 24.0f;   // 絵のまわりの余白 (生成りのマット)
constexpr float kFrameBand = 11.0f;   // その外側の額縁 (焦茶) の太さ
constexpr float kArtX      = 80.0f;   // 額の中の絵の左上 X (額縁はこれより 35px 外へ広がる)
constexpr float kArtY      = 70.0f;   // 額の中の絵の左上 Y
constexpr float kLogoW     = 420.0f;  // ロゴの表示幅 (高さは元画像の比から決める)

constexpr float kBekoScale = 0.42f;   // 赤べこの表示倍率
constexpr int   kPetals    = 10;      // 舞う桜の花びらの数 (控えめに)

// ── 右ゾーン「触って試す」カード ──────────────────────────────────────────
// 図形の並び → スライダー → チェックボックス → [ボタン＋ゲージの組] を、白いカードに
// 載せる。各組は同じ左右のガイド (kColL〜kColR) にそろえ、組の間は均等な余白で置く。
// ボタンとゲージだけは 1 組に見えるよう間を詰める。カード高は左の額装赤富士の列と釣り合う。
constexpr float kCardX = 672.0f, kCardY = 48.0f, kCardW = 536.0f, kCardH = 488.0f;
constexpr float kCardR   = 18.0f;                      // カードの角丸
constexpr float kCardPad = 40.0f;                      // カード内の左右の余白
constexpr float kColL = kCardX + kCardPad;             // 全組共通の左ガイド (= 712)
constexpr float kColR = kCardX + kCardW - kCardPad;    // 共通の右ガイド (= 1168)
constexpr float kColW = kColR - kColL;                 // 段の横幅 (= 456)

// 1 段目: 基本図形 4 つ。列幅を 4 等分した各セルの中心に、下端をそろえて置く。
constexpr float kShapeBottom = 160.0f;                 // 4 図形の共通の下端
constexpr float kShapeCx[4]  = {                       // 各セルの中心 X (等間隔)
	kColL + kColW * 0.125f, kColL + kColW * 0.375f,
	kColL + kColW * 0.625f, kColL + kColW * 0.875f};

// 2 段目: スライダー。1 行目にラベル (左) と値 (右)、その下に列いっぱいの溝を敷く。
constexpr float kSliderRowY = 206.0f;                  // ラベル/値の行の上端
constexpr float kTrackY     = 255.0f;                  // 溝の中心 Y
constexpr float kTrackLeft  = kColL;                   // 溝は列いっぱい
constexpr float kTrackLen   = kColW;

// 3 段目: チェックボックス「桜を降らせる」。左の四角 + 右にラベル。桜の描画を on/off する。
constexpr float kCheckX    = kColL;                    // チェック四角の左上 X (= kColL)
constexpr float kCheckY    = 312.0f;                   // チェック四角の左上 Y
constexpr float kCheckSize = 28.0f;                    // 四角の一辺

// 4 段目: ボタン＋ゲージの組。見出し → ボタン → ゲージ を近づけて 1 組に見せる。
constexpr float kSetLabelY = 386.0f;                   // 組の見出しの行の上端
constexpr float kBtnX = kColL, kBtnW = kColW;
constexpr float kBtnY = 412.0f, kBtnH = 54.0f;         // ボタン (見出しのすぐ下)
constexpr float kGaugeY = 483.0f;                      // ゲージ(溝)の中心 Y (ボタンのすぐ下)

// ── 落ち着いた和の配色 (theme に無い色はここで名前をつける)。──
constexpr Color kBgTop      = hex(0xFCF7EF);         // 背景グラデの上 (ほんのり暖かい生成り)
constexpr Color kBgBottom   = hex(0xF2F5FA);         // 背景グラデの下 (涼しい紙白)
constexpr Color kFrameWood  = hex(0x2E2A27);         // 額縁 (焦茶)
constexpr Color kMatteCream = hex(0xFBF8F1);         // マットの白 (生成り)
constexpr Color kBevel      = hex(0xD8CFBE);         // 絵ぎわの細い見切り線
constexpr Color kPanelFill  = hex(0xFFFFFF);         // 右パネルの下地 (白)
constexpr Color kVermilion  = hex(0xC8352B);         // 朱 (波紋の色)
constexpr Color kBluePress  = hex(0x0060D8);         // ボタンを押した瞬間の濃い青
constexpr Color kSakura     = hex(0xF3C4D4);         // 桜の花びら (淡い桃)
constexpr Color kFirefly    = hex(0xF6CC5A);         // マウスを追う蛍 (やわらかい金)
constexpr Color kGaugeDrain = hex(0xF6C98A);         // ゲージが満タンから減っている最中の淡い橙 (リセット中)
constexpr Color kDropShadow = rgba(20, 20, 24, 40);  // うすい落ち影

constexpr float kRippleLife = 0.9f;                  // クリックの波紋が消えるまでの秒数

struct Welcome00
{
	// 状態はすべて単純な値 (まるごとコピーできる)。ポインタや可変長の配列は持たない。
	float t        = 0.0f;
	float bekoX    = 640.0f, bekoY = 612.0f;   // 赤べこの中心
	bool  faceLeft = false;                    // 進む向き (左を向いているか)
	Timer walk;                                // 歩行アニメ用タイマー
	int   step     = 0;                        // 今の歩行コマ (0〜3)

	float fx = kScreenW * 0.5f, fy = kScreenH * 0.5f;   // マウスを追う蛍の位置 (少し遅れて寄る)
	struct Ripple { float x = 0.0f, y = 0.0f, age = 9.0f; };   // age が kRippleLife 以上 = 消えている
	Ripple ripples[5] = {};                    // クリックで出す波紋 (5 枠を使い回す)
	bool   prevClick  = false;                 // 前フレームの左ボタン (押した瞬間だけ反応するため)

	float sliderT  = 0.36f;   // スライダーの位置 (0=左端, 1=右端)。値 speed=120 あたりから始める
	int   count    = 0;       // ボタンを押した回数 ("count: N" 表示用)
	bool  btnDown  = false;   // 今マウスがボタンを押しているか (見た目を少しへこませる用)
	bool  petalsOn = true;    // チェックボックスの状態 (on = 桜を降らせる)
	float gauge    = 0.0f;    // ゲージの溜まり具合 (0=空, 1=満タン)
	bool  draining = false;   // 満タン後、0 へ減っている最中か (この間は溜められない)
	float holdT    = 0.0f;    // 満タンを見せる短い静止の経過秒

	static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

	// マウスがボタンの矩形の中にいるか。
	static bool inButton(float x, float y)
	{
		return x >= kBtnX && x <= kBtnX + kBtnW && y >= kBtnY && y <= kBtnY + kBtnH;
	}

	// マウスがスライダーの「掴める範囲」にいるか (溝の少し外まで含めてハンドルを掴みやすくする)。
	static bool inSliderGrab(float x, float y)
	{
		return x >= kTrackLeft - 16.0f && x <= kTrackLeft + kTrackLen + 16.0f
		    && y >= kTrackY - 18.0f     && y <= kTrackY + 18.0f;
	}

	// マウスがチェックボックスの上にいるか (四角とラベルの行ぜんたいを押せる範囲にする)。
	static bool inCheckbox(float x, float y)
	{
		return x >= kCheckX - 4.0f && x <= kCheckX + kCheckSize + 180.0f
		    && y >= kCheckY - 6.0f && y <= kCheckY + kCheckSize + 6.0f;
	}

	void update(Input in, float dt)
	{
		t += dt;

		// ── 矢印キー・WASD・パッドで赤べこを歩かせる ──
		const Stick m = in.move();     // 全デバイスの入力を 1 つのベクトルにまとめてくれる
		const float bekoSpeed = 20.0f + sliderT * 280.0f;   // 歩く速さは右のスライダーで決まる (連動)
		bekoX += m.x * bekoSpeed * dt;
		bekoY += m.y * bekoSpeed * dt;
		if      (m.x < -0.1f) { faceLeft = true;  }   // 左へ動いたら左向き
		else if (m.x >  0.1f) { faceLeft = false; }   // 右へ動いたら右向き
		bekoX = clampf(bekoX, 90.0f, kScreenW - 90.0f);
		bekoY = clampf(bekoY, 588.0f, 636.0f);        // 手前の帯の中だけ歩く (パネルや額と重ならない)

		const bool moving = (m.x * m.x + m.y * m.y) > 0.01f;
		if (moving) { if (walk.every(0.13f, dt)) { step = (step + 1) % 4; } }
		else        { step = 0; }

		// ── マウスの位置と左ボタン ──
		const float mx = in.mouseX(), my = in.mouseY();
		const bool  down      = in.mouseDown(0);
		const bool  clickEdge = down && !prevClick;   // 押した瞬間だけ true (押しっぱなしでは反応しない)
		const bool  onSlider   = inSliderGrab(mx, my);
		const bool  onButton   = inButton(mx, my);
		const bool  onCheckbox = inCheckbox(mx, my);

		// マウスを追う蛍。少し遅れてカーソルへ寄るので「ついてくる」ように見える。
		const float k = std::min(1.0f, dt * 6.5f);
		fx += (mx - fx) * k;
		fy += (my - fy) * k;

		// スライダー: ハンドルを掴んでいる間、マウスの X に合わせて位置 (0〜1) を更新する。
		if (down && onSlider) { sliderT = clampf((mx - kTrackLeft) / kTrackLen, 0.0f, 1.0f); }

		// ボタン: 押した瞬間に回数を数え、ゲージを 0.1 溜める。押している間は見た目をへこませる。
		btnDown = down && onButton;
		if (clickEdge && onButton)
		{
			count++;                          // "count: N" 表示はいつも増える
			if (!draining)                    // 減っている最中は溜まらない
			{
				gauge += 0.1f;
				if (gauge >= 1.0f) { gauge = 1.0f; draining = true; holdT = 0.0f; }   // 満タン → 静止して減り始める
			}
		}

		// ゲージ: 満タンになったら少し見せてから、毎フレーム滑らかに 0 へ減らす。
		if (draining)
		{
			holdT += dt;                      // まず 0.4 秒 満タンのまま見せる
			if (holdT >= 0.4f)
			{
				gauge -= dt * 0.5f;           // そのあとゆっくり空へ (満タンから約 2 秒)
				if (gauge <= 0.0f) { gauge = 0.0f; draining = false; }
			}
		}

		// チェックボックス: 押した瞬間に on/off を切り替える (桜を降らせるかどうか)。
		if (clickEdge && onCheckbox) { petalsOn = !petalsOn; }

		// 波紋: クリックの瞬間にその場所へ 1 つ出す。ただしボタン/スライダー/チェックの上では
		//       出さない (UI 操作と波紋を重ねない。触った所が二重に反応すると分かりにくいため)。
		if (clickEdge && !onButton && !onSlider && !onCheckbox)
		{
			int slot = 0;                                    // 一番古い (age 最大の) 枠を使い回す
			for (int i = 1; i < 5; ++i) { if (ripples[i].age > ripples[slot].age) { slot = i; } }
			ripples[slot] = Ripple{mx, my, 0.0f};
		}
		prevClick = down;
		for (Ripple& r : ripples) { if (r.age < kRippleLife) { r.age += dt; } }
	}

	// ── 描画部品 ───────────────────────────────────────────────────────────

	// 赤べこ 1 匹を中心 (cx,cy)・大きさ scale・向き flip で描く。
	static void beko(Screen& s, int frame, float cx, float cy, float scale, bool flip)
	{
		const render::Texture& tex = kBeko[frame];
		const float w = static_cast<float>(tex.width()) * scale;
		const float h = static_cast<float>(tex.height()) * scale;
		const Rect  dst{cx - w * 0.5f, cy - h * 0.5f, w, h};
		const Rect  src{0.0f, 0.0f, static_cast<float>(tex.width()), static_cast<float>(tex.height())};
		s.drawSprite(tex, dst, src, color::White, flip);
	}

	// ゆっくり舞い落ちる控えめな桜。位置は経過秒 t と番号 i だけから決まる (状態を持たない)。
	// 落ちながら左右にゆれ、少しずつ回る。画面下に消えたら上へ戻って繰り返す。
	void petals(Screen& s) const
	{
		for (int i = 0; i < kPetals; ++i)
		{
			const float fi   = static_cast<float>(i);
			const float fall = 26.0f + static_cast<float>((i * 13) % 20);   // 落ちる速さ (番号でばらす)
			const float sway = 12.0f + static_cast<float>((i * 7) % 14);    // 左右ゆれの幅
			const float baseX = 50.0f + (kScreenW - 100.0f) * fi / kPetals;
			const float x = baseX + std::sin(t * (0.5f + fi * 0.11f) + fi) * sway;
			const float y = std::fmod(fi * 90.0f + t * fall, kScreenH + 60.0f) - 30.0f;
			const float rx = 6.0f + static_cast<float>(i % 3) * 1.5f;       // 花びらの大きさ
			const float alpha = 0.16f + static_cast<float>(i % 4) * 0.04f;  // 控えめに (手前ほど少し濃く)
			s.pushRotation(deg(fi * 41.0f + t * 40.0f), x, y);              // ひらひら回りながら
			s.drawEllipse(Vec2{x, y}, rx, rx * 0.58f, kSakura.withAlpha(alpha));
			s.popTransform();
		}
	}

	// 額に入れた絵を、絵の左上を (x,y) として描く。落ち影→額縁→マット→絵→見切り線の順に重ねる。
	void framedArt(Screen& s, float x, float y, float artW, float artH) const
	{
		const float ox = x - kMatte - kFrameBand, oy = y - kMatte - kFrameBand;
		const float ow = artW + 2.0f * (kMatte + kFrameBand), oh = artH + 2.0f * (kMatte + kFrameBand);
		s.drawRoundedRect(Rect{ox + 6.0f, oy + 10.0f, ow, oh}, kDropShadow, 8.0f);   // 落ち影
		s.drawRoundedRect(Rect{ox, oy, ow, oh}, kFrameWood, 6.0f);                    // 額縁 (焦茶)
		s.drawRoundedRect(Rect{ox + kFrameBand, oy + kFrameBand,                      // マット (生成り)
		                       ow - 2.0f * kFrameBand, oh - 2.0f * kFrameBand}, kMatteCream, 3.0f);
		const Rect dst{x, y, artW, artH};
		const Rect src{0.0f, 0.0f, static_cast<float>(kFuji.width()), static_cast<float>(kFuji.height())};
		s.drawSprite(kFuji, dst, src);                    // 浮世絵そのもの
		s.drawRectFrame(dst, kBevel, 1.0f);               // 絵ぎわの見切り線
	}

	// 上段: 基本図形 4 つを、列を 4 等分した各セルの中心に、下端 kShapeBottom をそろえて並べる。
	// 角丸四角 (青)・円 (緑)・縦長カプセル (橙)・三角形 (桃)。大きさをそろえて上品に見せる。
	void shapesRow(Screen& s) const
	{
		const float b = kShapeBottom;

		// 角丸四角 (青)。60x60。
		s.drawRoundedRect(Rect{kShapeCx[0] - 30.0f, b - 60.0f, 60.0f, 60.0f}, theme::kBlue, 15.0f);

		// 円 (緑)。直径 62。下端がそろうよう中心を b-31 に置く。
		s.fillCircle(kShapeCx[1], b - 31.0f, 31.0f, theme::kGreen);

		// 縦長カプセル (橙)。幅 42・高さ 66。角丸を幅の半分にして丸い縦棒に。
		s.drawRoundedRect(Rect{kShapeCx[2] - 21.0f, b - 66.0f, 42.0f, 66.0f}, theme::kOrange, 21.0f);

		// 三角形 (桃)。頂点を上に、底辺を下端にそろえる。3 点を drawTriangle に渡す。
		const Vec2 apex {kShapeCx[3],         b - 58.0f};
		const Vec2 left {kShapeCx[3] - 33.0f, b};
		const Vec2 right{kShapeCx[3] + 33.0f, b};
		s.drawTriangle(apex, left, right, theme::kPink);
	}

	// 右ゾーンの下地カード。影は横にずらさず真下だけに置き、カードと同じ幅・同じ角丸にする。
	// 斜めにずらすと角丸の隅に影が三日月形に覗く。真下だけなら影の角がカードの角の
	// 真下にぴったり重なり、下辺に薄い帯としてだけ見え、
	// 横や上の角には決してはみ出さない。2 枚重ねて下ほど濃く滲ませる。
	void panelCard(Screen& s) const
	{
		const Rect body{kCardX, kCardY, kCardW, kCardH};
		// 落ち影は横にずらさず「真下」だけ・カードと同じ幅と角丸で 2 枚重ねる。影の角が
		// カードの角の真下にぴったり重なるので、下辺に薄い帯としてだけ見え、横や上の角には
		// はみ出さない。細い帯を内側に置く前案は角丸の端が浮いて見えたので、この手つきに直す。
		s.drawRoundedRect(Rect{kCardX, kCardY + 7.0f, kCardW, kCardH}, rgba(28, 32, 44, 12), kCardR);
		s.drawRoundedRect(Rect{kCardX, kCardY + 3.0f, kCardW, kCardH}, rgba(28, 32, 44, 18), kCardR);
		// カード本体 (白)。枠線は付けない。下辺の帯影だけで背景から浮かせる。
		s.drawRoundedRect(body, kPanelFill, kCardR);
	}

	// カード内の説明ラベルは全部この書式でそろえる (読みやすい濃色 kInk・18px・左そろえ)。
	// スライダーの数値「121」などは機能上の値なので、これとは別扱い。
	static void cardLabel(Screen& s, const Rect& r, const char* text)
	{
		s.drawTextInRect(r, text, theme::kInk, 18.0f,
		                 Screen::TextAlignH::Left, Screen::TextAlignV::Middle);
	}

	// 2 段目: スライダー。1 行目の左にラベル・右に今の値、その下に列いっぱいの溝とハンドル。
	void slider(Screen& s) const
	{
		// ラベル (左)。このスライダーが赤べこの歩く速さを決めることを示す。
		cardLabel(s, Rect{kColL, kSliderRowY, kColW * 0.6f, 24.0f}, "赤べこの速さ");

		// 値 (右)。溝の位置を 20〜300 の数に読み替えて、行の右端にそろえる。
		const int speed = static_cast<int>(20.0f + sliderT * 280.0f + 0.5f);
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", speed);
		s.drawTextInRect(Rect{kColL + kColW * 0.55f, kSliderRowY, kColW * 0.45f, 24.0f}, buf,
		                 theme::kBlue, 20.0f, Screen::TextAlignH::Right, Screen::TextAlignV::Middle);

		// 溝 (灰) と、左からハンドルまでの塗り (青)。列いっぱいに敷く。
		s.drawRoundedRect(Rect{kTrackLeft, kTrackY - 3.0f, kTrackLen, 6.0f}, theme::kFrame, 3.0f);
		s.drawRoundedRect(Rect{kTrackLeft, kTrackY - 3.0f, sliderT * kTrackLen, 6.0f}, theme::kBlue, 3.0f);

		// ハンドル (白い丸に青いふち)。sliderT の位置に置く。
		const float hx = kTrackLeft + sliderT * kTrackLen;
		s.fillCircle(hx, kTrackY + 2.0f, 11.0f, kDropShadow);          // 落ち影
		s.fillCircle(hx, kTrackY,        11.0f, color::White);         // 白い玉
		s.drawCircleFrame(Vec2{hx, kTrackY}, 11.0f, theme::kBlue, 3.0f);
	}

	// 下段: 押すたびに回数が増える角丸ボタン。列いっぱいの幅。押す間は 2px 沈めて反応を伝える。
	void button(Screen& s) const
	{
		const float dy = btnDown ? 2.0f : 0.0f;                 // 押している間だけ少し下げる
		const Rect  face{kBtnX, kBtnY + dy, kBtnW, kBtnH};
		// 落ち影はカードと同じ手つき: 横にずらさず真下だけ・同じ幅と角丸で。角に影が覗かない。
		s.drawRoundedRect(Rect{kBtnX, kBtnY + 4.0f, kBtnW, kBtnH}, kDropShadow, 12.0f);
		s.drawRoundedRect(face, btnDown ? kBluePress : theme::kBlue, 12.0f);             // ボタン面

		char buf[24];
		std::snprintf(buf, sizeof(buf), "count: %d", count);
		s.drawTextInRect(face, buf, color::White, 22.0f,
		                 Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
	}

	// 4 段目: チェックボックス「桜を降らせる」。四角をクリックで on/off。
	// on = 青く塗って白いレ点、off = 灰の枠だけの空箱。状態は petalsOn が持ち、draw() が桜を切り替える。
	// 3 段目。
	void checkbox(Screen& s) const
	{
		const Rect box{kCheckX, kCheckY, kCheckSize, kCheckSize};
		if (petalsOn)
		{
			s.drawRoundedRect(box, theme::kBlue, 7.0f);            // 塗り (青)
			// 白いレ点。左中→底の角→右上 の 2 本の線で描く。
			s.drawLine(Vec2{kCheckX +  6.0f, kCheckY + 15.0f}, Vec2{kCheckX + 12.0f, kCheckY + 21.0f},
			           color::White, 3.0f);
			s.drawLine(Vec2{kCheckX + 12.0f, kCheckY + 21.0f}, Vec2{kCheckX + 22.0f, kCheckY +  8.0f},
			           color::White, 3.0f);
		}
		else
		{
			s.drawRoundedRect(box, color::White, 7.0f);           // 白い空箱
			s.drawRoundedRectFrame(box, theme::kFrame, 7.0f, 2.0f);
		}
		// 右にラベル。四角の縦中心に文字をそろえる (他のラベルと同じ書式)。
		cardLabel(s, Rect{kCheckX + kCheckSize + 16.0f, kCheckY, kColW - kCheckSize - 16.0f, kCheckSize},
		          "桜を降らせる");
	}

	// 4 段目 (ボタンと 1 組): ゲージ「おすと たまる」。ボタンを押すたびに 0.1 溜まり、満タンで 0 へ戻る。
	// 見出しはボタンの真上に置いて、ボタン→ゲージが 1 組だと分かるようにする。溝 + 塗りで表し、
	// 満タンから減っている最中は塗りを淡い橙にして「今リセット中」を伝える。
	void gaugeBar(Screen& s) const
	{
		// 組の見出し (ボタンの真上、他のラベルと同じ書式)。
		cardLabel(s, Rect{kColL, kSetLabelY, kColW, 24.0f}, "おすと たまる (満タンで戻る)");

		// 溝 (灰) と、溜まった分の塗り。列いっぱいの横メーター。gauge (0〜1) を幅に読み替える。
		const float h = 14.0f;
		s.drawRoundedRect(Rect{kColL, kGaugeY - h * 0.5f, kColW, h}, theme::kFrame, 7.0f);
		const float fill = gauge * kColW;
		if (fill > 0.0f)
		{
			// 溜まる間と満タンの静止中は明るい橙 (満タンを見せる)、実際に減り始めたら淡い橙に。
			const Color fillColor = (draining && holdT >= 0.4f) ? kGaugeDrain : theme::kOrange;
			s.drawRoundedRect(Rect{kColL, kGaugeY - h * 0.5f, fill, h}, fillColor, 7.0f);
		}
	}

	void draw(Screen& s) const
	{
		// 背景は上→下のゆるいグラデ (暖かい生成り → 涼しい紙白)。奥行きを出す静かな下地。
		s.drawGradientRect(Rect{0.0f, 0.0f, kScreenW, kScreenH}, kBgTop, kBgBottom);
		if (petalsOn) { petals(s); }   // チェックが on の時だけ桜が舞う (主役の絵や部品より奥に敷く)

		// ── 左ゾーン: 額装した赤富士 → キャプション → ロゴ を縦に積む ──
		const float artW = static_cast<float>(kFuji.width())  * kArtScale;
		const float artH = static_cast<float>(kFuji.height()) * kArtScale;
		framedArt(s, kArtX, kArtY, artW, artH);

		// 絵の下に小さなキャプション (美術館の解説札のように作者と外題を添える)。
		const float capY = kArtY + artH + kMatte + kFrameBand + 8.0f;
		s.drawTextInRect(Rect{kArtX - kMatte, capY, artW + 2.0f * kMatte, 22.0f},
		                 "葛飾北斎「凱風快晴」富嶽三十六景", theme::kSubtle, 15.0f,
		                 Screen::TextAlignH::Center, Screen::TextAlignV::Middle);

		// キャプションの下にブランドロゴ (透過画像を縮小)。絵の中心にそろえて置く。
		const float logoH = kLogoW * static_cast<float>(kLogo.height()) / static_cast<float>(kLogo.width());
		const float logoX = kArtX + artW * 0.5f - kLogoW * 0.5f;
		const float logoY = capY + 22.0f + 22.0f;
		s.drawSprite(kLogo, Rect{logoX, logoY, kLogoW, logoH},
		             Rect{0.0f, 0.0f, static_cast<float>(kLogo.width()), static_cast<float>(kLogo.height())});

		// ── 右ゾーン: 触って試す機能見本。白いカードの上に、図形の並び → スライダー →
		//    チェックボックス → [ボタン＋ゲージの組] を同じ左右のガイドでそろえて載せる。
		//    button(s) と gauge(s) が上下に隣り合って 1 組を作る。──
		panelCard(s);
		shapesRow(s);
		slider(s);
		checkbox(s);
		button(s);
		gaugeBar(s);

		// ── 手前: 矢印キーで歩く赤べこ。進む向きに反転し、短い脚が交互に動く。──
		beko(s, step, bekoX, bekoY, kBekoScale, faceLeft);

		// ── 触れるデモ: クリックで広がる波紋。蛍と同じ金の輪が、押した場所からそっと広がって薄れる。──
		for (const Ripple& r : ripples)
		{
			if (r.age >= kRippleLife) { continue; }
			const float fade = 1.0f - r.age / kRippleLife;
			s.drawCircleFrame(Vec2{r.x, r.y}, 8.0f + r.age * 110.0f, kFirefly.withAlpha(fade * 0.5f), 2.0f);
		}
		// ── 触れるデモ: マウスを追う蛍。やわらかい金の光が、少し遅れてカーソルについてくる。──
		s.fillCircle(fx, fy, 20.0f, kFirefly.withAlpha(0.10f));
		s.fillCircle(fx, fy, 12.0f, kFirefly.withAlpha(0.24f));
		s.fillCircle(fx, fy,  6.0f, kFirefly.withAlpha(0.90f));

		// 操作の案内 (手前の帯にそっと)。一目で分かる平易な言葉で。
		const Rect band{0.0f, kScreenH - 34.0f, kScreenW, 34.0f};
		s.drawRect(band, rgba(226, 231, 240, 225));
		s.drawTextInRect(band, "矢印キーで赤べこが歩く　　右のスライダーとボタンは触って動かせる",
		                 hex(0x3A4048), 18.0f, Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
	}
};

// この 1 行がゲームの入口。実行役 mitiru_host.exe がこの struct を作り、毎フレーム draw() を呼ぶ。
// 実行:  mitiru_host.exe welcome/welcome.dll
MITIRU_GAME(Welcome00);
