// fluid3d。「流れる体」章 (粒でできた体の輪郭が液体のように流れる)
// 実行すると: 緑の体が床に落ちて広がり、まとまる。矢印で歩くと手足が生え、止まると溶けて戻る。Space で柱へ仮足を伸ばし、掴んだら引き寄せる
// 関連 API: fluidReset / fluidStep (体を進める) / fluidPodShoot (仮足) / buildFluidMesh + updateMesh3D (前のフレームの位置つきで面を送る)

#include <cmath>
#include <mitiru.hpp>
#include <mitiru/softbody/FluidBody.hpp>
#include <mitiru/softbody/FluidMeshVertices.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace sb = mitiru::softbody;

const sb::FluidParams kFluid{};
const sb::Vec3 kPoleTop{3.8f, 1.7f, -1.8f};
const sb::Vec3 kPoleFoot{3.2f, 0.0f, -1.3f};   // 引き寄せたあと着く所
constexpr float kZipTime = 0.38f;               // 引き寄せにかける秒

// 体の作業領域と面の網は毎フレーム作り直すので、ゲームの全状態の外 (DLL の static) に置く
sb::FluidScratch g_scratch;
sb::FluidSurfaceScratch g_surface;
sb::FluidMesh g_mesh;
render::Vertex3D g_verts[sb::kFluidMeshMaxVertices];

struct Fluid3D
{
	sb::FluidBodyState body{};          // 粒の位置と速さ、仮足、手足、顔 (巻き戻しで丸ごと戻る)
	sb::Vec3 foot{0.0f, 0.0f, 0.0f};    // 芯の足元
	sb::Vec3 velocity{};
	sb::Vec3 facing{0.0f, 0.0f, 1.0f};
	sb::Vec3 zipFrom{};
	float phase = 0.0f;                 // 歩きの位相 (1 歩で 1 進む)
	float zipT = -1.0f;                 // 引き寄せの経過秒 (負なら引き寄せていない)

	// 休みの形の粒を並べ、1.2 m 上から落とす
	void init()
	{
		static sb::Vec3 rest[sb::kFluidMaxParticles];
		const int count = sb::fluidSampleRest(kFluid, rest, sb::kFluidMaxParticles);
		static_cast<Fluid3D&>(*this) = Fluid3D{};
		sb::fluidReset(body, kFluid, rest, count, pose(), g_scratch);
		for (int i = 0; i < body.count; ++i) { body.x[i].y += 1.2f; }
	}

	// 芯の姿勢: 歩きの位相で足と腕を交互に振り、進む向きへ少し倒れる
	sb::FluidPose pose() const
	{
		sb::FluidPose p;
		p.root = foot;
		p.forward = facing;
		const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
		const float swing = speed > 0.3f ? std::sin(3.14159265f * phase) : 0.0f;
		p.leanDeg = 3.0f * speed;
		p.bob = 0.03f * std::fabs(swing);
		p.feet[0] = {0.0f, 0.05f * std::fmax(swing, 0.0f), 0.12f * swing, 15.0f * swing};
		p.feet[1] = {0.0f, 0.05f * std::fmax(-swing, 0.0f), -0.12f * swing, -15.0f * swing};
		p.arms[0] = {-30.0f * swing, 0.0f};
		p.arms[1] = {30.0f * swing, 0.0f};
		return p;
	}

	// 矢印で歩く。速さは 0.15 秒ほどで目標へ寄せる
	void walk(Input in, float dt)
	{
		const sb::Vec3 want{(in.down(Key::Right) ? 2.5f : 0.0f) - (in.down(Key::Left) ? 2.5f : 0.0f), 0.0f,
		                    (in.down(Key::Down) ? 2.5f : 0.0f) - (in.down(Key::Up) ? 2.5f : 0.0f)};
		velocity += (want - velocity) * std::fmin(1.0f, dt / 0.15f);
		const float speed = sb::length(velocity);
		if (speed > 0.3f) { facing = velocity / speed; }
		phase += (1.5f + 0.9f * speed) * dt;
	}

	// 掴んでから kZipTime 秒で柱の足元へ弧を描いて飛び、着いたら仮足を戻す
	void zip(float dt)
	{
		const float u = zipT / kZipTime;
		const float ease = u * u * (3.0f - 2.0f * u);
		const sb::Vec3 next = zipFrom + (kPoleFoot - zipFrom) * ease + sb::Vec3{0.0f, 0.5f * std::sin(3.14159265f * u), 0.0f};
		velocity = (next - foot) / dt;
		foot = next;
		zipT += dt;
		if (zipT < kZipTime) { return; }
		zipT = -1.0f;
		velocity = {};
		sb::fluidPodSetState(body, 0, sb::FluidPodState::Release);
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		if (in.pressed(Key::Space) && body.pods[0].state == sb::FluidPodState::Off) { sb::fluidPodShoot(body, 0, kPoleTop); }
		if (zipT < 0.0f && body.pods[0].state == sb::FluidPodState::Grab)
		{
			zipT = 0.0f;
			zipFrom = foot;
		}
		if (zipT >= 0.0f) { zip(dt); }
		else
		{
			walk(in, dt);
			foot += velocity * dt;
		}
		const sb::FluidPlane floor{};
		sb::FluidStepInput step;
		step.pose = pose();
		step.coreVelocity = velocity;
		step.mode = zipT >= 0.0f ? sb::FluidMode::Air : sb::FluidMode::Ground;
		step.phase = phase;
		step.cadence = 1.5f + 0.9f * sb::length(velocity);
		step.planes = &floor;
		step.planeCount = 1;
		sb::fluidStep(body, kFluid, step, dt, g_scratch);
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({1.9f, 2.1f, 4.4f}, {1.9f, 0.6f, -0.7f}, Deg{48.0f});
		s.light3D({-0.4f, -0.9f, -0.5f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {30.0f, 1.0f, 30.0f}, {0, 0, 0}, hex(0xD6D0C6));
		s.drawMesh("cube", {kPoleTop.x, 0.85f, kPoleTop.z}, {0.12f, 1.7f, 0.12f}, {0, 0, 0}, hex(0x6B5A48));
		drawBody(s);
		chapterTitle(s, "流れる体");
		chapterControls(s, "矢印: あるく　Space: 柱へ仮足を伸ばす　R: もう一度落とす");
	}

	// 粒から面の網を作り、前のフレームの位置といっしょに送る。目は顔の点から顔の向きへ進んで面に出た所に置く
	void drawBody(Screen& s) const
	{
		sb::buildFluidMesh(body, kFluid, nullptr, 0, g_surface, g_mesh);
		sb::fluidMeshVertices(g_mesh, Color{0.47f, 0.72f, 0.24f, 1.0f}, g_verts);
		if (!s.hasMesh3D("blob"))
		{
			s.registerMesh3D("blob", g_verts, sb::kFluidMeshMaxVertices, g_mesh.indices, sb::kFluidMeshMaxIndices);
		}
		s.updateMesh3D("blob", g_verts, g_mesh.vertexCount, g_mesh.indices, g_mesh.indexCount, g_mesh.previousPositions);
		s.drawMesh("blob", {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, Color{1, 1, 1, 1});
		const sb::Vec3 right = body.faceUp.cross(body.faceForward);
		for (const float side : {-1.0f, 1.0f})
		{
			const sb::Vec3 eye = sb::fluidSurfaceAlong(g_surface, body.face + right * (0.11f * side), body.faceForward, 0.8f);
			s.drawMesh("sphere", eye, {0.07f, 0.09f, 0.05f}, {0, 0, 0}, theme::kInk);
		}
	}
};

MITIRU_ASSERT_NO_PADDING(Fluid3D);
MITIRU_REFLECT(Fluid3D, phase, zipT);
MITIRU_GAME(Fluid3D);
