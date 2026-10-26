// material3d。自分で書いた材質で描く (assets/materials/*.slang)
// 実行すると、水面の上に 3 つの球が並ぶ。左は溶けて消えては戻る球、中央は走査線のホログラム、右は材質を使わない球。
// 材質は material() だけを書いたテキストで、照明・影・輪郭線はエンジンのまま。Space で時間を止める / 動かす
// 関連 API: registerMaterial3D / material3D / sceneLook3D / camera3D

#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/render/SceneLook.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

struct Material3D
{
	float t = 0.0f;   // 材質に渡す秒。ゲームの状態として持つので、巻き戻すと材質の絵も戻る
	std::uint8_t paused = 0;
	std::uint8_t _pad[3] = {};

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::Space)) { paused = paused ? 0 : 1; }
		if (paused == 0) { t += dt; }
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0x0B1220));
		s.camera3D({0.0f, 2.6f, 7.5f}, {0.0f, 0.9f, 0.0f}, {0.0f, 1.0f, 0.0f}, Deg{45.0f}, 0.5f, 60.0f);
		s.light3D({-0.4f, -1.0f, -0.5f}, hex(0xFFF1DC));
		render::SceneLook look;
		look.ambient[0] = 0.18f; look.ambient[1] = 0.2f; look.ambient[2] = 0.26f;
		look.shadingModel = 2;   // トゥーン。Dawn もトゥーンだけなので、どの描画でも同じ絵になる
		look.toonBands = 3;
		look.shadow = true;
		look.shadowDirection[0] = -0.4f; look.shadowDirection[1] = -1.0f; look.shadowDirection[2] = -0.5f;
		s.sceneLook3D(look);

		// 登録の時にコンパイルを済ませるので、毎フレーム呼んでも番号を引くだけ
		const int melt = s.registerMaterial3D("melt");
		const int hologram = s.registerMaterial3D("hologram");
		const int water = s.registerMaterial3D("water");

		const float waterParams[3] = {1.0f, 1.0f, 1.0f};
		s.material3D(water, waterParams, 3, t);
		s.drawMesh("cube", {0.0f, -0.15f, 0.0f}, {14.0f, 0.3f, 8.0f}, {0, 0, 0}, hex(0xFFFFFF));

		// 0 から 0.75 までを往復して、溶け具合を変える
		const float amount = 0.375f - 0.375f * std::cos(t * 0.8f);
		const float meltParams[3] = {amount, 0.08f, 2.0f};
		s.material3D(melt, meltParams, 3, t);
		s.drawMesh("sphere", {-2.6f, 1.0f, 0.0f}, {1.8f, 1.8f, 1.8f}, {0, 0, 0}, hex(0xE8A060));

		const float holoParams[2] = {0.6f, 1.0f};
		s.material3D(hologram, holoParams, 2, t);
		s.drawMesh("sphere", {0.0f, 1.0f, 0.0f}, {1.8f, 1.8f, 1.8f}, {0, 0, 0}, hex(0xFFFFFF));

		s.material3D(0);   // 以後は場面の陰影
		s.drawMesh("sphere", {2.6f, 1.0f, 0.0f}, {1.8f, 1.8f, 1.8f}, {0, 0, 0}, hex(0xE8A060));

		chapterTitle(s, "自作の材質");
		chapterControls(s, paused ? "Space: うごかす　Esc: おわる" : "Space: とめる　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe material3d/material3d.dll
MITIRU_REFLECT_AUTO(Material3D);

MITIRU_ASSERT_NO_PADDING(Material3D);
MITIRU_GAME(Material3D);
