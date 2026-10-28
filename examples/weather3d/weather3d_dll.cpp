// weather3d。夕方の雨の街 (濡れた路面・雨の粒・雲の層)
// 実行すると、低い夕日と雲の下の通りに雨が降り、降るほど路面が暗くなって街灯が映り込む。
// Space で雨を止める / 降らせる。止んでも路面はしばらく濡れたままで、雲も薄くなる
// 関連 API: SceneLook::wetness / cloudCover / particles3D / render::weatherEmitter / pointLight3D

#include <algorithm>
#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/render/SceneLook.hpp>
#include <mitiru/render/Weather.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

constexpr float kLampX[4] = {-9.0f, -3.0f, 3.0f, 9.0f};
constexpr float kRoadHalf = 4.0f;

struct Weather3D
{
	float t = 0.0f;
	float camYaw = 0.5f;
	float rain = 1.0f;       // 降りの強さ 0..1。Space で目標が切り替わり、なだらかに追う
	float wet = 0.0f;        // 路面の濡れ 0..1。降ると増え、止んでも少しずつしか乾かない
	std::uint8_t raining = 1;
	std::uint8_t _pad[3] = {};

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::Space)) { raining = raining ? 0 : 1; }
		t += dt;
		camYaw += 0.2f * dt;
		rain = std::clamp(rain + (raining ? 0.5f : -0.5f) * dt, 0.0f, 1.0f);
		wet = std::clamp(wet + (rain > 0.3f ? 0.3f : -0.04f) * dt, 0.0f, 1.0f);
	}

	void draw(Screen& s) const
	{
		static const render::WeatherPreset kRain = render::defaultRainPreset();
		const float sway = 0.35f * std::sin(camYaw);   // 通りの奥を向いたまま、ゆっくり左右へ首を振る
		const Vec3 eye{-17.0f, 2.2f, 0.0f};
		s.clear(hex(0x1B1F2B));
		s.camera3D(eye, {0.0f, 2.6f, 14.0f * sway}, {0.0f, 1.0f, 0.0f}, Deg{55.0f}, 0.3f, 300.0f);
		s.light3D({-0.55f, -0.12f, -0.2f}, hex(0xFFB27A));   // 低い夕日。光の向きの反対 (通りの奥) に太陽が出る
		render::SceneLook look;
		look.shadingModel = 1;   // Phong。濡れで光沢が増えると街灯の光の筋が路面に伸びる
		look.ambient[0] = 0.10f; look.ambient[1] = 0.11f; look.ambient[2] = 0.16f;
		look.sky.enabled = 1;
		look.sky.sunIlluminance = 5.0f;
		look.wetness = static_cast<std::uint8_t>(255.0f * wet);
		look.cloudCover = static_cast<std::uint8_t>(255.0f * (0.35f + 0.5f * rain));
		s.sceneLook3D(look);

		s.drawMesh("cube", {0.0f, -0.1f, 0.0f}, {80.0f, 0.2f, 80.0f}, {0, 0, 0}, hex(0x6A6C75));   // 路面
		for (int i = 0; i < 8; ++i)
		{
			const float x = -14.0f + 4.0f * static_cast<float>(i), h = 4.0f + 1.5f * static_cast<float>((i * 5) % 4);
			for (const float side : {-1.0f, 1.0f})
			{
				s.drawMesh("cube", {x, h * 0.5f, side * (kRoadHalf + 2.5f)}, {3.2f, h, 4.0f}, {0, 0, 0}, hex(i % 2 ? 0x8A7F78 : 0x72788A));
			}
		}
		for (const float x : kLampX)
		{
			for (const float side : {-1.0f, 1.0f})
			{
				const Vec3 head{x, 3.4f, side * (kRoadHalf - 0.4f)};
				s.drawMesh("cube", {head.x, 1.7f, head.z}, {0.12f, 3.4f, 0.12f}, {0, 0, 0}, hex(0x2A2C33));
				s.drawMesh("sphere", head, {0.35f, 0.35f, 0.35f}, {0, 0, 0}, hex(0xFFD9A0));
				s.pointLight3D(head, 9.0f, hex(0xFFC27A), 3.0f);
			}
		}
		const float cam[3] = {eye.x, eye.y, eye.z};
		render::WeatherWind wind;
		wind.dirX = -1.0f; wind.dirZ = 0.4f; wind.strength = 0.15f;
		const render::ParticleEmitterDesc drops = render::weatherEmitter(kRain, cam, rain, wind, t);
		s.particles3D(&drops, 1);
		chapterTitle(s, "Weather");
		chapterControls(s, raining ? "Space: あめを止める　Esc: おわる" : "Space: あめを降らせる　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe weather3d/weather3d.dll
MITIRU_REFLECT_AUTO(Weather3D);

MITIRU_ASSERT_NO_PADDING(Weather3D);
MITIRU_GAME(Weather3D);
