// music_layers。危なさに合わせて曲が厚くなる (曲の強さで層を足す、拍に合わせて重ねる)
// 実行すると: 夜の野原で赤い影が青い灯りへ寄ってくる。近いほど太鼓、さらに近いと低音が曲に加わり、Space で追い払うと次の拍で鐘が鳴る。
// 関連 API: hud.music / hud.musicIntensity / in.music / hud.play (assets/audio/music.json と sounds.json)

#include <cmath>
#include <cstdint>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

// 曲は assets/audio/music.json に書いてある。区間 night (2 小節) を 3 つの層で鳴らし、太鼓と低音の層は
// 「強さ」に合わせて host が拍の区切りで上げ下げする。ゲームは強さを 0..1 で渡すだけでよい。
// 追い払う時の風の音は assets/audio/sounds.json に 2 つの候補と音の高さの揺れを書いてある
constexpr Rect kField{120.0f, 78.0f, 1040.0f, 560.0f};   // 野原 (暗いカード)
constexpr float kWalk = 260.0f, kShadowSpeed = 70.0f;      // 灯りと影の速さ (px/秒)

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 層の聞こえ方を画面に出すための近似。music.json の curve と同じ折れ線にしてある
float layerLevel(int layer, float intensity)
{
	if (layer == 1) { return clamp01((intensity - 0.25f) / 0.20f); }   // 太鼓: 0.25 から 0.45 で上がる
	if (layer == 2) { return clamp01((intensity - 0.60f) / 0.20f); }   // 低音: 0.6 から 0.8 で上がる
	return 1.0f;                                                       // 和音はいつも鳴る
}

struct MusicLayers
{
	Vec2  light{360.0f, 360.0f};   // 青い灯り (自分)
	Vec2  shadow{980.0f, 200.0f};  // 赤い影
	float intensity = 0.0f;        // 曲の強さ 0..1 (影が近いほど強い)
	float sent = -1.0f;            // 最後に host へ渡した強さ
	float pushed = 0.0f;           // 追い払った輪が広がる間 1 → 0
	std::int32_t beat = -1;        // 耳に届いている拍 (小節の中で 0..3)。曲が鳴っていなければ -1
	float beatPhase = 0.0f;        // 拍の中の位置 0..1

	void update(Input in, Hud hud, float dt)
	{
		hud.music("night");   // 同じ区間を毎フレーム頼んでも、鳴らし始めるのは 1 回だけ
		const module::MusicClock& clock = in.music();   // 録画にも乗るので、再生しても同じ拍になる
		beat = clock.playing != 0 ? clock.beatInBar : -1;
		beatPhase = clock.beatPhase;

		const Stick m = in.move();
		light.x = std::fmin(std::fmax(light.x + m.x * kWalk * dt, kField.x() + 30.0f), kField.right() - 30.0f);
		light.y = std::fmin(std::fmax(light.y + m.y * kWalk * dt, kField.y() + 30.0f), kField.bottom() - 30.0f);

		const Vec2  toLight = light - shadow;   // 影は灯りへまっすぐ寄ってくる
		const float dist = std::sqrt(toLight.x * toLight.x + toLight.y * toLight.y);
		if (dist > 40.0f) { shadow += toLight * (kShadowSpeed * dt / dist); }

		intensity = clamp01(1.0f - (dist - 90.0f) / 380.0f);
		if (std::fabs(intensity - sent) > 0.01f)   // host が覚えているので、変わった時だけ渡せばよい
		{
			hud.musicIntensity(intensity);
			sent = intensity;
		}

		if (in.pressed(Key::Space) && dist < 260.0f)
		{
			hud.play("whoosh");   // sounds.json の効果音。すぐ鳴る (2 つの音から選び、高さも少し揺れる)
			hud.play("chime");    // music.json のスティンガー。曲の次の拍に合わせて鳴る
			shadow = light - toLight * (420.0f / dist);   // 影を反対側へ押し戻す
			shadow.x = std::fmin(std::fmax(shadow.x, kField.x() + 30.0f), kField.right() - 30.0f);
			shadow.y = std::fmin(std::fmax(shadow.y, kField.y() + 30.0f), kField.bottom() - 30.0f);
			pushed = 1.0f;
		}
		pushed = std::fmax(0.0f, pushed - dt * 1.5f);
	}

	// 下の段: 層ごとに 1 小節 4 拍の点を並べ、いま鳴っている拍を光らせる。聞こえていない層の点は暗いまま
	void drawBeats(Screen& s) const
	{
		const char* kNames[3] = {"和音", "太鼓", "低音"};
		const Color kColors[3] = {theme::kBlue, theme::kOrange, theme::kPink};
		for (int layer = 0; layer < 3; ++layer)
		{
			const float y = kField.bottom() - 108.0f + 34.0f * static_cast<float>(layer);
			const float level = layerLevel(layer, intensity);
			s.drawTextInRect(Rect{kField.x() + 24.0f, y - 14.0f, 80.0f, 28.0f}, kNames[layer],
			                 theme::kCardInk, 18.0f, Screen::TextAlignH::Left, Screen::TextAlignV::Middle);
			for (int b = 0; b < 4; ++b)
			{
				const float flash = (b == beat) ? 1.0f - beatPhase : 0.0f;
				const float x = kField.x() + 130.0f + 46.0f * static_cast<float>(b);
				s.fillCircle(x, y, 9.0f + 5.0f * flash * level, kColors[layer].withAlpha(0.15f + 0.85f * level * (0.35f + 0.65f * flash)));
			}
		}
	}

	// 下の段の右: 曲の強さの目盛り。太鼓と低音が加わり始める強さに印を付ける
	void drawGauge(Screen& s) const
	{
		const Rect rail{kField.x() + 380.0f, kField.bottom() - 80.0f, 600.0f, 12.0f};
		s.drawRoundedRect(rail, theme::kCardInk.withAlpha(0.25f), 6.0f);
		s.drawRoundedRect(Rect{rail.x(), rail.y(), rail.width() * intensity, rail.height()}, theme::kRed.withAlpha(0.85f), 6.0f);
		const float marks[2] = {0.25f, 0.6f};
		const Color markColors[2] = {theme::kOrange, theme::kPink};
		for (int i = 0; i < 2; ++i)
		{
			const float x = rail.x() + rail.width() * marks[i];
			s.drawRect(Rect{x - 2.0f, rail.y() - 10.0f, 4.0f, rail.height() + 20.0f}, markColors[i]);
		}
	}

	void draw(Screen& s) const
	{
		s.fillScreen(theme::kPaper);
		s.drawRoundedRect(kField, theme::kCard, 20.0f);   // 光は白地では見えないので、暗いカードの中に描く
		const float beatGlow = (beat >= 0) ? 1.0f - beatPhase : 0.0f;   // 灯りの輪は拍ごとに広がる
		s.glowRing(light.x, light.y, 22.0f + 26.0f * beatGlow * intensity, theme::kBlue.withAlpha(0.35f + 0.5f * intensity), 2.0f, 8.0f, 40);
		s.fillCircle(light.x, light.y, 16.0f, theme::kBlue);
		s.glowRing(shadow.x, shadow.y, 20.0f + 10.0f * intensity, theme::kRed.withAlpha(0.4f + 0.6f * intensity), 2.0f, 9.0f, 40);
		s.fillCircle(shadow.x, shadow.y, 14.0f, theme::kRed.withAlpha(0.85f));
		if (pushed > 0.0f) { s.glowRing(light.x, light.y, 40.0f + 260.0f * (1.0f - pushed), theme::kAmber.withAlpha(pushed), 2.0f, 8.0f, 48); }
		drawBeats(s);
		drawGauge(s);
		chapterTitle(s, "曲の強さ");
		chapterControls(s, "矢印: うごく　Space: 影を追い払う");
	}
};

// 実行:  mitiru_host.exe music_layers/music_layers.dll
MITIRU_ASSERT_NO_PADDING(MusicLayers);
MITIRU_GAME(MusicLayers);
