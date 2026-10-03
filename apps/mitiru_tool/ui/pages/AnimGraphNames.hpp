#pragma once
// anim ページが状態機械の番号を名前にするための表。DLL が MITIRU_INSPECT_ASSETS で渡す describeAnimGraph の JSON か、
// "--page anim?graph=<x.animgraph.json>" で渡したグラフの JSON そのものから読む。どちらも params / layers.states /
// layers.transitions の形は同じで、遷移は priority の大きい順 (同じなら書いた順) に並べ直してグラフの番号と合わせる。

#include "../SnapshotFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mitiru::tool
{

struct AnimGraphNames
{
	struct Param
	{
		std::string name;
		std::string type;   ///< float / int / bool / trigger
	};
	struct Transition
	{
		std::string from;   ///< "*" はどの状態からでも
		std::string to;
	};
	struct Layer
	{
		std::string name;
		std::vector<std::string> states;
		std::vector<Transition> transitions;
	};

	std::string name;
	std::uint32_t key = 0;
	std::vector<Param> params;
	std::vector<Layer> layers;
	std::vector<std::string> events;   ///< describeAnimGraph のときだけ入る
};

/// JSON から読む。"name" が無ければ fallbackName をグラフの名前にする。形が違えば error に理由を書いて false。
bool readAnimGraphNames(const Snapshot& json, const std::string& fallbackName, AnimGraphNames& out, std::string& error);

/// ファイルから読む (相対パスは作業フォルダ、次に MITIRU_ASSET_ROOT)。読めなければ理由を返す。
[[nodiscard]] std::string loadAnimGraphNames(const std::string& path, const std::string& fallbackName, AnimGraphNames& out);

/// loadAnimGraphNames が読むファイルの更新時刻 (無ければ既定値)。書き換えを見て読み直すのに使う。
[[nodiscard]] std::filesystem::file_time_type animGraphNamesStamp(const std::string& path);

} // namespace mitiru::tool
