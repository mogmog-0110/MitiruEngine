// cutscene3d。カットシーンと会話とクエストを、テキストエディタで書いたファイルから動かす
// 実行すると: 暗転が明けてカメラが村の上から門へ降り、狐が門から歩いて来る (字幕付き)。狐と話して頼みを受けると、右上にクエストが出る。
// この章で使う関数: mitiru/narrative/ の playSequence・evaluateCamera・evaluateActor・evaluateClips (カットシーン)、
//   Talk (会話。画面は同梱の mitiru:talk.rml)、updateQuests (クエスト)、pushSequence などの UI へ写す関数、
//   StageLoader (狐のモデルを読み終わるまで同梱の mitiru:loading.rml を出し、カットシーンを始めない)。
//   台本は assets/story/ の intro.seq.json (カットシーン)、gate.talk (会話)、quests.json (クエスト)、strings.json (英語の訳)。
//   GameMemory に置くのは再生の時刻・会話の位置・旗・クエストの進み具合だけなので、巻き戻すとカットシーンの途中にも戻れる。

#include <cmath>
#include <cstdint>
#include <mitiru.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include <mitiru/module/StageLoader.hpp>
#include <mitiru/narrative/NarrativeFile.hpp>
#include <mitiru/narrative/NarrativeHud.hpp>
#include <mitiru/narrative/Talk.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace nr = mitiru::narrative;
namespace anim = mitiru::animation;

// 台本はゲームの状態ではなく読み取り専用のデータ。--watch-assets で書き換えを検知したら読み直す
nr::NarrativeFile<nr::Sequence>       g_intro("cutscene3d/assets/story/intro.seq.json", &nr::loadSequenceFile);
nr::NarrativeFile<nr::DialogueScript> g_gate("cutscene3d/assets/story/gate.talk", &nr::loadDialogueFile);
nr::NarrativeFile<nr::QuestTable>     g_quests("cutscene3d/assets/story/quests.json", &nr::loadQuestFile);
const LocalizationManager g_strings = [] { LocalizationManager l; l.loadTranslations("cutscene3d/assets/story/strings.json"); return l; }();

constexpr const char* kFoxPath = "cutscene3d/assets/fox/fox.glb";
constexpr const char* kStage[] = {kFoxPath};
const anim::AnimAsset kFox = anim::loadAnimAssetFile(kFoxPath).value_or(anim::AnimAsset{});
const int kSurvey = kFox.findClip("Survey");
const nr::CameraShot kGameCamera{{2.6f, 2.3f, -8.8f}, {0.0f, 1.0f, 1.0f}};   // 会話と自由時間のカメラ (主人公の肩越し)

struct Block { Vec3 center, size; std::uint32_t col; };
constexpr Block kVillage[] = {
	{{0, -0.15f, 4}, {30, 0.3f, 34}, 0x9CC69B},                                             // 地面
	{{-1.6f, 1.6f, 2}, {0.35f, 3.2f, 0.35f}, 0xC2412D}, {{1.6f, 1.6f, 2}, {0.35f, 3.2f, 0.35f}, 0xC2412D},   // 門の柱
	{{0, 3.25f, 2}, {4.6f, 0.3f, 0.5f}, 0xC2412D}, {{0, 2.7f, 2}, {3.4f, 0.18f, 0.3f}, 0xC2412D},         // 門の笠木と貫
	{{-5, 1.2f, 8}, {3.6f, 2.4f, 3.2f}, 0xE4D3B4}, {{5, 1.2f, 9}, {3.2f, 2.4f, 3.6f}, 0xE4D3B4},           // 家
	{{-5, 2.7f, 8}, {4.0f, 0.6f, 3.6f}, 0x6B4F3A}, {{5, 2.7f, 9}, {3.6f, 0.6f, 4.0f}, 0x6B4F3A},           // 屋根
	{{0, 0.6f, -4}, {0.6f, 1.2f, 0.6f}, 0xFF9500}, {{0, 1.45f, -4}, {0.45f, 0.45f, 0.45f}, 0xFFD9A8},     // 主人公
};
constexpr Vec3 kLantern{2.4f, 1.3f, 2.6f};

struct Cutscene3D
{
	nr::SequencePlayback cut{};
	nr::Talk             talk{};
	nr::NarrativeVars    vars{};
	nr::QuestLog         quests{};
	StageLoader          loader{};
	float                clock = 0.0f;   // カットシーンの外で狐が待機する時刻

	void init() { nr::playSequence(cut, g_intro.get()); }

	void update(Input in, Hud hud, float dt)
	{
		const char* changed = in.actionPayload("asset.reloaded");
		g_intro.reloadIf(changed); g_gate.reloadIf(changed); g_quests.reloadIf(changed);
		g_intro.localize(g_strings, in.language()); g_gate.localize(g_strings, in.language()); g_quests.localize(g_strings, in.language());
		if (!loader.update(in, hud, kStage)) { return; }
		const nr::Sequence& seq = g_intro.get();
		const nr::DialogueScript& gate = g_gate.get();
		const auto onCue = [&](const nr::Cue& c) {
			if (!nr::playCue(c, hud) && c.name == "start_talk") { talk.start(hud, gate, "門", &vars); }
		};
		clock += dt;
		if (cut.showing(seq)) { playCutscene(in, hud, seq, dt, onCue); }
		else if (!talk.active(gate) && in.confirmPressed()) { talk.start(hud, gate, "門", &vars); }
		else if (!talk.active(gate) && in.pressed(Key::R)) { nr::playSequence(cut, seq); }
		talk.update(in, hud, gate, &vars);   // 台詞を送り、選択肢を選び、view.talk_* を送る
		nr::updateQuests(quests, g_quests.get(), vars, [&](const nr::QuestDef&, nr::QuestEvent e, int) {
			hud.mark(e == nr::QuestEvent::Started ? "quest.start" : "quest.update");
		});
		nr::pushSequence(hud, cut, seq);
		nr::pushQuests(hud, quests, g_quests.get());
	}

	template <class OnCue>
	void playCutscene(Input in, Hud hud, const nr::Sequence& seq, float dt, OnCue& onCue)
	{
		if (in.confirmPressed()) { nr::skipSequence(cut, seq, onCue); hud.cameraCut(); return; }
		const float before = cut.time;
		nr::advanceSequence(cut, seq, dt, onCue);
		if (nr::cameraCutBetween(seq, before, cut.time)) { hud.cameraCut(); }   // 混ぜずに替わったら前の絵を引きずらない
	}

	// 狐の位置・向き・アニメは、カットシーンの時刻から毎回求める (時刻が同じなら同じ姿)
	void drawFox(Screen& s, const nr::Sequence& seq) const
	{
		const int fox = seq.findActor("fox");
		const nr::ActorPose pose = fox >= 0 ? nr::evaluateActor(seq, fox, cut.time) : nr::ActorPose{};
		anim::AnimPoseParams p;
		nr::ClipSample clips[4];
		const int n = (fox >= 0 && cut.showing(seq)) ? nr::evaluateClips(seq, fox, cut.time, clips, 4) : 0;
		for (int i = 0; i < n; ++i) { anim::pushLayer(p, anim::AnimLayer::clipAt(kFox.findClip(clips[i].clip->clip), clips[i].time, clips[i].weight)); }
		if (n == 0) { anim::pushLayer(p, anim::AnimLayer::clipAt(kSurvey, clock)); }
		const float half = pose.yawDeg * 0.5f * 3.14159265f / 180.0f;   // 上まわりの回転の四元数 (0 で +Z を向く)
		const sgc::Mat4f world = anim::localMatrix({pose.position, {0.0f, std::sin(half), 0.0f, std::cos(half)}, {0.01f, 0.01f, 0.01f}});
		s.drawModelPose(kFoxPath, world, &p);
	}

	// 門の灯籠から舞う火の粉。合図の時刻からの経過秒で描くので、巻き戻してもその時刻の姿になる
	void drawSparkles(Screen& s, const nr::Sequence& seq) const
	{
		nr::forEachActiveCue(seq, cut.time, [&](const nr::Cue&, float age) {
			render::ParticleEmitterDesc fx;
			fx.key = 2; fx.seed = 7; fx.age = age; fx.burst = 48;
			fx.position[0] = kLantern.x; fx.position[1] = kLantern.y + 0.3f; fx.position[2] = kLantern.z;
			fx.spreadDeg = 60.0f; fx.speedMin = 0.6f; fx.speedMax = 1.8f; fx.lifeMin = 0.6f; fx.lifeMax = 1.4f; fx.gravity[1] = 0.4f;
			fx.size[0] = 0.06f; fx.size[1] = 0.1f; fx.size[2] = 0.02f;
			for (auto& c : fx.color) { c[0] = 1.0f; c[1] = 0.7f; c[2] = 0.25f; c[3] = 1.0f; }
			fx.color[2][3] = 0.0f;
			s.particles3D(&fx, 1);
		});
	}

	// 上下の黒い帯と暗転も時刻から描く (host の letterbox / fade の intent は遷移を host が持ち、巻き戻しに戻らない)
	void drawFrame(Screen& s, const nr::Sequence& seq) const
	{
		const float bar = (cut.sequence == seq.id ? nr::letterboxAt(seq, cut.time) : 0.0f) * 86.0f;
		if (bar > 0.5f) { s.drawRect(Rect{0, 0, 1280, bar}, hex(0x05070C)); s.drawRect(Rect{0, 720 - bar, 1280, bar}, hex(0x05070C)); }
		const float fade = cut.sequence == seq.id ? nr::fadeAt(seq, cut.time) : 0.0f;
		if (fade > 0.0f) { s.drawRect(Rect{0, 0, 1280, 720}, hex(0x05070C).withAlpha(fade)); }
	}

	void draw(Screen& s) const
	{
		const nr::Sequence& seq = g_intro.get();
		const nr::CameraShot cam = cut.sequence == seq.id ? nr::evaluateCamera(seq, cut.time, kGameCamera) : kGameCamera;
		s.clear(hex(0xF6D9B8));
		if (!loader.ready()) { return; }
		s.camera3D(cam.eye, cam.target, cam.up, Deg{cam.fovDeg}, cam.nearDist, cam.farDist);
		s.light3D({-0.6f, -0.7f, 0.4f}, hex(0xFFE2B8));
		s.skybox3D(hex(0xE59A6B), hex(0xF8E3C4));
		for (const Block& b : kVillage) { s.drawMesh("cube", b.center, b.size, {0, 0, 0}, hex(b.col)); }
		s.drawMesh("cube", kLantern, {0.3f, 0.45f, 0.3f}, {0, 0, 0}, hex(0xFFC46B));
		s.pointLight3D(kLantern, 4.0f, hex(0xFFB357), 2.5f);
		drawFox(s, seq);
		drawSparkles(s, seq);
		drawFrame(s, seq);
		chapterTitle(s, "カットシーンと会話");
		chapterControls(s, cut.showing(seq) ? "Enter: とばす" : "Enter: 話す・進める　上下: 選ぶ　R: カットシーンをもう一度");
	}
};

// 実行:  mitiru_host.exe cutscene3d/cutscene3d.dll
MITIRU_ASSERT_NO_PADDING(Cutscene3D);
MITIRU_GAME(Cutscene3D);
