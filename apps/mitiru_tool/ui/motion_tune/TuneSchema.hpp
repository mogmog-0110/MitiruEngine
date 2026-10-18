#pragma once
// motion_tune が読む調整値のスキーマ宣言。スキーマは素の JSON で、GameMemory への対応はゲームが渡す。エンジンはゲームの調整値の形式を知らない。
// source          {"logical": 論理パスの先頭, "disk": プロジェクト内のパスの先頭}。元ファイルを探す対応は stage_edit と同じ
// trace           軌跡に使う GameMemory の欄。x、y、z は必須、speed は任意。流し直す秒数も持つ
// favorites       「よく使う」に出す GameMemory の欄の名前
// files[]         調整値のファイル。path はゲームが読む論理パス、prefix は GameMemory の欄名の先頭
// files[].groups[] ファイル内の節。key が空なら最上位。欄名は <prefix>_[<節>_]<欄>
// fields[]        key、hint (欄の意味)、kind (real / whole / flag)、min、max

#include "../stage_edit/EditSchema.hpp"

#include <string>
#include <vector>

namespace mitiru::tool::motiontune
{

struct Field
{
	std::string key;
	std::string hint;
	std::string kind = "real";   ///< real / whole / flag
	double min = 0.0;
	double max = 1.0;
};

struct Group
{
	std::string key;     ///< JSON の節の鍵。空なら最上位
	std::string label;
	std::vector<Field> fields;
};

struct FileDecl
{
	std::string id;
	std::string label;
	std::string path;
	std::string prefix;
	std::vector<Group> groups;
};

struct TraceDecl
{
	std::string x;
	std::string y;
	std::string z;
	std::string speed;
	double seconds = 3.0;
};

struct TuneSchema
{
	stageedit::SourceMap source;
	TraceDecl trace;
	std::vector<std::string> favorites;
	std::vector<FileDecl> files;
};

[[nodiscard]] bool parseTuneSchema(const std::string& text, TuneSchema& out, std::string& error);

/// GameMemory の欄名は <prefix>_[<節>_]<欄>
[[nodiscard]] std::string memoryName(const FileDecl& file, const Group& group, const Field& field);

/// JSON 内の位置は "節/欄" または "欄"
[[nodiscard]] std::string jsonPointer(const Group& group, const Field& field);

} // namespace mitiru::tool::motiontune
