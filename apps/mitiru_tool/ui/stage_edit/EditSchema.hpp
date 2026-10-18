#pragma once
// stage_edit の編集スキーマ。ゲームが「どんな種類の物が、どの欄を持ち、どんな gizmo で動かせるか」を書いた JSON を読む。
// エンジンはゲームのステージの形式を知らない。種類の名前、JSON の中の位置、色、欄の型はすべてこのファイルから来る。
// 書き方は docs/STAGE_EDIT.md。

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace mitiru::tool::stageedit
{

/// 図の上で物をどう扱うか。数値の置き場 (JSON の鍵) は Gizmo::keys が決める
enum class Shape
{
	Box,           ///< min / max の 2 点で決まる軸に沿った箱
	OrientedBox,   ///< center (底の中心) / size (幅, 高さ, 長さ) / yaw で決まる、向きのある箱
	Point,         ///< at の 1 点。yaw と radius があれば向きと半径も持つ
	Segment,       ///< from / to の線分
	Polyline,      ///< points の折れ線
	Wall,          ///< from / to ([x, z] の 2 点) と bottom / top の高さ
};

enum class FieldType { Vec3, Vec2, Number, Angle, Bool, Text, Color };

struct Rgb
{
	unsigned char r = 128;
	unsigned char g = 128;
	unsigned char b = 128;
};

struct Gizmo
{
	Shape shape = Shape::Point;
	/// 役割 ("min" "max" "center" "size" "yaw" "at" "radius" "from" "to" "points" "bottom" "top" "count") -> JSON の鍵。
	/// "at" の鍵が空なら、要素そのものが [x, y, z] の配列
	std::map<std::string, std::string> keys;

	[[nodiscard]] const std::string* key(const std::string& role) const
	{
		const auto it = keys.find(role);
		return it == keys.end() ? nullptr : &it->second;
	}
};

struct Field
{
	std::string key;
	std::string label;
	FieldType type = FieldType::Number;
	bool optional = false;    ///< 無くてよい欄。パネルで足したり消したりできる
	double step = 0.0;        ///< 0 ならスナップの既定
	double minValue = -1.0e9;
	double maxValue = 1.0e9;
	nlohmann::ordered_json fallback;   ///< 足したときの値。null なら型の既定
};

/// 同じ配列に違う形の要素が混ざるとき (拾う物の 1 個と並び、掛け所の線分と点) の見分け方
struct Match
{
	std::string hasKey;       ///< この鍵を持つオブジェクト
	std::string lacksKey;     ///< この鍵を持たないオブジェクト
	bool isArray = false;     ///< 要素そのものが配列
	bool any = true;          ///< 条件なし
};

struct Kind
{
	std::string id;
	std::string label;
	std::string path;         ///< ステージの JSON の中の鍵。配列なら要素が物、single ならその値が物
	bool single = false;
	Match match;
	Gizmo gizmo;
	Rgb color;
	std::vector<Field> fields;
	nlohmann::ordered_json fresh;     ///< パレットから足すときの中身
	bool inlineStyle = true;          ///< 新しい要素を 1 行で書くか (false は 1 要素 1 行に展開)
};

struct Snap
{
	double move = 0.5;
	double size = 0.5;
	double height = 0.5;
	double angle = 15.0;
};

struct SourceMap
{
	std::string logicalPrefix;   ///< ゲームが読む論理パスの頭 ("ooze_drive/assets/")
	std::string diskPrefix;      ///< プロジェクトの中の元ファイルの頭 ("assets/")
	[[nodiscard]] bool valid() const noexcept { return !logicalPrefix.empty() && !diskPrefix.empty(); }
};

/// 真上の図の向き。右が +x か -x か、上が +z か -z か
struct TopOrientation
{
	bool rightIsNegX = false;
	bool upIsNegZ = false;
};

struct Schema
{
	std::string title;
	std::string dir;                              ///< ステージのファイルを置く論理パス ("ooze_drive/assets/stages")
	SourceMap source;
	Snap snap;
	TopOrientation top;
	std::map<std::string, std::string> generated; ///< ファイル名 -> そのファイルを作るスクリプト
	std::vector<std::string> exclude;             ///< dir の中の JSON のうち、ステージではないファイル名
	std::vector<Kind> kinds;
};

/// @return 読めなければ false。error に直す場所つきの文
[[nodiscard]] bool parseSchema(const std::string& text, Schema& out, std::string& error);

} // namespace mitiru::tool::stageedit
