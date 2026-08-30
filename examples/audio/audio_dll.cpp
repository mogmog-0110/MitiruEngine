// audio。音を鳴らし、いまどの音が鳴っているかを画面でも見せる章。
// 中央の暗い計器に、効果音 3 つの専用バー (Z=低い / X=中くらい / C=高い) と、
// BGM を表す回るディスク (再生中=回る / 一時停止=止まったまま / フェード=薄れて消える) が映る。
#include <algorithm>   // std::max / std::min
#include <cmath>       // std::sin / std::cos / std::fmod
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"   // 章ラベル + 操作帯 + 共通の配色
using namespace mitiru;

// 効果音 (SE) は 3 つのキーに 1 対 1 で割り当てる。同じ音 (ping) を再生速度だけ変えて
// 高さを鳴らし分ける。押したキーのバーだけが強く光り + 輪が弾け、他の 2 本は静かなまま。
constexpr Key   kSeKey[3]   = {Key::Z, Key::X, Key::C};                        // 低・中・高
constexpr float kSePitch[3] = {0.75f, 1.0f, 1.5f};                            // 再生速度 = 音の高さ
constexpr Color kSeCol[3]   = {theme::kBlue, theme::kGreen, theme::kPink};    // 音ごとの色
constexpr const char* kSeLbl[3] = {"Z", "X", "C"};                            // バーの下に出すキー名

// BGM の状態。ゲーム自身は音を鳴らす仕組みを持たず、host に「こう鳴らして」と頼むだけ。
// いま再生中か・止めているかは、ゲームが自分で覚えておく。
enum class Bgm : int { Stopped, Playing, Paused };

// 中心 c・半径 r の円周上で、角度 a の点を返す (回転する線や縁の目印に使う)。
static Vec2 onCircle(Vec2 c, float r, float a) { return Vec2{c.x + r * std::cos(a), c.y + r * std::sin(a)}; }

struct Audio07
{
	Bgm   bgm       = Bgm::Stopped;
	float seGlow[3] = {0.0f, 0.0f, 0.0f};   // 各バーの残り光 (押した瞬間 1 → だんだん 0 へ)
	float spin      = 0.0f;    // ディスクの回転角。再生中だけ進む (止めれば回転も止まる)
	float bgmLvl    = 0.0f;    // ディスクの見える濃さ。再生=1 / 一時停止=0.5 / 停止=0 へ滑らかに寄る
	void update(Input in, Hud hud, float dt)
	{
		if (in.cancelPressed()) { hud.quit(); }   // ESC で終わる

		// Z / X / C: それぞれ専用の効果音を 1 発鳴らし、そのバーの残り光を立てる。
		for (int i = 0; i < 3; ++i)
		{
			// 効果音は BGM を邪魔しないよう、控えめな音量 (0.6) で鳴らす。
			// こうすれば BGM を流したまま効果音を重ねても、両方きちんと聞こえる。
			if (in.pressed(kSeKey[i])) { hud.play("ping", 0.6f, kSePitch[i]); seGlow[i] = 1.0f; }
		}

		// Space: 再生 → 一時停止 → 続きから、と切り替える (一時停止は再生位置を覚えている)。
		if (in.pressed(Key::Space))
		{
			if      (bgm == Bgm::Stopped) { hud.music("bgm", true, 0.35f); bgm = Bgm::Playing; }
			else if (bgm == Bgm::Playing) { hud.pauseMusic();             bgm = Bgm::Paused; }
			else                          { hud.resumeMusic();            bgm = Bgm::Playing; }
		}

		// B: 1.5 秒かけて薄れて停止する (再生位置は捨てるので、次の Space は最初から)。
		if (in.pressed(Key::B) && bgm != Bgm::Stopped) { hud.stopMusic(1.5f); bgm = Bgm::Stopped; }

		// 各バーの残り光をだんだん減らす。
		for (int i = 0; i < 3; ++i) { seGlow[i] = std::max(0.0f, seGlow[i] - dt * 2.2f); }

		// ディスクの濃さ bgmLvl を、状態に応じた目標値へ少しずつ近づける (急に変えず滑らかに = フェードの見た目)。
		float target = 0.0f;                             // 停止中は 0
		if      (bgm == Bgm::Playing) { target = 1.0f; }
		else if (bgm == Bgm::Paused)  { target = 0.5f; }
		bgmLvl += (target - bgmLvl) * std::min(1.0f, dt * 3.0f);

		if (bgm == Bgm::Playing) { spin = std::fmod(spin + dt * 2.2f, deg(360.0f)); }
	}
	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		chapterTitle(s, "Audio");
		// 光や脈動は白地では見えないので、計器は暗いカードの中で見せる。
		s.drawRoundedRect(Rect{220.0f, 140.0f, 840.0f, 420.0f}, theme::kCard, 20.0f);

		// 左: 効果音の 3 本のバー。ふだんは薄く見え、鳴らした 1 本だけ強く光り + 輪が弾ける。
		const float baseY = 480.0f;
		s.drawLine(Vec2{306.0f, baseY}, Vec2{596.0f, baseY}, theme::kCardInk.withAlpha(0.35f), 2.0f);
		for (int i = 0; i < 3; ++i)
		{
			const float h = 78.0f + 44.0f * static_cast<float>(i);   // 低い音ほど短いバー
			const float x = 324.0f + 92.0f * static_cast<float>(i);
			const float g = seGlow[i];
			s.drawRoundedRect(Rect{x, baseY - h, 62.0f, h}, kSeCol[i].withAlpha(0.26f + 0.74f * g), 8.0f);
			// バーの下にキー名 (Z/X/C)。位置とキーの対応で「どれが鳴ったか」が読める。
			s.drawTextInRect(Rect{x, baseY + 8.0f, 62.0f, 22.0f}, kSeLbl[i], theme::kCardInk.withAlpha(0.9f),
			                 18.0f, Screen::TextAlignH::Center, Screen::TextAlignV::Middle);
			if (g > 0.0f)   // 鳴った本の上で輪が弾ける (1→0 につれ外へ広がって薄れる)
			{
				s.glowRing(x + 31.0f, baseY - h - 24.0f, 20.0f + 70.0f * (1.0f - g),
				           kSeCol[i].withAlpha(0.9f * g), 2.0f, 9.0f, 44);
			}
		}

		// 右: BGM を表す回るディスク。消えているときも位置が分かるよう、薄い外枠は常に描く。
		const Vec2  disc{820.0f, 348.0f};
		const float R = 104.0f;
		s.drawCircleFrame(disc, R + 10.0f, theme::kCardInk.withAlpha(0.22f), 2.0f);
		if (bgmLvl > 0.01f)
		{
			const float lv = bgmLvl;
			s.glowRing(disc.x, disc.y, R + 12.0f + 9.0f * std::sin(spin * 3.0f),
			           theme::kBlue.withAlpha(0.5f * lv), 2.0f, 8.0f, 40);   // 回転と合わせて脈打つ輪
			s.fillCircle(disc.x, disc.y, R, rgba(18, 28, 52, static_cast<int>(235.0f * lv)));
			s.drawCircleFrame(disc, R, theme::kBlue.withAlpha(0.9f * lv), 3.0f);
			for (int k = 0; k < 3; ++k)   // 回る 3 本の線と縁の目印 (角度が spin で回る = 再生中の合図)
			{
				const float ang = spin + deg(120.0f * static_cast<float>(k));
				const Vec2  rim = onCircle(disc, R - 14.0f, ang);
				s.drawLine(disc, rim, theme::kBlue.withAlpha(0.8f * lv), k == 0 ? 5.0f : 2.0f);
				s.fillCircle(rim.x, rim.y, 6.0f, theme::kPink.withAlpha(0.9f * lv));
			}
			s.fillCircle(disc.x, disc.y, 15.0f, theme::kBlue.withAlpha(lv));   // 中心の軸
		}
		chapterControls(s, "Z / X / C: ひくい / なか / たかい おと　Space: BGM さいせい / いちじていし　B: フェードアウト　ESC: おわる");
	}
};

// 自動テスト中は実際には音は鳴らないが、再生を頼む処理がきちんと動くことは確認できる。
MITIRU_GAME(Audio07);
