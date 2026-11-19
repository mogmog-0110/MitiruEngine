#pragma once
// stage_edit のスキーマと同じく、エンジンが扱うのはゲームから渡される素の JSON だけ。形式は docs/TILEMAP_EDIT.md に記載。

#include "../stage_edit/EditSchema.hpp"

#include <string>
#include <vector>

namespace mitiru::tool::tilemapedit
{

struct TileKind
{
	int id = 0;              ///< 0 は空タイル
	std::string label;
	stageedit::Rgb color;
};

struct Tileset
{
	std::string image;       ///< 論理パス
	int tileWidth = 0;
	int tileHeight = 0;
	int firstId = 1;         ///< 画像の左上のマスの tileId。右、次の段の順に 1 ずつ増える
};

struct TileSchema
{
	std::string title;
	std::string dir;
	stageedit::SourceMap source;
	std::vector<std::string> exclude;
	Tileset tileset;
	std::vector<TileKind> tiles;     ///< tileset があるときは、同じ id の名前と色を上書きする
};

[[nodiscard]] bool parseTileSchema(const std::string& text, TileSchema& out, std::string& error);

} // namespace mitiru::tool::tilemapedit
