// vcam3d。仮想カメラの切り替えと衝撃の揺れの見本 (廊下の固定カメラ → 追従 → 広間のボス戦の演出カメラ)
// あそびかた: 矢印で歩く。暗い廊下では隅の固定カメラが混ぜずに切り替わり(ホラーの定番)、広間に出ると追従へ1秒で混ざり、
//   ボスに近づくと上からの演出カメラへ混ざる。ボスは2秒ごとに衝撃波を出し、近くにいると被弾して画面が揺れる。
//   揺れは設定の「カメラの揺れ」(Screen::cameraShakeScale) に従う。混ぜの途中の姿も GameMemory に入るので、巻き戻すとその姿に戻る。

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mitiru.hpp>
#include <mitiru/action/CameraRig.hpp>
#include <mitiru/action/CharacterController.hpp>
#include <mitiru/camera/CameraImpulse.hpp>
#include <mitiru/camera/CameraOcclusion.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace act = mitiru::action;
namespace cam = mitiru::camera;

struct Block { act::Vec3 lo, hi; std::uint32_t col; };
constexpr Block kBlocks[] = {
	{{-8, -1, -2}, {8, 0, 26}, 0x3A3F4A},        // 床
	{{-2.5f, 0, -2}, {-2, 3, 10}, 0x5B5348},        // 廊下の左右の壁
	{{2, 0, -2}, {2.5f, 3, 10}, 0x5B5348},
	{{-7.5f, 0, 9.5f}, {-2.5f, 3, 10}, 0x474C57},   // 広間の手前と左右と奥の壁
	{{2.5f, 0, 9.5f}, {7.5f, 3, 10}, 0x474C57},
	{{-8, 0, 10}, {-7.5f, 3, 26}, 0x474C57},
	{{7.5f, 0, 10}, {8, 3, 26}, 0x474C57},
	{{-8, 0, 25.5f}, {8, 3, 26}, 0x474C57},
};
constexpr act::Vec3 kBoss{0, 0, 19};
constexpr float kPulseSec = 2.0f, kPulseReach = 7.0f, kBossCamReach = 9.0f, kCorridorEnd = 9.0f;
constexpr act::CharacterConfig kHero{};  constexpr act::CameraRigConfig kRig{};
constexpr cam::CameraShakeShape kShake{0.18f, 2.0f, 3.0f, 26.0f};
enum : int { kFollow, kCorridor, kBossCam };

const act::CollisionLevel kLevel = [] { act::CollisionLevelBuilder b; for (const Block& k : kBlocks) { b.addBox(k.lo, k.hi, 0); } return b.build(); }();

struct Vcam3D
{
	act::CharacterState        hero{};
	act::CameraRigState        rig{};
	cam::VirtualCameraStack<4> cams{};
	cam::CameraImpulseSet<4>   shakes{};   // 被弾の時刻と強さだけ。揺れの量は毎フレーム時刻から求める
	cam::CameraView            view{};
	float                      t = 0.0f;
	std::uint32_t              hits = 0;

	void init()
	{
		hero.position = {0, kHero.skinWidth, 3}; hero.grounded = 1;
		cams.configure(kFollow, 1, 1.0f);                           // 追従: 1 秒かけて混ぜる
		cams.configure(kCorridor, 5, 0.0f);                         // 廊下の固定: 混ぜずに切り替える
		cams.configure(kBossCam, 9, 1.2f, cam::BlendCurve::EaseOut);   // ボス戦の演出: 寄るように混ぜる
		cams.setActive(kFollow, true);
	}

	void update(Input in, Hud hud, float dt)
	{
		const act::CollisionWorld world(&kLevel);
		const Stick m = in.move();   // 3 台とも +Z を向くので、画面の右は -X
		act::stepCharacter(hero, kHero, world, {act::Vec3{-m.x, 0, -m.y} * 5.0f, 0.0f}, dt);
		const float before = t;
		t += dt;
		if (std::floor(t / kPulseSec) != std::floor(before / kPulseSec) && act::length(hero.position - kBoss) < kPulseReach)
		{
			shakes.add(t, 0.9f, {0, -1, 0}, 0.6f, ++hits);
		}
		const act::Vec3 chest = hero.position + act::Vec3{0, 1.2f, 0};
		cams.setView(kFollow, act::updateCameraRig(rig, kRig, act::CameraRigInput{hero.position}, world, dt));
		cams.setView(kCorridor, cam::CameraView{{1.5f, 2.7f, -1.5f}, chest, 62.0f});
		cams.setView(kBossCam, cam::CameraView{{0, 5.5f, 8.5f}, act::lerp(chest, kBoss + act::Vec3{0, 1, 0}, 0.5f), 58.0f});
		cams.setActive(kCorridor, hero.position.z < kCorridorEnd);
		cams.setActive(kBossCam, act::length(hero.position - kBoss) < kBossCamReach);
		view = cam::avoidWalls(cam::resolveVirtualCamera(cams, dt), world, chest);   // 混ぜの途中で壁に入らない
		cams.commitShown(view);
		if (cams.cut != 0) { hud.cameraCut(); }
	}

	void draw(Screen& s) const
	{
		const cam::CameraView v = cam::applyCameraShake(view, cam::sampleCameraShake(shakes, t, kShake), s.cameraShakeScale());
		s.clear(hex(0x0B0D12));
		s.camera3D(v.eye, v.target, v.up, Deg{v.fovDeg}, 0.1f, 80.0f);
		s.light3D({-0.3f, -1.0f, 0.4f}, hex(0x8C96AA));
		for (const Block& k : kBlocks) { s.drawMesh("cube", (k.lo + k.hi) * 0.5f, k.hi - k.lo, {0, 0, 0}, hex(k.col)); }
		const bool pulsing = std::fmod(t, kPulseSec) < 0.25f;
		s.drawMesh("cube", kBoss + act::Vec3{0, 1, 0}, {1.6f, 2, 1.6f}, {0, 0, 0}, hex(pulsing ? 0xFF3B30 : 0x7A2530));
		s.drawMesh("cube", hero.position + act::Vec3{0, 0.8f, 0}, {0.6f, 1.6f, 0.6f}, {0, 0, 0}, hex(0xFF9500));
		chapterTitle(s, "仮想カメラ");
		char text[96];
		std::snprintf(text, sizeof text, "矢印: あるく　固定 → 追従 → ボス戦のカメラ　被弾 %u 回", hits);
		chapterControls(s, text);
	}
};

// 実行:  mitiru_host.exe vcam3d/vcam3d.dll
MITIRU_ASSERT_NO_PADDING(Vcam3D);
MITIRU_GAME(Vcam3D);
