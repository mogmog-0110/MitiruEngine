// springbones3d。「揺れ物」章 (尻尾を骨のばねで揺らす)
// 実行すると: 狐が輪を描いて歩き、尻尾が曲がりと足取りに遅れて揺れる。Space で走る / 歩く、S で揺れを切って比べる
// 関連 API: loadSpringBones (<model>.springs.json) / stepSpringBones (update) / applySpringBones (draw) /
//           SpringBoneState (GameMemory に置くので揺れも巻き戻る) / drawModelNodeMatrices

#include <cmath>
#include <mitiru.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace anim = mitiru::animation;

constexpr const char* kPath = "springbones3d/assets/fox/fox.glb";
const anim::AnimAsset kFox = anim::loadAnimAssetFile(kPath).value_or(anim::AnimAsset{});
const anim::SpringBoneSet kTail = anim::loadSpringBones(kFox, kPath);   // 隣の fox.springs.json (尻尾の鎖と、腰と脚の当たり)
constexpr float kRadius = 1.3f;
anim::AnimPose g_pose;   // 姿勢の計算の置き場 (骨の数だけ使い回す)

struct SpringBones3D
{
	float t = 0.0f;
	float hx = 1.0f, hz = 0.0f;      // 歩く向き
	std::int32_t running = 0;
	std::int32_t springsOff = 0;
	anim::SpringBoneState tail{};    // 尻尾の先端の位置と前のフレームの位置

	float speed() const { return running != 0 ? 2.6f : 1.1f; }

	// 狐を置く行列: モデルの +z を歩く向きへ回し、輪の上に置く (cm のモデルを m へ)
	sgc::Mat4f place() const
	{
		sgc::Mat4f m = sgc::Mat4f::identity();
		m.m[0][0] = hz;  m.m[0][2] = hx;  m.m[2][0] = -hx;  m.m[2][2] = hz;
		m.m[0][3] = kRadius * hz;  m.m[2][3] = -kRadius * hx;
		return m * sgc::Mat4f::scaling({0.01f, 0.01f, 0.01f});
	}

	anim::AnimPoseParams gait() const
	{
		anim::AnimPoseParams p;
		p.flags = anim::kAnimPoseInPlace;
		anim::pushLayer(p, anim::AnimLayer::clipAt(kFox.findClip(running != 0 ? "Run" : "Walk"), t));
		return p;
	}

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::R)) { hud.requestRestart(); }
		if (in.pressed(Key::Space)) { running = 1 - running; }
		if (in.pressed(Key::S)) { springsOff = 1 - springsOff; anim::resetSpringBones(tail); }
		t += dt;
		// 輪の上で向きを a だけ回す。cos と sin は多項式で出す (CRT の三角関数は CPU で結果が変わる)
		const float a = speed() * dt / kRadius;
		const float c = 1.0f - a * a / 2.0f, s = a - a * a * a / 6.0f;
		const float x = hx * c - hz * s, z = hx * s + hz * c, inv = 1.0f / std::sqrt(x * x + z * z);
		hx = x * inv;
		hz = z * inv;
		if (springsOff == 0)   // 姿勢を出してから揺れを進める (当たり判定に使うならこの姿勢を使う)
		{
			anim::evaluatePose(kFox, gait(), g_pose);
			anim::stepSpringBones(kFox, kTail, place(), dt, tail, g_pose);
		}
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({0.0f, 1.3f, 3.6f}, {0.0f, 0.35f, 0.0f}, Deg{50.0f});
		s.light3D({-0.5f, -0.9f, -0.4f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x63A5E8), hex(0xEAF3FB));
		s.drawMesh("cube", {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f}, {0, 0, 0}, hex(0xBFC8D4));
		// update と同じ姿勢に、GameMemory の揺れを掛けて描く (スキニングの前に骨を回す)
		anim::evaluatePose(kFox, gait(), g_pose);
		if (springsOff == 0) { anim::applySpringBones(kFox, kTail, place(), tail, g_pose); }
		s.drawModelNodeMatrices(kPath, place(), g_pose.model.data(), static_cast<std::uint32_t>(g_pose.model.size()));
		chapterTitle(s, "揺れ物");
		chapterControls(s, springsOff != 0 ? "Space: 走る / 歩く　S: 揺れを戻す　R: さいしょから"
		                                   : "Space: 走る / 歩く　S: 揺れを切る　R: さいしょから");
	}
};

MITIRU_ASSERT_NO_PADDING(SpringBones3D);
MITIRU_REFLECT(SpringBones3D, t, running, springsOff);
MITIRU_GAME(SpringBones3D);
