// cave_exposure の実装。自動露出と接触影
// 暗い洞窟から明るい外へ歩くと、外が白く飛び、約 1 秒で明るさに慣れる。外の岩の足元には接触影が付く。
// Space で自動露出を切り替える。24 秒で洞窟の奥と外を往復する。
// 関連 API: sceneLook3D の autoExposureSpeed / contactShadow、camera3D、light3D

#include <algorithm>
#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/render/SceneLook.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

struct CaveExposure
{
	float t = 0.0f;   // 歩いた秒。ゲームの状態なので、巻き戻すと歩きも露出の履歴も同じ場所から再生される
	std::uint8_t autoOn = 1;
	std::uint8_t _pad[3] = {};

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::Space)) { autoOn = autoOn ? 0 : 1; }
		t += dt;
	}

	void draw(Screen& s) const
	{
		// 24 秒で洞窟の奥と外を往復し、両端で 1.5 秒ずつ止まる
		const float phase = std::fmod(t, 24.0f);
		const float walk = std::clamp(((phase < 12.0f ? phase : 24.0f - phase) - 1.5f) / 9.0f, 0.0f, 1.0f);
		const float z = 11.0f - 14.0f * walk;
		s.clear(hex(0x8EC5FF));
		s.camera3D({0.0f, 1.7f, z}, {0.0f, 1.3f, z - 6.0f}, {0.0f, 1.0f, 0.0f}, Deg{60.0f}, 0.1f, 80.0f);
		s.light3D({-0.3f, -1.0f, 0.6f}, hex(0xFFF1DC));

		render::SceneLook look;
		look.ambient[0] = 0.05f; look.ambient[1] = 0.05f; look.ambient[2] = 0.06f;
		look.shadingModel = 2;
		look.toonBands = 3;
		look.shadow = true;
		look.shadowDirection[0] = -0.3f; look.shadowDirection[1] = -1.0f; look.shadowDirection[2] = 0.6f;
		look.autoExposureSpeed = autoOn ? 1.2f : 0.0f;
		look.autoExposureBiasEv = 0.8f;   // 画面の平均を中間灰にすると明るい外が暗く沈むので、少し明るめを狙う
		look.contactShadow = 0.7f;
		look.contactShadowDistance = 0.6f;
		s.sceneLook3D(look);

		const Color rock = hex(0x6B5E52);
		s.drawMesh("cube", {0.0f, -0.15f, -10.0f}, {60.0f, 0.3f, 80.0f}, {0, 0, 0}, hex(0xE6D3A3));
		s.drawMesh("cube", {-3.0f, 2.2f, 7.0f}, {0.5f, 5.0f, 14.0f}, {0, 0, 0}, rock);
		s.drawMesh("cube", {3.0f, 2.2f, 7.0f}, {0.5f, 5.0f, 14.0f}, {0, 0, 0}, rock);
		s.drawMesh("cube", {0.0f, 4.7f, 7.0f}, {7.0f, 0.5f, 14.0f}, {0, 0, 0}, rock);
		s.drawMesh("cube", {0.0f, 2.2f, 14.2f}, {7.0f, 5.0f, 0.5f}, {0, 0, 0}, rock);
		s.drawMesh("cube", {2.0f, 0.4f, -6.5f}, {0.8f, 0.8f, 0.8f}, {0, 0.4f, 0}, hex(0x8C8C94));
		s.drawMesh("cube", {-2.4f, 0.3f, -8.5f}, {0.6f, 0.6f, 0.6f}, {0, 0.9f, 0}, hex(0x8C8C94));
		s.drawMesh("cube", {0.8f, 0.5f, -10.5f}, {1.0f, 1.0f, 1.0f}, {0, 0.2f, 0}, hex(0x8C8C94));

		chapterTitle(s, "自動露出と接触影");
		chapterControls(s, autoOn ? "Space: 自動露出を切る　Esc: おわる" : "Space: 自動露出を入れる　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe cave_exposure/cave_exposure.dll
MITIRU_REFLECT_AUTO(CaveExposure);

MITIRU_ASSERT_NO_PADDING(CaveExposure);
MITIRU_GAME(CaveExposure);
MITIRU_RENDER_NEEDS("auto_exposure,contact_shadow");
