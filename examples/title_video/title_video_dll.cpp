// title_video。ゲームの時刻だけで動画 (assets/video/title.mpg) を映すタイトル画面
// 実行すると: 星空に月が昇り、流れ星のあとに MITIRU のロゴが現れる 8 秒の映像が流れる。終わると最後の絵のまま「スタート」が出る
// 関連 API: Screen::drawVideo / hud.music / hud.pauseMusic / hud.seekMusic / hud.resumeMusic / in.paused
// 動画ファイルは MPEG-1 (.mpg)。作り方は docs/VIDEO.md。使うには mitiru.toml の features に "video" が要る。
#include <algorithm>   // std::min
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"
using namespace mitiru;

constexpr float kLength = 8.0f;   // title.mpg の長さ (秒)

struct TitleVideo
{
	// 映像の絵は t だけで決まる。巻き戻せば絵も戻り、終わりを超えた秒は最後の絵のまま止まる
	float t = 0.0f;
	int   state = 0;        // 0 = 映像が流れている、1 = 終わって「スタート」を待っている
	int   audioOn = 0;      // 音を頼んだか (音は host が鳴らす。ゲームの全状態には音の位置を持たない)
	int   wasPaused = 0;
	int   started = 0;      // スタートを押した回数

	void init() { *this = TitleVideo{}; }

	void update(Input in, Hud hud, float dt)
	{
		if (in.cancelPressed()) { hud.quit(); }
		if (in.pressed(Key::R)) { *this = TitleVideo{}; hud.stopMusic(0.0f); return; }
		if (state == 0 && !audioOn) { hud.music("title_video", false, 0.8f); audioOn = 1; }   // 映像の頭と同じフレームで音も始める
		// 一時停止の間は音も止める。戻ったときは映像の秒へ音を合わせてから続ける (巻き戻した後もずれない)
		const int paused = in.paused() ? 1 : 0;
		if (state == 0 && paused != wasPaused)
		{
			if (paused) { hud.pauseMusic(); }
			else        { hud.seekMusic(t); hud.resumeMusic(); }
		}
		wasPaused = paused;
		if (state == 0)
		{
			t = std::min(t + dt, kLength);
			if (in.confirmPressed() || t >= kLength) { state = 1; t = kLength; hud.stopMusic(0.4f); }   // 決定で飛ばす
		}
		else if (in.confirmPressed()) { ++started; }
	}

	template <class Surface>
	void drawImpl(Surface& s) const
	{
		s.fillScreen(hex(0x0B1020));
		s.drawVideo("title", Rect{0.0f, 0.0f, 1280.0f, 720.0f}, t);   // 動画の名前は assets/video/<名前>.mpg
		chapterTitle(s, "動画");
		if (state == 1)
		{
			const char* text = started > 0 ? "スタートしました" : "Space でスタート";
			s.drawTextInRect(Rect{0.0f, 600.0f, 1280.0f, 44.0f}, text, rgba(230, 236, 250, 235), 30.0f,
			                 Surface::TextAlignH::Center, Surface::TextAlignV::Middle);
		}
		chapterControls(s, "Space: とばす / スタート　R: さいしょから　ESC: おわる");
	}
	void draw(Screen& s) const { drawImpl(s); }
	void draw(Canvas& c) const { drawImpl(c); }
};

MITIRU_REFLECT_AUTO(TitleVideo);
MITIRU_ASSERT_NO_PADDING(TitleVideo);
MITIRU_GAME(TitleVideo);
