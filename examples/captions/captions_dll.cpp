// captions。効果音の字幕の章。鳴らした音のうち assets/audio/sounds.json に caption を書いた音だけが、
// 画面の下寄りに字幕として出る (mitiru:captions.rml)。音の種類 (dialogue / ambient / music / alert) ごとに、
// 利用者が設定 (accessibility.captions) で出す・出さないを選べる。ゲームは hud.play で鳴らすだけで、字幕の
// 出し方は知らなくてよい。読み上げソフト (Narrator) には、新しい字幕の 1 行を host が渡す。
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
using namespace mitiru;

struct CaptionsGame
{
    int plays = 0;   // 鳴らした回数 (状態は POD にする)

    void init() { plays = 0; }

    void update(Input in, Hud hud, float)
    {
        if (in.cancelPressed()) { hud.quit(); }
        // 聞き手は原点で、-z (奥) を向く。音の来る方向の字幕 (左・右・前) は、ここから見て決まる
        hud.listener(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f});
        if (in.pressed(Key::Z)) { hud.play("door").at(Vec3{-6.0f, 0.0f, -2.0f}); ++plays; }
        if (in.pressed(Key::X)) { hud.play("steps").at(Vec3{6.0f, 0.0f, 0.0f}); ++plays; }
        if (in.pressed(Key::C)) { hud.play("alarm").at(Vec3{0.0f, 0.0f, -8.0f}); ++plays; }
        if (in.pressed(Key::V)) { hud.play("hello"); ++plays; }
    }

    void draw(Screen& s) const { s.fillScreen(hex(0x1b2230)); }
    void draw(Canvas& c) const { c.fillScreen(hex(0x1b2230)); }
};

MITIRU_REFLECT_AUTO(CaptionsGame);
MITIRU_ASSERT_NO_PADDING(CaptionsGame);
MITIRU_GAME(CaptionsGame);
