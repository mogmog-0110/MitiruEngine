// face3d。顔の表情をモーフターゲット (形の差分) の重みで動かす
// 実行すると: 丸い顔が まばたきをして、Space で「こんにちは」の口の形を順に動かす。H で笑い、S で驚く
// 関連 API: Screen::drawModelPose (モーフの重み) / animation::ExpressionPose / evaluateMorphWeights / VisemeTrack

#include <cmath>
#include <mitiru.hpp>
#include <mitiru/animation/MorphExpression.hpp>
#include <mitiru/animation/AnimAssetLoad.hpp>
#include <mitiru/animation/VisemeTrack.hpp>
#include <mitiru/module/AutoReflect.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace anim = mitiru::animation;

// 顔の形はモデルの中に「口を開く」「目を閉じる」などの差分として入っていて、重みを 0..1 で渡すと足される。
// 表情の名前 (aa, blink, happy ...) とどの差分に当たるかは、隣の face.expressions.json に書く。
// 口の形の時刻表 hello.visemes.json は、音から前もって作った物 (tools/bake_visemes.py)。実行中に音は解析しない
constexpr const char* kFacePath = "face3d/assets/face/face.glb";
const anim::AnimAsset kFace = anim::loadAnimAssetFile(kFacePath).value_or(anim::AnimAsset{});
const anim::VisemeTrack kHello = [] {
	const auto bytes = vfs::readGlobal("face3d/assets/face/hello.visemes.json");
	const std::string text = bytes ? std::string(bytes->begin(), bytes->end()) : std::string{};
	return anim::parseVisemeTrack(text).value_or(anim::VisemeTrack{});
}();

struct Face3D
{
	float t = 0.0f;          // 経過秒。まばたきと、顔のゆれに使う
	float talk = -1.0f;      // 「こんにちは」を話し始めてからの秒。負なら話していない
	float happy = 0.0f;      // 笑い (0..1)。H を押している間に近づく
	float surprised = 0.0f;  // 驚き (0..1)。S を押している間に近づく

	void init() { *this = Face3D{}; }

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }
		t += dt;
		if (in.pressed(Key::Space)) { talk = 0.0f; }
		if (talk >= 0.0f) { talk += dt; if (talk > kHello.durationSec()) { talk = -1.0f; } }
		const float k = (dt * 8.0f > 1.0f) ? 1.0f : dt * 8.0f;   // 表情はなめらかに近づく
		happy += ((in.down(Key::H) ? 1.0f : 0.0f) - happy) * k;
		surprised += ((in.down(Key::S) ? 1.0f : 0.0f) - surprised) * k;
	}

	// 3 秒に 1 回、0.2 秒かけて目を閉じて開く
	float blinkWeight() const
	{
		const float phase = std::fmod(t, 3.0f);
		return phase < 0.2f ? 1.0f - std::fabs(phase - 0.1f) * 10.0f : 0.0f;
	}

	void draw(Screen& s) const
	{
		s.clear(theme::kPaper);
		s.camera3D({0.0f, 0.2f, 3.6f}, {0.0f, 0.0f, 0.0f}, Deg{40.0f});
		s.light3D({0.2f, -0.3f, -1.0f}, hex(0xFFF4E0));
		s.skybox3D(hex(0xCFE3F5), hex(0xF5F7FA));

		anim::ExpressionPose pose;   // 今の状態から、名前ごとの重みを決める
		(void)anim::setExpression(pose, "blink", blinkWeight());
		(void)anim::setExpression(pose, "happy", happy);
		(void)anim::setExpression(pose, "surprised", surprised);
		if (talk >= 0.0f) { anim::applyViseme(pose, kHello, talk); }

		// 表情の名前 → 差分ごとの重み。happy と surprised は、まばたきと口を打ち消す (face.expressions.json の override)
		float weights[anim::kMorphMaxSlots];
		const int count = anim::evaluateMorphWeights(kFace, nullptr, &pose, weights);

		const sgc::Mat4f world = sgc::Mat4f::translation({0.0f, 0.05f * std::sin(t * 1.5f), 0.0f}) *
		                         sgc::Mat4f::rotationY(deg(18.0f * std::sin(t * 0.5f)));
		s.drawModelPose(kFacePath, world, nullptr, nullptr, 0, render::DrawTint{}, weights, count);

		chapterTitle(s, "Face Morph");
		chapterControls(s, "Space: はなす　H: わらう　S: おどろく　Esc: おわる");
	}
};

MITIRU_REFLECT(Face3D, t, talk, happy, surprised);
MITIRU_ASSERT_NO_PADDING(Face3D);
MITIRU_GAME(Face3D);
