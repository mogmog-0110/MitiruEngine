// hdr_sky の実装。HDR の空の下で、金属と水が空を映す
// 昼の空の画像 (Radiance .hdr) を空と環境光にして、粗さを変えた金属の球と水面に映す。太陽は 1 を超える明るさのまま映り、
// 水面の反射と球の光り方で見える。Space で HDR の空と、今までの色の勾配を切り替える。上下キーで空の明るさを変える。
// 関連 API: environmentHdr3D / sceneLook3D (shadingModel = 3) / drawModel / skybox3D / environment3D

#include <algorithm>
#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/render/SceneLook.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

struct HdrSky
{
	float t = 0.0f;
	float intensity = 1.0f;   // 空の明るさ。環境光と空に掛ける
	std::uint8_t hdrOn = 1;
	std::uint8_t _pad[3] = {};

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::Space)) { hdrOn = hdrOn ? 0 : 1; }
		if (in.down(Key::Up)) { intensity = std::min(intensity + dt, 2.5f); }
		if (in.down(Key::Down)) { intensity = std::max(intensity - dt, 0.2f); }
		t += dt;
	}

	void draw(Screen& s) const
	{
		// 太陽 (画像の中の位置) は -Z から +X の側へ 40 度の高い所。その向きを見下ろして、水面の光の筋が入るようにし、ゆっくり左右に振れる
		const float yaw = 0.70f + 0.35f * std::sin(t * 0.3f);
		s.camera3D({-7.0f * std::sin(yaw), 3.0f, 7.0f * std::cos(yaw)}, {0.0f, 0.4f, 0.0f}, {0.0f, 1.0f, 0.0f}, Deg{55.0f}, 0.1f, 300.0f);
		const sgc::Vec3f sunTravel{-0.506f, -0.616f, 0.604f};   // 画像の太陽 (+X 寄り、高さ 38 度) から差す向き
		s.light3D(sunTravel, hex(0xFFE3B8));

		render::SceneLook look;
		look.shadingModel = 3;   // PBR
		look.exposure = 0.7f;
		look.shadow = true;
		look.shadowDirection[0] = sunTravel.x; look.shadowDirection[1] = sunTravel.y; look.shadowDirection[2] = sunTravel.z;
		look.bloom = true;
		look.bloomThreshold = 4.0f;   // 1 を超える空 (太陽のまわり) だけが滲む
		s.sceneLook3D(look);

		// 色の空と環境光は毎フレーム頼んでおく。DX12 は HDR の間これを預かって、外すと戻す。HDR を描けない Dawn はこの色のまま描く
		s.skybox3D(hex(0x2F6FD0), hex(0xCFE3F5));
		s.environment3D(hex(0x2F6FD0), hex(0x6A5F52));
		s.environmentHdr3D(hdrOn ? "assets/sky/day_field_2k.hdr" : "", intensity, Deg{0.0f});
		s.drawModel("assets/models/reflect.glb", {0.0f, 0.0f, 0.0f}, sgc::Vec3f{0.0f, 0.0f, 0.0f}, 1.0f);   // glb をそのまま描く (clod ではなく forward)

		chapterTitle(s, "HDR の空と反射");
		chapterControls(s, hdrOn ? "Space: 色の空にする　上下: 空の明るさ　Esc: おわる" : "Space: HDR の空にする　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe hdr_sky/hdr_sky.dll
MITIRU_REFLECT_AUTO(HdrSky);

MITIRU_ASSERT_NO_PADDING(HdrSky);
MITIRU_GAME(HdrSky);
