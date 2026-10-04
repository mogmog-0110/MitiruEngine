// level3d。Blender で作ったステージを読み込み、そのまま歩く (見た目・当たり判定・置いた印が 1 つの .glb から来る)
// 実行すると: 石の中庭を橙の箱のキャラが歩く。段と坂で台へ上がれ、奥の台の印に入ると桃色の灯りが強くなる。
// 関連 API: LevelFile / buildLevelCollision / find / forEachOfType / drawModel / stepCharacter / updateCameraRig /
//           lightingBake3D + SceneLook::indirect (焼いた光)

#include <cmath>
#include <vector>
#include <mitiru.hpp>
#include <mitiru/action/CameraRig.hpp>
#include <mitiru/action/CharacterController.hpp>
#include <mitiru/action/CollisionLevelLoad.hpp>
#include <mitiru/level/LevelLoader.hpp>
#include <mitiru/render/SceneLook.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace act = mitiru::action;
namespace lv = mitiru::level;

// ステージは tools/make_stage.py が Blender で作った .glb。見た目は drawModel がこのファイルを描き、
// 当たり判定の面と置いた印 (出発点、ゴールの箱、灯り) はこの DLL が同じファイルから読む。
// 中身はファイルだけで決まるので、ゲームの状態ではなく DLL の static に置く
constexpr const char* kStagePath = "level3d/assets/stage.glb";
// 壁から跳ね返った光と映り込みは、ビルドが stage.lighting.json から焼いておく (mitiru_lightbake。docs/LIGHTING_GI.md)
constexpr const char* kBakePath = "level3d/assets/stage.lighting.bin";
lv::LevelFile g_stage(kStagePath);
act::CollisionLevel g_level = act::buildLevelCollision(g_stage.get());   // Blender の面がそのまま地形になる

act::Vec3 at(const float (&p)[3]) { return {p[0], p[1], p[2]}; }

// 型 "lamp" の印。色・届く距離・ゴールの灯りかどうかは Blender のカスタムプロパティに書いてある
struct Lamp { act::Vec3 pos; Color color; float range; bool goal; };
std::vector<Lamp> readLamps(const lv::LevelData& level)
{
	std::vector<Lamp> lamps;
	level.forEachOfType("lamp", [&](const lv::Entity& e, std::size_t) {
		const lv::Property* c = level.property(e, "color");
		const Color color = (c != nullptr) ? Color{c->value[0], c->value[1], c->value[2], 1.0f} : hex(0xFFFFFF);
		lamps.push_back({at(e.position), color, level.number(e, "range", 4.0f), level.number(e, "goal") != 0.0f});
	});
	return lamps;
}
std::vector<Lamp> g_lamps = readLamps(g_stage.get());

constexpr act::CharacterConfig kHero{};
constexpr act::CameraRigConfig kCam{};
constexpr float kRunSpeed = 4.5f, kJumpSpeed = 8.0f;

// 状態は数値だけ (まるごとコピーできる形)。巻き戻しと記録／再生がそのまま効く
struct Level3D
{
	act::CharacterState hero{};
	act::CameraRigState cam{};
	act::CameraView     view{};
	act::Vec3           facing{0, 0, 1};
	float               goalGlow = 0.0f;   // ゴールの灯りの強さ 0..1。印の箱に入ると上がる

	void init()
	{
		const lv::Entity* spawn = g_stage.get().find("PlayerSpawn");   // Blender で置いた出発点
		hero.position = (spawn != nullptr) ? at(spawn->position) : act::Vec3{0, 0, 0};
		hero.position.y += kHero.skinWidth;
		hero.grounded = 1;
	}

	void update(Input in, float dt)
	{
		// --watch-assets で起動していれば、Blender で書き出し直すたびに地形と印を読み直す
		if (g_stage.reloadIf(in.actionPayload("asset.reloaded")))
		{
			g_level = act::buildLevelCollision(g_stage.get());
			g_lamps = readLamps(g_stage.get());
		}
		const act::CollisionWorld world(&g_level);

		const Stick     m   = in.move();   // カメラから見た前後左右へ歩く (move の y は下が正)
		const act::Vec3 fwd = act::cameraRigForward(cam.yaw, 0.0f), right = act::cameraRigRight(fwd);
		const act::Vec3 run = (right * m.x - fwd * m.y) * kRunSpeed;
		if (act::lengthSq(run) > 0.01f) { facing = act::normalizeOr(run, facing); }
		const bool jump = hero.grounded != 0 && in.pressed(Key::Space);
		act::stepCharacter(hero, kHero, world, {run, jump ? kJumpSpeed : 0.0f}, dt);

		const float target = inGoal() ? 1.0f : 0.0f;   // 灯りは 0.5 秒ほどで点き、離れると消える
		goalGlow += (target - goalGlow) * ((dt * 6.0f > 1.0f) ? 1.0f : dt * 6.0f);

		act::CameraRigInput ci;
		ci.follow = hero.position;
		ci.orbitX = (in.down(Key::E) ? 1.0f : 0.0f) - (in.down(Key::Q) ? 1.0f : 0.0f) + in.rightStick().x;
		ci.orbitY = in.rightStick().y;
		view      = act::updateCameraRig(cam, kCam, ci, world, dt);
	}

	// Blender の trigger の印は、中心 (position) と箱の半分の大きさ (halfExtents) を持つ
	bool inGoal() const
	{
		const lv::Entity* goal = g_stage.get().find("Goal");
		if (goal == nullptr) { return false; }
		const act::Vec3 d = hero.position - at(goal->position);
		return std::fabs(d.x) <= goal->halfExtents[0] && std::fabs(d.y) <= goal->halfExtents[1] &&
		       std::fabs(d.z) <= goal->halfExtents[2];
	}

	// ゴールの箱の底に薄い板を敷いて場所を見せる。入っている間は桃色に寄る
	void drawGoal(Screen& s) const
	{
		const lv::Entity* goal = g_stage.get().find("Goal");
		if (goal == nullptr) { return; }
		const act::Vec3 c = at(goal->position), h = at(goal->halfExtents);
		const Color pad = Color{0.85f, 0.87f, 0.90f, 1.0f}.lerp(hex(0xE8338A), goalGlow);
		s.drawMesh("cube", {c.x, c.y - h.y + 0.02f, c.z}, {h.x * 2.0f, 0.04f, h.z * 2.0f}, {0, 0, 0}, pad);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xEAF1F8));
		s.camera3D(view.eye, view.target, view.up, Deg{view.fovDeg}, 0.1f, 60.0f);
		s.light3D({-0.5f, -1.0f, 0.35f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		render::SceneLook look;   // ステージ・キャラ・灯りの環境光を焼いた放射照度に置き換える。影は太陽の向き
		look.shadow = true;
		look.shadowDirection[0] = -0.5f; look.shadowDirection[1] = -1.0f; look.shadowDirection[2] = 0.35f;
		look.ambient[0] = look.ambient[1] = look.ambient[2] = 0.45f;   // 焼いた格子の外で使う平らな環境光
		look.indirect.flags = render::kIndirectGi;
		look.indirect.giIntensity = 0.7f;   // 日の当たる芝の照り返しで石の柱が緑に寄りすぎないよう、跳ね返りを少し弱める
		s.sceneLook3D(look);
		s.lightingBake3D(kBakePath);   // 毎フレーム呼んでよい。読み終わるまでは平らな環境光のまま
		s.drawModel(kStagePath, {0.0f, 0.0f, 0.0f});   // ステージの見た目 (当たり判定だけの面と印は描かれない)
		for (const Lamp& l : g_lamps)
		{
			const float power = l.goal ? 0.5f + 3.5f * goalGlow : 2.0f;   // ゴールの灯りは入るまで弱い
			const float size = 0.12f + 0.08f * power;
			s.drawMesh("sphere", l.pos, {size, size, size}, {0, 0, 0}, l.color);
			s.pointLight3D(l.pos, l.range, l.color, power);
		}
		drawGoal(s);
		const act::Vec3 p = hero.position;
		s.drawMesh("cube", p + act::Vec3{0, 0.8f, 0}, {0.6f, 1.6f, 0.6f}, {0, 0, 0}, hex(0xFF9500));
		s.drawMesh("cube", p + act::Vec3{0, 1.25f, 0} + facing * 0.32f, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, hex(0x1D1D1F));
		chapterTitle(s, "Blender のステージ");
		chapterControls(s, "矢印: あるく　Space: 跳ぶ　Q / E: カメラを回す");
	}
};

// 実行:  mitiru_host.exe level3d/level3d.dll   (--watch-assets level3d/assets を付けると書き出し直しを読む)
MITIRU_ASSERT_NO_PADDING(Level3D);
MITIRU_GAME(Level3D);
