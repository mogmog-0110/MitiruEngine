// model3d — 大きな 3D モデル (.clod) を drawModel で置く
// 実行すると: 13 万ポリゴンの火山島の上をゆっくり旋回しながら飛ぶ。近づくほど細かく、遠いほど粗く、切り替えは見えない
// 関連 API: drawModel / camera3D / light3D / skybox3D (自動 LOD の大規模モデル)

#include <cmath>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

// 地形は examples/model3d/tools/gen_terrain.py → clod_build で作った .clod。
// drawModel は初回に読み込み、以後は距離に応じた詳細度で毎フレーム描く。
struct Model3D
{
	float t = 0.0f;        // 周回の経過時間
	float altitude = 5.0f; // 飛行高度 (うえ・した で変更)

	void init()
	{
		t = 0.0f;
		altitude = 5.0f;
	}

	void update(Input in, float dt)
	{
		t += dt;
		constexpr float climb = 2.4f;
		if (in.down(Key::Up))   { altitude += climb * dt; }
		if (in.down(Key::Down)) { altitude -= climb * dt; }
		if (altitude < 0.8f)  { altitude = 0.8f; }
		if (altitude > 10.0f) { altitude = 10.0f; }
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xDCE9F5));   // 3D が使えない環境 (画面なしの自動テストなど) ではこの色のまま

		// 島の外周をゆっくり旋回するカメラ。山頂のやや上を見て海と空も入れる。
		const float yaw = t * 0.12f;
		const float r = 13.5f;
		s.camera3D({std::sin(yaw) * r, altitude, std::cos(yaw) * r},
		           {0.0f, 0.7f, 0.0f}, 50.0f);
		s.light3D({-0.55f, -0.8f, -0.35f}, hex(0xFFF7EA));
		s.skybox3D(hex(0x6FA8E4), hex(0xF2F6FA));

		// 大規模モデルは drawModel 1 行。詳細度 (LOD) は距離から自動で決まる。
		s.drawModel("model3d/assets/terrain.clod", {0.0f, 0.0f, 0.0f});

		chapterTitle(s, "3D モデル");
		chapterControls(s, "うえ・した: たかさ");
	}
};

// 実行:  mitiru_host.exe model3d/model3d.dll
MITIRU_GAME(Model3D);
