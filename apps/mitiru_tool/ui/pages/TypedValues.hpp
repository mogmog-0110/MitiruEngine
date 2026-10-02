#pragma once
// gameMemory の JSON から、host が解いたエンジンの型 ("$type" を持つ object、observe/ReflectEngineTypes.hpp) を探す。
// ai / anim / nav のページが使う。値の持ち主は path の最後の "." より前で、同じ敵の BtState と知覚の記憶は
// 持ち主が同じになる ("e[0].bt" と "e[0].mind"、"squad.e[2].bt" と "squad.e[2].mind")。

#include "../SnapshotFormat.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mitiru::tool
{

struct TypedValue
{
	std::string path;              ///< "e[0].bt" / "squad.e[2].bt" / "agents[5]"
	std::string owner;             ///< path の最後の "." より前 (無ければ空)
	const Snapshot* value = nullptr;
};

/// snapshot の gameMemory.state (無ければ nullptr)。
[[nodiscard]] const Snapshot* gameMemoryState(const Snapshot& snap);

/// root を前順に歩き、"$type" が type の object を集める。見つけた object の中へは入らない。
[[nodiscard]] std::vector<TypedValue> findTyped(const Snapshot& root, std::string_view type);

/// 持ち主が owner の最初の値 (無ければ nullptr)。
[[nodiscard]] const TypedValue* findOwned(const std::vector<TypedValue>& values, std::string_view owner);

/// 持ち主が owner の値が list に 1 つだけか。持ち主の無い値 (根に直に置いた値) が 2 つ以上あると、
/// 同じ持ち主の知覚やイベントがどれの組か決められないので、組にしない。
[[nodiscard]] bool ownerIsUnique(const std::vector<TypedValue>& list, std::string_view owner);

/// 一覧に出す名前。持ち主があれば持ち主、無ければ path。
[[nodiscard]] std::string ownerLabel(const TypedValue& v);

/// ゲーム DLL が MITIRU_INSPECT_ASSETS で渡した資産 1 件 (host が snapshot の "assets" に置いたファイル)。
struct SnapshotAsset
{
	std::string name;
	std::string file;   ///< UTF-8 のパス
};

/// snapshot の "assets" のうち種類が kind のもの (渡した順)。
[[nodiscard]] std::vector<SnapshotAsset> snapshotAssets(const Snapshot& snap, std::string_view kind);

/// "a=1&tree=x.json" の key の値 (無ければ空)。
[[nodiscard]] std::string queryValue(std::string_view query, std::string_view key);

/// [x, y, z] を "(1.00, 0.00, -2.50)" の形の文字に。形が違えば全角のダッシュ 1 字。
[[nodiscard]] std::string vec3Text(const Snapshot* v);

} // namespace mitiru::tool
