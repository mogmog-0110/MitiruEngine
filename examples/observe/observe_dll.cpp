// observe — 画面に見えるのは「結果」だけ、という例。
//   ここでは赤べこ (会津の張り子牛) が、腹が減れば餌へ歩き、疲れれば休む。画面に映るのは
//   その動き — 首を振る / 跳ねる / 表情が変わる / 餌へ寄る — だけ。
//   一方で、なぜ今それをしているか (腹・体力・機嫌・今の行動) という内部の値は画面に出さない。
//   その内部の値は、ゲームを止めずに別の窓 (inspector) から live で読める。これが「観測」。
//   画面を見て動きを楽しみ、値を見て理由を知る、という二面を分けて見せるのがこの章の主旨。
// 使う機能: 状態を登録する MITIRU_REFLECT (ファイル末尾) / 観測窓つきで起動する mitiru_host --inspect

#include <algorithm>   // std::min / std::max
#include <cmath>       // std::sqrt / std::sin / std::fabs
#include <cstring>     // std::strcmp

#include <mitiru.hpp>
#include <mitiru/render/Texture.hpp>   // 画像を直接渡して描く drawSprite のため

#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;

constexpr float kScreenW = 1280.0f, kScreenH = 720.0f;
constexpr float kMinX = 120.0f, kMaxX = kScreenW - 120.0f;   // 赤べこと餌が動ける範囲
constexpr float kMinY = 240.0f, kMaxY = kScreenH - 130.0f;   // (タイトルと操作帯を避ける)

// 赤べこは「胴」と「頭 (表情ちがい 4 枚)」の 2 枚を重ねて 1 匹になる。頭を首のところで
// 回すと、赤べこ名物の「こっくり」首振りになる。画像は可変長データを持つのでゲームの状態
// struct には入れられない (状態はポインタを持たない単純なデータの塊に保つ決まり)。そこで
// 画像はこのファイル直下の変数へ一度だけ読み込む。読み込み失敗時は value_or で空画像にする。
static const render::Texture kBody = render::Texture::fromFile(
	"observe/assets/sprites/akabeko_body.png").value_or(render::Texture{});
static const render::Texture kHeadNeutral = render::Texture::fromFile(
	"observe/assets/sprites/akabeko_head_neutral.png").value_or(render::Texture{});
static const render::Texture kHeadHappy = render::Texture::fromFile(
	"observe/assets/sprites/akabeko_head_happy.png").value_or(render::Texture{});
static const render::Texture kHeadHungry = render::Texture::fromFile(
	"observe/assets/sprites/akabeko_head_hungry.png").value_or(render::Texture{});
static const render::Texture kHeadSleepy = render::Texture::fromFile(
	"observe/assets/sprites/akabeko_head_sleepy.png").value_or(render::Texture{});
// 餌も画像 (スプライト) で描く。赤べこと同じ描き方にそろえておくと、餌を先に・赤べこを後に
// 描いたときに、赤べこがちゃんと餌の前面に出る。
static const render::Texture kFood = render::Texture::fromFile(
	"observe/assets/sprites/akabeko_food.png").value_or(render::Texture{});

constexpr float kScale = 0.5f;    // 画像の何倍で描くか (絵が高解像度なので縮小して使う)
// 首の支点 (頭が回る中心)。画像の中心 (152,116) から (+32,+8) の位置 = 首のつけ根。
constexpr float kPivotOffX = 32.0f, kPivotOffY = 8.0f;

struct Food { float x = 0.0f, y = 0.0f; };

struct Critter
{
	float x = kScreenW * 0.5f, y = kScreenH * 0.5f;
	bool  faceLeft = false;   // 進む向き (左を向いているか)

	// 内部の欲求 — 行動を決める値。画面には出さず、観測窓 (inspector) だけで見える。
	float hunger = 25.0f;   // 0..100  時間で上がる。高くなると餌を探す
	float energy = 100.0f;  // 0..100  動くと減り、休むと回復する
	float mood   = 70.0f;   // 0..100  腹が満ちて元気なほど高い
	FixedString<12> action; // 今の行動名 (WANDER / SEEK_FOOD / EAT / REST)

	// 目標地点・餌・乱数・見た目の位相など、動きを作るための内部データ。
	float goalX = kScreenW * 0.5f, goalY = kScreenH * 0.5f, wanderT = 0.0f;
	float nodPhase = 0.0f, hopPhase = 0.0f;   // 首振り・跳ねのサイン波の位相
	FixedVec<Food, 8> foods;
	int   foodCount = 0;    // 今 画面に出ている餌の数 (食べると減り、時々わいて増える。観測窓で見える)
	int   meals = 0;        // これまでに食べた餌の総数 (観測窓で見える)
	float spawnT = 3.0f;    // 次に餌がわくまでの秒
	unsigned int rng = 2463534242u;   // 決定論的な乱数 (毎回同じ動き = 巻き戻し / リプレイと相性が良い)
	bool started = false;
	bool resting = false;   // 休憩中か (いちど休むと十分回復するまで続く)

	float rnd()   // 0..1 の乱数
	{
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		return static_cast<float>(rng & 0xFFFFFF) / 16777216.0f;
	}
	Food spawnFood() { return { kMinX + rnd() * (kMaxX - kMinX), kMinY + rnd() * (kMaxY - kMinY) }; }

	void update(Input /*in*/, float dt)
	{
		if (!started)   // 最初のフレームで餌を撒く
		{
			started = true;
			for (int i = 0; i < 3; ++i) { (void)foods.push_back(spawnFood()); }
			action.set("WANDER");
		}

		// 時々あたらしい餌がわく (最大 8 個)。食べると減るので、画面の餌の数はいつも変わる。
		spawnT -= dt;
		if (spawnT <= 0.0f)
		{
			spawnT = 3.0f;
			if (foods.size() < 8) { (void)foods.push_back(spawnFood()); }
		}

		hunger = std::min(100.0f, hunger + 4.5f * dt);   // だんだん腹が減る

		// 一番近い餌を探す
		int nearest = -1;
		float nd = 1e18f;
		for (std::size_t i = 0; i < foods.size(); ++i)
		{
			const float d = sq(foods[i].x - x) + sq(foods[i].y - y);
			if (d < nd) { nd = d; nearest = static_cast<int>(i); }
		}

		// 疲れたら休む。いちど休み始めたら十分回復するまで続く (だから寝姿がしばらく見られる)。
		if (resting) { if (energy >= 75.0f) { resting = false; } }
		else         { if (energy < 28.0f)  { resting = true;  } }

		// 行動を決める。優先度: 休む > 腹が減った > それ以外はうろうろ。
		float speed = 0.0f;
		if (resting)
		{
			action.set("REST");
			energy = std::min(100.0f, energy + 20.0f * dt);
		}
		else if (hunger > 60.0f && nearest >= 0)
		{
			action.set("SEEK_FOOD");
			goalX = foods[nearest].x; goalY = foods[nearest].y;
			speed = 150.0f; energy -= 6.0f * dt;
			if (nd < 40.0f * 40.0f)   // 餌に届いた → 食べる
			{
				hunger = std::max(0.0f, hunger - 55.0f);
				action.set("EAT");
				foods.removeAt(static_cast<std::size_t>(nearest));   // その餌は消える
				++meals;
			}
		}
		else
		{
			action.set("WANDER"); speed = 85.0f; energy -= 2.5f * dt;
			wanderT -= dt;
			if (wanderT <= 0.0f || sq(goalX - x) + sq(goalY - y) < 44.0f * 44.0f)
			{
				goalX = kMinX + rnd() * (kMaxX - kMinX);
				goalY = kMinY + rnd() * (kMaxY - kMinY);
				wanderT = 2.0f + rnd() * 2.0f;
			}
		}
		energy = clampf(energy, 0.0f, 100.0f);

		// 目標へ向かって進む
		const float dx = goalX - x, dy = goalY - y;
		const float len = std::sqrt(dx * dx + dy * dy) + 0.001f;
		x += (dx / len) * speed * dt;
		y += (dy / len) * speed * dt;
		if (speed > 1.0f) { faceLeft = (dx < 0.0f); }   // 進む向きに顔を向ける

		mood = clampf(100.0f - hunger * 0.5f - (100.0f - energy) * 0.3f, 0.0f, 100.0f);

		// 見た目の位相を進める。機嫌が良いほど首振りは速く、元気なほど跳ねが速い。
		nodPhase += (2.0f + (mood / 100.0f) * 3.0f) * dt;
		hopPhase += (5.0f + (energy / 100.0f) * 3.0f) * dt;

		foodCount = static_cast<int>(foods.size());   // 観測窓に出す用に、今の餌の数を控える
	}

	// 今の気分に合う頭 (表情) を選ぶ。休む→眠い / 腹ぺこ→ひもじい / 上機嫌→にっこり / それ以外→ふつう。
	const render::Texture& pickHead() const
	{
		if (std::strcmp(action.c_str(), "REST") == 0) { return kHeadSleepy; }
		if (hunger > 55.0f)                           { return kHeadHungry; }
		if (mood   > 68.0f)                           { return kHeadHappy;  }
		return kHeadNeutral;
	}

	// 赤べこ 1 匹を中心 (cx, cy)・大きさ kScale で描く。胴を描いてから、頭を首の支点まわりに
	// nodDeg 度ぶん回して重ねる。faceLeft なら胴・頭ともに左右反転し、支点も鏡像にする。
	void drawBeko(Screen& s, float cx, float cy, float nodDeg) const
	{
		const float w = kBody.width() * kScale, h = kBody.height() * kScale;
		const Rect dst{ cx - w * 0.5f, cy - h * 0.5f, w, h };
		const Rect srcBody{ 0.0f, 0.0f, kBody.width() * 1.0f, kBody.height() * 1.0f };
		s.drawSprite(kBody, dst, srcBody, color::White, faceLeft);

		const render::Texture& head = pickHead();
		const Rect srcHead{ 0.0f, 0.0f, head.width() * 1.0f, head.height() * 1.0f };
		const float side   = faceLeft ? -1.0f : 1.0f;
		const float pivotX = cx + side * kPivotOffX * kScale;   // 首の支点 (向きで左右反転)
		const float pivotY = cy + kPivotOffY * kScale;
		s.pushRotation(deg(nodDeg * side), pivotX, pivotY);     // 支点まわりに頭を回す
		s.drawSprite(head, dst, srcHead, color::White, faceLeft);
		s.popTransform();
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);

		// 餌 (スプライト。赤べこより先に描くので、あとで描く赤べこが前面になる)
		for (std::size_t i = 0; i < foods.size(); ++i)
		{
			const float fs = 26.0f;   // 画面での大きさ (直径)
			const Rect dst{ foods[i].x - fs * 0.5f, foods[i].y - fs * 0.5f, fs, fs };
			const Rect src{ 0.0f, 0.0f, kFood.width() * 1.0f, kFood.height() * 1.0f };
			s.drawSprite(kFood, dst, src, color::White, false);
		}

		// 首振り: サイン波でこっくり。機嫌が良いと大きく、疲れていると小さくなる。
		const float nodAmp = (3.5f + (mood / 100.0f) * 5.0f) * (0.45f + 0.55f * energy / 100.0f);
		const float nodDeg = std::sin(nodPhase) * nodAmp;

		// 跳ね: 元気で休んでいないときだけ、周期的に軽くホップする。
		const bool  lively = (energy > 60.0f) && (std::strcmp(action.c_str(), "REST") != 0);
		const float hop    = lively ? 12.0f * std::fabs(std::sin(hopPhase)) : 0.0f;

		// 赤べこは餌より前面 (餌を先に描いてある) に、跳ねぶんだけ上へずらして描く。
		drawBeko(s, x, y - hop, nodDeg);

		chapterTitle(s, "Observe");
		chapterControls(s, "別の窓 --inspect で 内部の値 (腹・体力・機嫌・えさの数) を観測できる　ESC: おわる");
	}

	static float sq(float v) { return v * v; }
	static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
};

// 状態の構造をエンジンに登録する。読ませたいフィールド名を並べるだけでよい。これだけで、外の
// 観測ツールが hunger / energy / mood / action を名前付きのデータとして読めるようになる。
MITIRU_REFLECT(Critter, energy, hunger, mood, action, foodCount, meals);

// 実行:  mitiru_host.exe observe/observe.dll --inspect
MITIRU_GAME(Critter);
