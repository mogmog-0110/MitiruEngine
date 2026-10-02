// outdoor3d。「屋外の 1 枚」の見本 (高さマップの地形、層の塗り分け、草、撒いた岩と松、水面、空とフォグ)
//   描画は world.json 1 つを drawOutdoor に渡すだけ。地形・草・岩・松・水面は host が描く。
//   当たり判定は同じ world.json をこの DLL が読んで作るので、見た目の地形とキャラが歩く地形は同じ面になる。
//   草のなびきと波、キャラが草を押し倒すのは描画だけのもので、drawOutdoor に渡す時刻 (ゲームの状態の t) と
//   足元の球だけで決まる。
// あそびかた: 島の浜辺から、橙の箱のキャラが歩く。丘を登り、松林を抜け、浅瀬では歩みが遅くなる。草はキャラの足元で倒れる。
// この章で使う関数: loadOutdoorWorld / addOutdoorWorld / waterDepthAt / drawOutdoor (world.json、時刻、草を押す球) /
//   sceneLook3D (空・フォグ・カスケード影)
// 素材は make_assets.py が作る (高さマップは 16 bit の PNG)。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mitiru.hpp>
#include <mitiru/action/CameraRig.hpp>
#include <mitiru/action/CharacterController.hpp>
#include <mitiru/terrain/OutdoorCollision.hpp>
#include <mitiru/terrain/OutdoorWorldLoad.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace act = mitiru::action;
namespace ter = mitiru::terrain;

constexpr const char* kWorldPath = "outdoor3d/assets/island.world.json";

// 地形と当たり判定は、DLL の読み込み時とホットリロード時に作り直し、その後は変えない
const std::unique_ptr<ter::OutdoorWorld> kWorld = ter::loadOutdoorWorld(kWorldPath).world;
const act::CollisionLevel kLevel = [] {
	act::CollisionLevelBuilder b;
	if (kWorld) { ter::addOutdoorWorld(b, *kWorld, 0, 1); }   // 地形は layer 0、岩と松の幹は layer 1
	return b.build();
}();

constexpr act::CharacterConfig kHero{};
constexpr float kWalkSpeed = 5.0f;
// 高い日。2 トーンのトゥーンは光に背いた斜面を影の側に入れるので、低い日だと丘の片側が全部影になる
constexpr act::Vec3 kSun{-0.3f, -0.85f, -0.25f};

// 状態は数値だけで、まるごとコピーできる。巻き戻しと記録、再生にそのまま使える
struct Outdoor3D
{
	act::CharacterState hero{};
	act::CameraRigState cam{};
	act::CameraView     view{};
	act::Vec3           facing{1, 0, 0};
	float               t = 0.0f;

	void init()
	{
		const float x = -30.0f, z = 30.0f;   // 南西の浜辺
		hero.position = {x, kWorld ? kWorld->terrain.heightAt(x, z) + kHero.skinWidth : 0.0f, z};
		hero.grounded = 1;
		cam.yaw = 0.9f;
	}

	void update(Input in, float dt)
	{
		t += dt;
		const act::CollisionWorld world(&kLevel);
		const Stick     m   = in.move();   // カメラから見た前後左右へ歩く (move の y は下が正)
		const act::Vec3 fwd = act::cameraRigForward(cam.yaw, 0.0f), right = act::cameraRigRight(fwd);
		act::Vec3 run = (right * m.x - fwd * m.y) * kWalkSpeed;
		if (kWorld && !kWorld->water.empty())
		{
			// 水深が増すほど歩みが遅くなり、1.2 m で止まる
			const float depth = ter::waterDepthAt(kWorld->water[0], kWorld->terrain, hero.position.x, hero.position.z);
			run = run * std::clamp(1.0f - depth / 1.2f, 0.0f, 1.0f);
		}
		if (act::lengthSq(run) > 0.01f) { facing = act::normalizeOr(run, facing); }
		act::stepCharacter(hero, kHero, world, {run, 0.0f}, dt);

		act::CameraRigInput ci;
		ci.follow = hero.position;
		ci.orbitX = (in.down(Key::E) ? 1.0f : 0.0f) - (in.down(Key::Q) ? 1.0f : 0.0f) + in.rightStick().x;
		ci.orbitY = in.rightStick().y;
		view      = act::updateCameraRig(cam, act::CameraRigConfig{}, ci, world, dt);
	}

	static render::SceneLook look()
	{
		render::SceneLook l;
		l.shadow = true;
		l.shadowDirection[0] = kSun.x; l.shadowDirection[1] = kSun.y; l.shadowDirection[2] = kSun.z;
		l.shadowCascaded = true;
		l.shadowCascadeAutoFit = true;
		l.shadowCascadeCount = 3;
		l.shadowDistance = 90.0f;
		l.toonBands = 2;   // 影と日なたの 2 トーン (3 段だと日なたが中間の帯に入り、影が薄く見える)
		l.fog = true;   // 遠い島の縁と海を空の色へなじませる
		l.fogColor[0] = 0.62f; l.fogColor[1] = 0.74f; l.fogColor[2] = 0.86f;
		l.fogNear = 70.0f;
		l.fogFar = 320.0f;
		l.sky.enabled = 1;
		l.sky.groundHeight = 3.0f;
		const float skyFill[3] = {0.55f, 0.65f, 0.85f}, groundFill[3] = {0.30f, 0.26f, 0.18f};   // 影が空の明るさで消えない強さ
		std::copy(skyFill, skyFill + 3, l.ambientSky);
		std::copy(groundFill, groundFill + 3, l.ambientGround);
		return l;
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0x9FC3E6));
		s.camera3D(view.eye, view.target, view.up, view.fovDeg, 0.2f, 2000.0f);   // 海を水平線まで見せる
		s.light3D(kSun, hex(0xFFF1DC));
		s.toon3D(true, sgc::Colorf{0.45f, 0.52f, 0.70f, 1.0f});   // 影を少し濃くして、木と人の影を草の上で読めるようにする
		s.sceneLook3D(look());
		const act::Vec3 p = hero.position;
		render::OutdoorDrawPod world;
		world.timeSec = t;
		world.benderCount = 1;   // キャラの足元の草を倒す
		world.benders[0][0] = p.x; world.benders[0][1] = p.y; world.benders[0][2] = p.z; world.benders[0][3] = 0.9f;
		s.drawOutdoor(kWorldPath, world);   // 地形・草・岩・松・水面
		s.drawMesh("cube", p + act::Vec3{0, 0.8f, 0}, {0.6f, 1.6f, 0.6f}, {0, 0, 0}, hex(0xFF9500));
		s.drawMesh("cube", p + act::Vec3{0, 1.25f, 0} + facing * 0.32f, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, hex(0x1D1D1F));
		chapterTitle(s, kWorld ? "屋外" : "屋外 (world.json を読めない)");
		chapterControls(s, "矢印: あるく　Q / E: カメラを回す");
	}
};

// 実行:  mitiru_host.exe outdoor3d/outdoor3d.dll
MITIRU_ASSERT_NO_PADDING(Outdoor3D);
MITIRU_GAME(Outdoor3D);
