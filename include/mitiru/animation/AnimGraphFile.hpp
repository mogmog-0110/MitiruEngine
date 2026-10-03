#pragma once

/// @file AnimGraphFile.hpp
/// @brief グラフのファイル 1 つ。ゲーム DLL の static に置き、`asset.reloaded` が届いたら読み直す。
/// @details 読み直しで誤りがあれば前のグラフを残し、誤りを errors() に置く。実行時の状態 (AnimGraphState) は
///          GameMemory にあるので読み直しても消えず、次の stepAnimGraph が状態の名前でつなぎ直す。

#include <string>
#include <utility>
#include <vector>

#include <mitiru/animation/AnimGraphLoad.hpp>
#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/asset/AssetReload.hpp>

namespace mitiru::animation
{

/// @brief ツール窓 (anim) に渡す名前の表。グラフの JSON と同じ形 (params / layers.states / layers.transitions) に、
///        番号で引くイベント名 (events) と鍵 (key) を足したもの。遷移は調べる順 (priority の大きい順) に並ぶ
[[nodiscard]] inline std::string describeAnimGraph(const AnimGraph& graph)
{
	static constexpr const char* kTypes[] = {"float", "int", "bool", "trigger"};
	nlohmann::json params = nlohmann::json::array();
	for (const auto& p : graph.params) { params.push_back({{"name", p.name}, {"type", kTypes[static_cast<int>(p.type)]}}); }
	nlohmann::json layers = nlohmann::json::array();
	for (const auto& l : graph.layers)
	{
		nlohmann::json states = nlohmann::json::array();
		for (const auto& st : l.states) { states.push_back({{"name", st.name}}); }
		nlohmann::json transitions = nlohmann::json::array();
		for (const auto& t : l.transitions)
		{
			const std::string from = t.from < 0 ? std::string("*") : l.states[static_cast<std::size_t>(t.from)].name;
			transitions.push_back({{"from", from}, {"to", l.states[static_cast<std::size_t>(t.to)].name}, {"priority", t.priority}});
		}
		layers.push_back({{"name", l.name}, {"states", std::move(states)}, {"transitions", std::move(transitions)}});
	}
	return nlohmann::json{{"name", graph.name}, {"key", graph.key}, {"params", std::move(params)}, {"events", graph.eventNames},
	                      {"layers", std::move(layers)}}
	    .dump();
}

class AnimGraphFile
{
public:
	/// asset は同じモデルの AnimAsset。この AnimGraphFile より長く生きること (DLL の static に並べて置く)
	AnimGraphFile(std::string path, const AnimAsset& asset) : m_path(std::move(path)), m_asset(&asset) {}

	/// @brief 初めて呼んだときに読む。一度も読めていなければレイヤの無いグラフ (姿勢はレスト) を返す
	[[nodiscard]] const AnimGraph& get()
	{
		if (!m_loaded) { reload(); }
		return m_graph;
	}

	/// @brief payload (`in.actionPayload("asset.reloaded")`) がこのファイルなら読み直して true
	bool reloadIf(const char* reloadPayloadJson)
	{
		if (!asset::isReloadOf(reloadPayloadJson, m_path)) { return false; }
		reload();
		return true;
	}

	[[nodiscard]] const std::string& path() const noexcept { return m_path; }
	/// @brief 最後の読み込みの誤り。空なら最後の読み込みは通った
	[[nodiscard]] const std::vector<std::string>& errors() const noexcept { return m_errors; }
	/// @brief 今使っているグラフの名前の表 (describeAnimGraph)。ツール窓へ MITIRU_INSPECT_ASSETS で渡す
	[[nodiscard]] const std::string& description() const noexcept { return m_description; }

private:
	void reload()
	{
		m_loaded = true;
		const auto bytes = vfs::readGlobal(m_path);
		if (!bytes)
		{
			m_errors = {"animgraph: ファイルを読めない: " + m_path};
			return;
		}
		const std::string text(bytes->begin(), bytes->end());
		auto result = parseAnimGraph(text, *m_asset, animGraphStem(m_path));
		m_errors = std::move(result.errors);
		if (!result.graph) { return; }
		m_graph = std::move(*result.graph);
		m_description = describeAnimGraph(m_graph);
	}

	std::string m_path;
	const AnimAsset* m_asset;
	AnimGraph m_graph;
	std::string m_description;
	std::vector<std::string> m_errors;
	bool m_loaded = false;
};

} // namespace mitiru::animation
