// lights3d。夜の広場に灯りを置く (点光源・スポットライト・インスタンス描画)
// 実行すると、夜の石畳に 100 本の柱が並び、色の違う 6 つの灯りがその間を回る。
// 上からのスポットライトがゆっくり首を振り、柱の影を落とす。Space で灯りを止める / 動かす
// 関連 API: pointLight3D / spotLight3D / drawMeshInstanced / camera3D (近い面と遠い面) / sceneLook3D

#include <array>
#include <cmath>
#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include <mitiru/render/SceneLook.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

constexpr int kSide = 10;                 // 柱は 10 x 10 本
constexpr int kPillars = kSide * kSide;
constexpr float kGap = 1.6f;              // 柱の間隔 (m)

struct Lamp { float radius, speed, phase, height; std::uint32_t rgb; };
constexpr Lamp kLamps[] = {
	{2.5f, 0.7f, 0.0f, 0.8f, 0xFF5A3C}, {4.0f, -0.5f, 1.0f, 1.2f, 0x3CC8FF}, {5.5f, 0.4f, 2.0f, 0.6f, 0x9BFF5A},
	{3.2f, -0.9f, 3.0f, 1.5f, 0xFFD23C}, {6.5f, 0.3f, 4.0f, 1.0f, 0xD25AFF}, {1.2f, 1.2f, 5.0f, 0.9f, 0xFFFFFF},
};

// 柱の配置は変わらないので、読み込みの時に 1 回だけ作る (行列は行優先。tint は柱ごとの色むら)
const auto kPillarInstances = [] {
	std::array<render::MeshInstance, kPillars> out{};
	for (int i = 0; i < kPillars; ++i)
	{
		const float x = (static_cast<float>(i % kSide) - (kSide - 1) * 0.5f) * kGap;
		const float z = (static_cast<float>(i / kSide) - (kSide - 1) * 0.5f) * kGap;
		const float h = 0.8f + 0.6f * static_cast<float>((i * 37) % 7) / 6.0f;
		const float shade = 0.85f + 0.15f * static_cast<float>((i * 13) % 5) / 4.0f;
		out[static_cast<std::size_t>(i)] = render::makeInstance(
			sgc::Mat4f::translation({x, h * 0.5f, z}) * sgc::Mat4f::scaling({0.3f, h, 0.3f}), shade, shade, shade);
	}
	return out;
}();

struct Lights3D
{
	float t = 0.0f;          // 灯りの時計 (秒)
	float camYaw = 0.6f;     // カメラの回り込み (ラジアン)
	std::uint8_t paused = 0;
	std::uint8_t _pad[3] = {};

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		if (in.pressed(Key::Space)) { paused = paused ? 0 : 1; }
		if (paused == 0) { t += dt; }
		camYaw += 0.08f * dt;   // ゆっくり回って、灯りが柱の陰へ入るのを見せる
	}

	Vec3 lampPos(const Lamp& l) const
	{
		const float a = l.phase + l.speed * t;
		return {l.radius * std::cos(a), l.height + 0.3f * std::sin(a * 2.0f), l.radius * std::sin(a)};
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0x0B0E18));
		const Vec3 eye{11.0f * std::cos(camYaw), 6.0f, 11.0f * std::sin(camYaw)};
		s.camera3D(eye, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, Deg{50.0f}, 0.5f, 60.0f);   // 広場は 20 m ほどなので遠い面は 60 m で足りる
		s.light3D({-0.2f, -1.0f, 0.3f}, hex(0x1A2238));             // 月明かり (弱い平行光)
		render::SceneLook look;
		look.ambient[0] = 0.03f; look.ambient[1] = 0.035f; look.ambient[2] = 0.06f;
		look.shadow = true;
		look.shadowDirection[0] = -0.2f; look.shadowDirection[1] = -1.0f; look.shadowDirection[2] = 0.3f;
		look.bloom = true;
		look.bloomThreshold = 0.8f;
		look.volumetricFog.enabled = 1;            // 夜霧。灯りのまわりがぼうっと光る
		look.volumetricFog.density = 0.06f;
		look.volumetricFog.localLightScale = 4.0f;
		look.volumetricFog.heightFalloff = 0.4f;   // 地面ほど濃い
		look.volumetricFog.range = 40.0f;
		s.sceneLook3D(look);

		s.drawMesh("cube", {0.0f, -0.1f, 0.0f}, {20.0f, 0.2f, 20.0f}, {0, 0, 0}, hex(0x5A5C66));   // 石畳
		s.drawMeshInstanced("cube", kPillarInstances.data(), kPillars, hex(0xC8C0B0));          // 柱 100 本を 1 回で

		for (const Lamp& l : kLamps)
		{
			const Vec3 p = lampPos(l);
			s.pointLight3D(p, 3.0f, hex(l.rgb), 2.5f);
			s.drawMesh("sphere", p, {0.15f, 0.15f, 0.15f}, {0, 0, 0}, hex(l.rgb));   // 灯りの玉 (bloom で光る)
		}
		// スポットライト: 真上から斜めに首を振る。castShadow = true で柱の影を落とす
		const float sweep = 0.6f * std::sin(t * 0.5f);
		s.spotLight3D({0.0f, 9.0f, 0.0f}, {sweep, -1.0f, 0.2f}, 14.0f, Deg{10.0f}, Deg{18.0f}, hex(0xFFF2D0), 6.0f, true);

		chapterTitle(s, "3D Lights");
		chapterControls(s, paused ? "Space: うごかす　Esc: おわる" : "Space: とめる　Esc: おわる");
	}
};

// 実行:  mitiru_host.exe lights3d/lights3d.dll
MITIRU_REFLECT_AUTO(Lights3D);

MITIRU_ASSERT_NO_PADDING(Lights3D);
MITIRU_GAME(Lights3D);
