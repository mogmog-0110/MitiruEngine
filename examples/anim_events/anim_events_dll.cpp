// anim_events。アニメーションに合わせてゲームが動く (ソケット・イベント・ルートモーション)
// 実行すると: 枝をくわえたキツネが草原を歩く。前足が地面に着くたびに足音が鳴って足跡が残り、進む距離は歩きの動きが決める。
// 関連 API: rootMotionDelta / collectAnimEvents / socketWorld / boneWorld / evaluatePose / drawModelPose (kAnimPoseInPlace)

#include <cstdint>
#include <mitiru.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace anim = mitiru::animation;

// キツネは anim3d と同じ Khronos の Fox に、歩きで前へ進む動き (ルートモーション) を焼き込んだもの (tools/bake_root_motion.py)。
// 隣の fox.anim.json に、前足が着く時刻 (イベント step_l / step_r)、口の位置 (ソケット mouth)、進む骨を書いてある。
// 描画 (host) もこの DLL も同じ 2 つのファイルから読むので、画面の骨とゲームが使う骨は一致する
constexpr const char* kFoxPath = "anim_events/assets/fox/fox.glb";
const anim::AnimAsset kFox = anim::loadAnimAssetFile(kFoxPath).value_or(anim::AnimAsset{});
const int kSurvey = kFox.findClip("Survey"), kWalk = kFox.findClip("Walk");
const int kMouth = kFox.findSocket("mouth");
const int kStepL = kFox.findEventName("step_l");
const int kLeftPaw = kFox.findNode("b_LeftHand_011"), kRightPaw = kFox.findNode("b_RightHand_08");
constexpr float kScale = 0.01f;   // モデルは cm で作られているので m にする

// 向きの回転は四元数のまま持つ。sin や atan2 を使わないので、どの PC で再生しても同じ足跡になる
constexpr sgc::Vec4f kTurnLeft{0.0f, 0.0174524f, 0.0f, 0.9998477f};   // Y まわりに 1 回 2 度
constexpr sgc::Vec4f kStartFacing{0.0f, 0.9238795f, 0.0f, 0.3826834f}; // 画面の右へ歩く向き (135 度)

anim::AnimPose g_pose;   // 姿勢の計算の置き場。中身は毎回すべて上書きするので、ゲームの状態には入れない

struct Footprint { sgc::Vec3f pos; float age; };
constexpr int kPrints = 12;

struct AnimEvents
{
	anim::YawXform body{{0.0f, 0.0f, 0.0f}, kStartFacing};   // キツネの位置 (m) と向き
	float animT = 0.0f;       // アニメ時間 (秒)。折り返さずに足し続ける (イベントとルートモーションが周回をまたげる)
	float walkMix = 0.0f;     // 0 = 待機だけ、1 = 歩きだけ
	float camX = 0.0f, camZ = 0.0f;
	Footprint prints[kPrints]{};
	std::uint32_t nextPrint = 0;
	sgc::Mat4f stick{};       // 口のソケットの行列 (枝を描く位置と向き)

	anim::AnimPoseParams pose() const
	{
		anim::AnimPoseParams p;
		p.flags = anim::kAnimPoseInPlace;   // 描画では歩きで進む分を止め、進む分はゲームが body に足す
		anim::pushLayer(p, anim::AnimLayer::clipAt(kSurvey, animT));
		anim::pushLayer(p, anim::AnimLayer::clipAt(kWalk, animT, walkMix));
		return p;
	}
	sgc::Mat4f world() const { return anim::localMatrix({body.t, body.r, {kScale, kScale, kScale}}); }

	void update(Input in, Hud hud, float dt)
	{
		const Stick m = in.move();
		if (m.x < -0.3f) { body.r = anim::quatMul(body.r, kTurnLeft); }               // 左右で向きを変える
		if (m.x > 0.3f)  { body.r = anim::quatMul(body.r, anim::quatConj(kTurnLeft)); }
		const float target = (m.y < -0.3f) ? 1.0f : 0.0f;                              // 上で歩く
		const float diff = target - walkMix, most = dt * 4.0f;                         // 0.25 秒で切り替える
		walkMix += (diff > most) ? most : (diff < -most) ? -most : diff;

		const float prevT = animT;
		animT += dt;

		// ルートモーション: この間に歩きの動きが進んだ量を、混ぜた重さの分だけ体に足す
		const anim::YawXform step = anim::rootMotionDelta(kFox, kWalk, prevT, animT, true);
		body = anim::composeYaw(body, {step.t * (kScale * walkMix), anim::quatNlerp(anim::kQuatIdentity, step.r, walkMix)});

		// この DLL の中で描画と同じ姿勢を出し、口のソケットと前足の骨を同じフレームのうちに読む
		anim::evaluatePose(kFox, pose(), g_pose);
		stick = anim::socketWorld(kFox, g_pose, kMouth, world());

		// イベント: 前の時刻から今の時刻までに前足が着いたら、足音を鳴らしてその足の下に足跡を残す
		anim::AnimEventHit hits[4];
		const int n = (walkMix > 0.5f) ? anim::collectAnimEvents(kFox, kWalk, prevT, animT, true, hits, 4) : 0;
		for (int i = 0; i < n; ++i)
		{
			const int paw = (hits[i].nameId == kStepL) ? kLeftPaw : kRightPaw;
			const sgc::Mat4f foot = anim::boneWorld(g_pose, paw, world());
			prints[nextPrint % kPrints] = {{foot.m[0][3], 0.005f, foot.m[2][3]}, 0.0f};
			++nextPrint;
			hud.play("step", 0.6f * walkMix);
		}
		for (Footprint& f : prints) { f.age += dt; }

		const float chase = (dt * 4.0f > 1.0f) ? 1.0f : dt * 4.0f;   // カメラはキツネを少し遅れて追う
		camX += (body.t.x - camX) * chase;
		camZ += (body.t.z - camZ) * chase;
	}

	void drawPrints(Screen& s) const
	{
		const Color grass = hex(0x9CC69B), mud = hex(0x3A2C1E);
		for (std::uint32_t i = 0; i < kPrints && i < nextPrint; ++i)
		{
			const float fade = (prints[i].age > 6.0f) ? 1.0f : prints[i].age / 6.0f;   // 6 秒かけて草の色に戻る
			s.drawMesh("cube", prints[i].pos, {0.07f, 0.01f, 0.09f}, {0, 0, 0}, mud.lerp(grass, fade));
		}
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xDCE9F5));
		s.camera3D({camX + 2.6f, 1.6f, camZ + 2.6f}, {camX, 0.4f, camZ}, Deg{45.0f});
		s.light3D({0.4f, -0.8f, 0.35f}, hex(0xFFF4E0));
		s.skybox3D(hex(0x6FA8E4), hex(0xF2F6FA));
		s.drawMesh("cube", {0.0f, -0.15f, 0.0f}, {40.0f, 0.3f, 40.0f}, {0, 0, 0}, hex(0x9CC69B));
		for (int i = -4; i <= 4; ++i)   // 進んだ距離が分かる目印の石
		{
			const float x = static_cast<float>(i) * 3.0f;
			s.drawMesh("cube", {x, 0.12f, -1.5f + 0.7f * static_cast<float>(i % 2)}, {0.4f, 0.25f, 0.4f}, {0, 30, 0}, hex(0x8FA08F));
		}
		drawPrints(s);

		const anim::AnimPoseParams p = pose();
		s.drawModelPose(kFoxPath, world(), &p);
		// 枝はソケットの行列に、枝の長さと太さ (モデルの単位の cm) を掛けて置く。横向きにくわえる
		const render::MeshInstance branch = render::makeInstance(stick * sgc::Mat4f::scaling({70.0f, 3.0f, 3.0f}));
		s.drawMeshInstanced("cube", &branch, 1, hex(0x8B5A2B));

		chapterTitle(s, "アニメに合わせて動く");
		chapterControls(s, "上: あるく　左右: 向きを変える");
	}
};

// 実行:  mitiru_host.exe anim_events/anim_events.dll
MITIRU_ASSERT_NO_PADDING(AnimEvents);
MITIRU_GAME(AnimEvents);
