// tiled_map。Tiled (.tmj) のマップを読み、Canvas::drawTiles で 1 つの描画命令として描く章。
// 実行すると、200 x 100 マス (2 万マス) の草原が出る。矢印キーで見える範囲を動かし、Space で全体の表示に切り替える。
// 使う機能: render::TiledMapLoader (.tmj の読み込み) / Canvas::drawTiles (見えている範囲だけを 1 命令で積む)
//          / in.move() / in.pressed()
//
// 2 万マスは、1 フレームの描画命令の上限 (4096) を越える。drawTiles はマスを 1 つの命令にまとめて積むので、上限に当たらない。
// 全体の表示 (Space) では 2 万マスをそのまま 1 命令で描く。

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <mitiru.hpp>
#include <mitiru/module/AutoReflect.hpp>   // MITIRU_REFLECT_AUTO (状態を inspector に出す)
#include <mitiru/asset/AssetPack.hpp>        // vfs::readGlobal (pack でも disk でも同じ呼び方で読む)
#include <mitiru/render/TiledMapLoader.hpp>
#include "../common/chapter_hud.hpp"         // 章の名前ラベル (左上) と操作バー (下端) の共通ヘルパー

using namespace mitiru;

namespace
{

// ゲームの状態 struct はポインタを持てないので、読み込んだマップはこのファイルの static に、開始時に 1 度だけ持つ。
// 読み込みに失敗したときは空のまま (何も描かれないだけで落ちない)。
struct LoadedStage
{
	int              width  = 0;
	int              height = 0;
	std::vector<int> indices;                      // row-major。-1 = 空。Tiled の gid から tileset の firstgid を引いた値
	render::TileAtlas atlas{16, 16, 4};
	float            startX = 0.0f, startY = 0.0f;  // "markers" レイヤーの先頭のオブジェクトの位置 (ピクセル)
};

LoadedStage loadStage()
{
	LoadedStage out;
	const auto bytes = vfs::readGlobal("tiled_map/assets/maps/stage.tmj");
	if (!bytes) { return out; }
	const std::string json(bytes->begin(), bytes->end());
	const auto result = render::TiledMapLoader::loadTiledMap(json);
	if (!result || result->tilemap.layers.empty() || result->tilemap.tilesets.empty()) { return out; }

	const render::TilesetConfig& tileset = result->tilemap.tilesets.front();
	const render::TilemapLayer&  layer   = result->tilemap.layers.front();
	out.width  = layer.width;
	out.height = layer.height;
	out.atlas  = render::TileAtlas{tileset.tileWidth, tileset.tileHeight, tileset.columns};
	out.indices.reserve(layer.data.size());
	for (const int gid : layer.data) { out.indices.push_back(gid == 0 ? -1 : gid - tileset.firstGid); }

	if (const auto* markers = result->findObjectLayer("markers"); markers != nullptr && !markers->objects.empty())
	{
		out.startX = markers->objects.front().x;
		out.startY = markers->objects.front().y;
	}
	return out;
}

const LoadedStage kStage = loadStage();

constexpr float kScrollSpeed = 420.0f;   // 見える範囲が動く速さ (1 秒あたりのピクセル)
constexpr float kScale       = 3.0f;     // 16 px のタイルを 48 px で描く
constexpr float kScreenW = 1280.0f, kScreenH = 720.0f;

}  // namespace

struct TiledMap07
{
	float        camX = 0.0f, camY = 0.0f;   // 画面の左上が指す、マップ上の位置 (ピクセル。タイルの大きさ 16 px の単位)
	std::uint8_t overview = 0;               // 1 = 全体の表示
	std::uint8_t placed   = 0;               // 1 = 開始位置へ置いた
	std::uint8_t _pad[2]  = {};              // 暗黙のパディングを残さない (状態をバイト単位で比べるため)

	void update(Input in, float dt)
	{
		if (placed == 0)
		{
			camX = kStage.startX - kScreenW * 0.5f / kScale;
			camY = kStage.startY - kScreenH * 0.5f / kScale;
			placed = 1;
		}
		const Stick m = in.move();
		camX += m.x * kScrollSpeed / kScale * dt;
		camY += m.y * kScrollSpeed / kScale * dt;
		if (in.pressed(Key::Space)) { overview = overview != 0 ? 0 : 1; }
	}

	void draw(Canvas& c) const
	{
		c.fillScreen(theme::kPaper);
		if (kStage.width > 0)
		{
			const float tw = static_cast<float>(kStage.atlas.tileW);
			const float th = static_cast<float>(kStage.atlas.tileH);
			if (overview != 0)
			{
				// 2 万マスをそのまま 1 命令で描く (1 マス = 6 x 6 px)
				c.drawTiles("tiles", kStage.atlas, kStage.indices.data(), kStage.width, kStage.height, 40.0f, 60.0f, 6.0f, 6.0f);
			}
			else
			{
				// 見えている範囲 (画面 1280 x 720 を 48 px のタイルで割った分に 1 マス足した数) だけを積む
				const int firstCol = static_cast<int>(std::floor(camX / tw));
				const int firstRow = static_cast<int>(std::floor(camY / th));
				const float x = (static_cast<float>(firstCol) * tw - camX) * kScale;
				const float y = (static_cast<float>(firstRow) * th - camY) * kScale;
				c.drawTiles("tiles", kStage.atlas, kStage.indices.data(), kStage.width, kStage.height,
				            firstCol, firstRow, 28, 16, x, y, tw * kScale, th * kScale);
			}
		}
		chapterTitle(c, "Tiled のマップ");   // タイルの上に重ねる
		chapterControls(c, "矢印キー: 見える範囲を動かす　Space: 全体の表示と切り替え");
	}
};

// 実行:  mitiru_host.exe tiled_map/tiled_map.dll
MITIRU_REFLECT_AUTO(TiledMap07);

MITIRU_ASSERT_NO_PADDING(TiledMap07);
MITIRU_GAME(TiledMap07);
