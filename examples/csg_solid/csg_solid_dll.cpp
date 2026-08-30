// csg_solid。「Makina で作った立体を、ゲームの世界に置く」章
//   立体は姉妹プロジェクト Makina (CSG モデラー) で作り、makina_bake が DXIL に焼いたもの。
//   メッシュを経由しない。距離場をそのままレイマーチするので、モデラーで見ていた形が
//   そのままゲームに出る。assets/ の .csgbake.json がその焼き上がりである。
// あそびかた: 矢印キーで赤い立方体を動かす。中央のフランジと左の腕が Makina 製。
//   腕は肘にキーを打ってあり、2 秒で曲げ伸ばしを繰り返す (D-15、live に焼いた立体)。
// この章で使う関数: drawSolid (焼いた CSG を置く) / drawMesh / camera3D / light3D / skybox3D

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"  // 章ラベル + 操作帯 (全章共通の書式)

using namespace mitiru;

// 状態は数値だけ。毎回同じ結果になるので、ホットリロードも記録／再生もそのまま使える。
struct CsgSolidChapter
{
	float t = 0.0f;                    // カメラの周回に使う
	float cubeX = 3.0f, cubeZ = 3.0f;  // 主役の立方体の位置

	void init() { t = 0.0f; cubeX = 3.0f; cubeZ = 3.0f; }

	void update(Input in, float dt)
	{
		t += dt;
		constexpr float speed = 4.5f, limit = 5.5f;
		if (in.down(Key::Left))  { cubeX -= speed * dt; }
		if (in.down(Key::Right)) { cubeX += speed * dt; }
		if (in.down(Key::Up))    { cubeZ -= speed * dt; }
		if (in.down(Key::Down))  { cubeZ += speed * dt; }
		cubeX = std::clamp(cubeX, -limit, limit);
		cubeZ = std::clamp(cubeZ, -limit, limit);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xEAF1F8));   // 3D が使えない環境ではこの色のまま (白っぽい色)

		const float yaw = t * 0.25f;
		s.camera3D({std::sin(yaw) * 11.0f, 6.5f, std::cos(yaw) * 11.0f}, {0.0f, 1.0f, 0.0f},
		           50.0f);
		s.light3D({-0.6f, -0.95f, -0.45f}, hex(0xFFFBF2));
		s.skybox3D(hex(0x9EC8F0), hex(0xF2EEE4));

		// 床と、プレイヤーの立方体。どちらもエンジンの組み込みメッシュである。
		// 床は plane。かつてここで描画されず薄い立方体で代用していたが、原因は
		// createPlane の巻き順が宣言法線と逆で背面カリングに食われていたこと
		// (plane_probe 章が再現と修正の証拠を撮る)。
		s.drawMesh("plane", {0.0f, 0.0f, 0.0f}, {14.0f, 1.0f, 14.0f}, {}, hex(0x8FA089));
		s.drawMesh("cube", {cubeX, 0.5f, cubeZ}, {1.0f, 1.0f, 1.0f}, {}, hex(0xC04434));

		// そして Makina の立体。ここだけが距離場で、それでも同じ深度に混ざる。
		// 立方体をフランジの向こうへ動かすと、正しく隠れる。
		// パスは章フォルダ名から。bake は DXIL を隣のファイルから読むので vfs ではなく
		// 素のファイルパスであり、ホストの cwd (mitiru_host の隣) から解決される。
		s.drawSolid("csg_solid/assets/hero_flange.csgbake.json", {0.0f, 0.0f, 0.0f}, t * 12.0f,
		            0.5f);

		// 動く立体 (Makina PLAN.md D-15)。肘に 0 -> -90 -> 0 の 3 キーを打った腕を
		// `makina_bake --live` で焼いてある: 葉の数値を毎フレーム載せ替えるので、
		// 関節が動いてもシェーダは一つのまま。時刻は drawModel のクリップと同じ流儀で
		// 自分の t を渡し、モーションの長さ (2 秒) で折り返す。
		s.drawSolid("csg_solid/assets/arm.csgbake.json", {-3.5f, 1.2f, 0.0f}, 0.0f, 1.0f,
		            std::fmod(t, 2.0f));

		// 2D の HUD は 3D の絵の上に重ねて描かれる。
		chapterTitle(s, "Makina CSG");
		chapterControls(s, "矢印: 立方体をうごかす");
	}
};

// 実行:  mitiru_host.exe csg_solid/csg_solid.dll
MITIRU_GAME(CsgSolidChapter);
