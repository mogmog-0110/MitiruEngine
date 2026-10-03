// anim3d。キャラクターを歩かせる (骨格アニメーションと状態機械)
// 実行すると、草原にキツネが立つ。WASD で歩き、Shift を足すと走る。待機・歩き・走りの切り替えは
// 隣の fox.animgraph.json に書いてあり、キツネの頭は、まわりを飛ぶ光の玉をいつも目で追う (IK)
// 関連 API: AnimGraphFile / stepAnimGraph / animGraphPose / drawModelPose / AnimIkRequest / MITIRU_INSPECT_ASSETS

#include <cmath>
#include <mitiru.hpp>
#include <mitiru/animation/AnimRuntime.hpp>
#include <mitiru/module/ReflectEngineTypes.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;
namespace anim = mitiru::animation;

// キツネは Khronos サンプルの Fox (assets/fox/CREDITS.md、顔は +z 向き)。骨格とクリップは描画と同じファイルから
// この DLL でも読む。どの状態からどの状態へ、何秒かけて移るかは fox.animgraph.json に書く。
// mitiru_host に --watch-assets anim3d/assets を付けて起動すると、JSON を保存するたびに読み直し、遊びながら調整できる
constexpr const char* kFoxPath = "anim3d/assets/fox/fox.glb";
const anim::AnimAsset kFox = anim::loadAnimAssetFile(kFoxPath).value_or(anim::AnimAsset{});
anim::AnimGraphFile g_graph("anim3d/assets/fox/fox.animgraph.json", kFox);
const int kHead = kFox.findNode("b_Head_05");
// 頭の骨の「前」がどの軸かは骨ごとに違う。レスト姿勢で体の前 (+z) を向いている軸を、読み込みの時に求めておく
const sgc::Vec3f kHeadForward = anim::restLocalAxis(kFox, kHead, {0.0f, 0.0f, 1.0f});

struct Anim3D
{
	float px = 0.0f, pz = 0.0f;      // キツネの位置 (m)
	float yawDeg = 180.0f;           // 体の向き
	float speed = 0.0f;              // 進む速さ (m/s)。グラフの param "speed" に渡す
	float t = 0.0f;                  // 光の玉の時計 (秒)
	float camX = 0.0f, camZ = 0.0f;  // カメラの注視点 (少し遅れて追う)
	anim::AnimGraphState graph{};    // 状態機械の今 (どの状態か、何秒目か)。数字だけなので巻き戻しても戻る

	void init() { *this = Anim3D{}; }

	void update(Input in, Hud hud, float dt)
	{
		if (in.pressed(Key::Escape)) { hud.quit(); }   // Esc で終わる
		(void)g_graph.reloadIf(in.actionPayload("asset.reloaded"));

		// WASD (in.move() が WASD/矢印/スティックを合成)。カメラが斜め上から見るため、入力を世界の軸へ 45° 回す
		const Stick m = in.move();
		const bool moving = (m.x * m.x + m.y * m.y) > 0.01f;
		const float want = !moving ? 0.0f : in.down(Key::Shift) ? 3.6f : 1.4f;   // 歩き 1.4 m/s、走り 3.6 m/s
		const float step = 5.0f * dt;                                             // 1 秒に 5 m/s まで速さを変える
		speed += (want - speed > step) ? step : (want - speed < -step) ? -step : want - speed;
		if (moving)
		{
			// 進む方向へ体を回す。一瞬で向きを変えず、旋回は 1 秒に 540 度までにする
			constexpr float k = 0.70710678f, kRad2Deg = 180.0f / 3.14159265f;
			float d = std::atan2((m.x + m.y) * k, (-m.x + m.y) * k) * kRad2Deg - yawDeg;
			while (d > 180.0f) { d -= 360.0f; }
			while (d < -180.0f) { d += 360.0f; }
			const float turn = 540.0f * dt;
			yawDeg += (d > turn) ? turn : (d < -turn) ? -turn : d;
		}
		px += std::sin(deg(yawDeg)) * speed * dt;   // 体の向きへ進む
		pz += std::cos(deg(yawDeg)) * speed * dt;
		px = (px < -8.0f) ? -8.0f : (px > 8.0f) ? 8.0f : px;   // 草原の外へ出ない
		pz = (pz < -8.0f) ? -8.0f : (pz > 8.0f) ? 8.0f : pz;

		// 速さをグラフへ渡して進める。待機 ↔ 歩き ↔ 走りの切り替えと混ぜ方は JSON が決める
		const anim::AnimGraph& g = g_graph.get();
		anim::setAnimParam(graph, g.findParam("speed"), speed);
		anim::stepAnimGraph(g, kFox, graph, dt);

		t += dt;
		const float chase = (dt * 5.0f > 1.0f) ? 1.0f : dt * 5.0f;   // カメラはキツネを少し遅れて追う
		camX += (px - camX) * chase;
		camZ += (pz - camZ) * chase;
	}

	// 光の玉はキツネのまわりを 1 周 6 秒で回る
	Vec3 ball() const { return {px + 1.2f * std::sin(t), 0.9f, pz + 1.2f * std::cos(t)}; }

	void drawFox(Screen& s) const
	{
		anim::AnimPoseParams pose;   // 今の状態から「どのクリップを何秒で、どれだけ混ぜるか」を出す
		anim::animGraphPose(g_graph.get(), kFox, graph, pose);

		// 頭を玉へ向ける。目標はワールドの座標のまま渡し、首はいまの向きから 70 度 (cos で渡す) まで回す
		const Vec3 b = ball();
		const anim::AnimIkRequest look = anim::AnimIkRequest::aim(kHead, b, kHeadForward, std::cos(deg(70.0f)));

		const sgc::Mat4f world = sgc::Mat4f::translation({px, 0.0f, pz}) *
		                         sgc::Mat4f::rotationY(deg(yawDeg)) * sgc::Mat4f::scaling({0.01f, 0.01f, 0.01f});
		s.drawModelPose(kFoxPath, world, &pose, &look, 1);
		s.drawMesh("sphere", b, {0.12f, 0.12f, 0.12f}, {0, 0, 0}, hex(0xFFF2A8));
		s.pointLight3D(b, 2.5f, hex(0xFFE27A), 1.2f);   // 玉は小さな灯り
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xDCE9F5));   // 3D が使えない環境 (画面なしの自動テストなど) ではこの色のまま

		s.camera3D({camX + 2.8f, 2.0f, camZ + 2.8f}, {camX, 0.5f, camZ}, 50.0f);
		s.light3D({0.4f, -0.8f, 0.35f}, hex(0xFFF4E0));
		s.skybox3D(hex(0x6FA8E4), hex(0xF2F6FA));

		// 草原 (薄い箱で、上面が y=0。影を受ける面) と、移動が分かる目印の岩
		s.drawMesh("cube", {0.0f, -0.15f, 0.0f}, {20.0f, 0.3f, 20.0f}, {0, 0, 0}, hex(0x9CC69B));
		struct Rock { float x, z, r; };
		static constexpr Rock kRocks[] = {{-5.5f, -3.0f, 0.7f}, {4.0f, -6.0f, 0.5f}, {6.5f, 2.5f, 0.9f},   {-3.0f, 5.5f, 0.6f},
		                                  {1.5f, 7.0f, 0.4f},   {-7.0f, 1.0f, 0.5f}, {2.5f, -2.0f, 0.35f}, {-1.5f, -6.5f, 0.55f}};
		for (const auto& r : kRocks)
		{
			s.drawMesh("cube", {r.x, r.r * 0.35f, r.z}, {r.r, r.r * 0.7f, r.r}, {0.0f, 25.0f, 0.0f}, hex(0x8FA08F));
		}
		drawFox(s);

		chapterTitle(s, "3D Character");
		if (!g_graph.errors().empty())   // JSON を書き換えて誤りがあれば、前のグラフのまま動き、誤りを出す
		{
			const Rect bar{16.0f, 60.0f, 1248.0f, 30.0f};
			s.drawRect(bar, rgba(229, 72, 77, 225));
			s.drawTextInRect(bar, g_graph.errors().front().c_str(), hex(0xFFFFFF), 16.0f, Screen::TextAlignH::Left,
			                 Screen::TextAlignV::Middle);
		}
		chapterControls(s, "WASD: あるかせる　Shift: はしる　Esc: おわる");
	}
};

// 状態機械の名前の表をツール窓へ渡す。mitiru_host anim3d/anim3d.dll --inspect anim で、今の状態と param が名前で出る
std::int32_t inspectAssets(module::InspectAsset* out, std::int32_t cap)
{
	(void)g_graph.get();   // まだ読んでいなければここで読む
	const std::string& names = g_graph.description();
	if (cap < 1 || names.empty()) { return 0; }
	out[0] = {"animgraph", "fox", names.data(), names.size()};
	return 1;
}
MITIRU_INSPECT_ASSETS(inspectAssets);

// 実行:  mitiru_host.exe anim3d/anim3d.dll   (--watch-assets anim3d/assets を付けると JSON の書き換えを読む)
// graph はエンジンの型のまま載せる。anim の窓と分岐エディタが名前で読む
MITIRU_REFLECT(Anim3D, px, pz, yawDeg, speed, t, camX, camZ, graph);
MITIRU_ASSERT_NO_PADDING(Anim3D);
MITIRU_GAME(Anim3D);
