// model3d — 大きな 3D モデル (.clod) を drawModel で並べる
// 実行すると: 島が地平線まで続く海の上を飛ぶ。合計 5 千万ポリゴン級でも、遠い島ほど自動で粗くなるので軽い
// 関連 API: drawModel / camera3D / light3D / skybox3D (自動 LOD の大規模モデル)

#include <cmath>
#include <mitiru.hpp>
#include "../common/chapter_hud.hpp"

using namespace mitiru;

// タイル 1 枚 = 16x16 の島モデル (10 万ポリ、examples/model3d/tools/gen_terrain.py で生成)。
// 毎フレーム 700 枚以上を drawModel で敷き詰める。詳細度 (LOD) は距離から自動で決まる。
struct Model3D
{
	float t = 0.0f;        // 飛行の経過時間
	float altitude = 7.0f; // 飛行高度 (うえ・した で変更)

	void init()
	{
		t = 0.0f;
		altitude = 7.0f;
	}

	void update(Input in, float dt)
	{
		t += dt;
		constexpr float climb = 4.0f;
		if (in.down(Key::Up))   { altitude += climb * dt; }
		if (in.down(Key::Down)) { altitude -= climb * dt; }
		if (altitude < 1.6f)  { altitude = 1.6f; }
		if (altitude > 24.0f) { altitude = 24.0f; }
	}

	// タイル番号から見た目を決める (乱数を使わず毎回同じ景色になる)
	static unsigned cellHash(int x, int z)
	{
		unsigned h = static_cast<unsigned>(x) * 374761393u
		           + static_cast<unsigned>(z) * 668265263u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return h ^ (h >> 16);
	}

	void draw(Screen& s) const
	{
		s.clear(hex(0xDCE9F5));   // 3D が使えない環境 (画面なしの自動テストなど) ではこの色のまま

		// まっすぐ北へ飛ぶカメラ。やや下を向いて、島の列と海と空を入れる
		const float camZ = t * 4.0f;
		s.camera3D({0.0f, altitude, camZ},
		           {0.0f, altitude * 0.45f, camZ + 18.0f}, 58.0f);
		s.light3D({-0.55f, -0.8f, -0.35f}, hex(0xFFF7EA));
		s.skybox3D(hex(0x6FA8E4), hex(0xC9DCEB));

		// カメラの周りにタイルを隙間なく敷き詰める。飛んだ分だけ前方に増える
		constexpr float spacing = 16.0f;   // タイル一辺
		const int baseZ = static_cast<int>(std::floor(camZ / spacing));
		for (int gz = -1; gz <= 26; ++gz)
		{
			for (int gx = -13; gx <= 13; ++gx)
			{
				const unsigned h = cellHash(gx, baseZ + gz);
				const float rot = static_cast<float>(h % 4u) * 90.0f;  // 4 方位のどれか
				const char* tile = "model3d/assets/sea.clod";          // 3 割は島なしの海
				if (h % 10u >= 3u)
				{
					tile = (h & 16u) ? "model3d/assets/island_a.clod"
					                 : "model3d/assets/island_b.clod";
				}
				s.drawModel(tile, {gx * spacing, 0.0f, (baseZ + gz) * spacing}, rot);
			}
		}

		chapterTitle(s, "3D Model");
		chapterControls(s, "うえ・した: たかさ");
	}
};

// 実行:  mitiru_host.exe model3d/model3d.dll
MITIRU_GAME(Model3D);
