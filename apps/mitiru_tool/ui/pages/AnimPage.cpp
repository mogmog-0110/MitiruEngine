// anim。選んだモデルの姿勢の入力 (AnimPoseParams のレイヤ)、状態機械 (AnimGraphState) の今の状態とクロスフェードと
// param、このフレームに通ったアニメイベント、ルートモーションの差分を見る (読み取り専用)。どれも gameMemory の "$type"
// 付きの値で、同じ持ち主の値を組にする (例: "hero.pose" と "hero.graph"、"hero.events"、"hero.rootDelta")。
// 状態と param の名前は、DLL が MITIRU_INSPECT_ASSETS で渡したグラフの名前の表 (kind "animgraph") から鍵で引く。
// "--page anim?graph=<x.animgraph.json>" を渡せばそのファイルを優先し、書き換えられたら読み直す (DLL が渡す表は、host が
// asset.reloaded を受けた update の後に写し替えるので、--watch-assets で状態の名前を変えても付いてくる)。名前が無ければ番号で出す。
// 値の調整はここではしない。グラフの数は JSON を書き換えて --watch-assets で読み直し、ゲームの値は分岐エディタで試す。

#include "AnimGraphNames.hpp"
#include "Pages.hpp"
#include "TypedValues.hpp"

#include "../PageUtil.hpp"

#include <algorithm>

namespace mitiru::tool
{

namespace
{

nlohmann::json layerRow(const Snapshot& l)
{
	const double weight = numberOr(findAt(l, { "weight" }), 0.0);
	const double mask = numberOr(findAt(l, { "mask" }), -1.0);
	std::string flags = truthy(findAt(l, { "loop" })) ? "loop" : "once";
	if (truthy(findAt(l, { "additive" }))) { flags += " · add"; }
	return { { "clip", jsString(l.value("clip", Snapshot(-1))) },
	         { "mask", mask < 0.0 ? std::string("全身") : "mask " + jsString(l.value("mask", Snapshot(0))) },
	         { "time", toFixed(numberOr(findAt(l, { "time" }), 0.0), 2) + " s" },
	         { "weight", toFixed(weight, 2) }, { "w", toFixed(std::clamp(weight, 0.0, 1.0) * 100.0, 1) + "%" },
	         { "flags", flags } };
}

std::string nameAt(const std::vector<std::string>& names, int index)
{
	if (index < 0) { return {}; }
	return static_cast<std::size_t>(index) < names.size() ? names[static_cast<std::size_t>(index)] : "#" + std::to_string(index);
}

std::string paramValue(const Snapshot& v, const std::string& type)
{
	const double x = numberOr(&v, 0.0);
	if (type == "bool") { return x != 0.0 ? "true" : "false"; }
	if (type == "int") { return std::to_string(static_cast<long long>(x)); }
	if (type == "trigger") { return "\xE2\x80\x94"; }   // step の終わりに消えるので、窓に届く値は常に 0
	return toFixed(x, 2);
}

class AnimPage final : public ToolPage
{
public:
	explicit AnimPage(const PageContext& ctx) : m_view(ctx.view)
	{
		m_queryFile = queryValue(ctx.query, "graph");
		reloadQueryGraph();
	}

	void start() override { push(); }

	void onSnapshot(const Snapshot& snap, bool) override
	{
		m_snap = snap;
		reloadQueryGraph();
		loadAssetGraphs(snapshotAssets(snap, "animgraph"));
		push();
	}

	void onAction(std::string_view name, const nlohmann::json& payload) override
	{
		if (name != "anim.select") { return; }
		m_selected = payloadString(payload, "label");
		push();
	}

private:
	/// ?graph= のファイルが書き換えられた時だけ読み直す
	void reloadQueryGraph()
	{
		if (m_queryFile.empty()) { return; }
		const auto stamp = animGraphNamesStamp(m_queryFile);
		if (m_queryLoaded && stamp == m_queryStamp) { return; }
		m_queryLoaded = true;
		m_queryStamp = stamp;
		m_queryError = loadAnimGraphNames(m_queryFile, "", m_queryNames);
	}

	/// 資産のファイルの並びが変わった時だけ読み直す (snapshot は 10 Hz で届く)
	void loadAssetGraphs(const std::vector<SnapshotAsset>& assets)
	{
		std::string key;
		for (const SnapshotAsset& a : assets) { key += a.file + "\n"; }
		if (key == m_assetKey) { return; }
		m_assetKey = key;
		m_assetGraphs.clear();
		for (const SnapshotAsset& a : assets)
		{
			AnimGraphNames names;
			if (loadAnimGraphNames(a.file, a.name, names).empty()) { m_assetGraphs.push_back(std::move(names)); }
		}
	}

	const AnimGraphNames* namesFor(std::uint32_t key) const
	{
		if (!m_queryFile.empty()) { return m_queryError.empty() ? &m_queryNames : nullptr; }
		for (const AnimGraphNames& g : m_assetGraphs) { if (g.key == key) { return &g; } }
		return nullptr;
	}

	void push()
	{
		const Snapshot* state = gameMemoryState(m_snap);
		const auto poses = state != nullptr ? findTyped(*state, "mitiru.AnimPoseParams") : std::vector<TypedValue>{};
		const auto graphs = state != nullptr ? findTyped(*state, "mitiru.AnimGraphState") : std::vector<TypedValue>{};
		const std::string sel = pushList(poses, graphs);
		const TypedValue* pose = nullptr;
		const TypedValue* graph = nullptr;
		for (const TypedValue& p : poses) { if (pose == nullptr && ownerLabel(p) == sel) { pose = &p; } }
		for (const TypedValue& g : graphs) { if (graph == nullptr && ownerLabel(g) == sel) { graph = &g; } }
		m_view->set("ready", state != nullptr);
		m_view->set("no_poses", state != nullptr && poses.empty() && graphs.empty());
		m_view->set("has_sel", !sel.empty());
		m_view->set("sel_label", sel);
		m_view->set("has_pose", pose != nullptr);
		pushLayers(pose);
		pushGraph(graph);
		const TypedValue* linked = pose != nullptr && ownerIsUnique(poses, pose->owner) ? pose
		                           : graph != nullptr && ownerIsUnique(graphs, graph->owner) ? graph : nullptr;
		pushEvents(state, linked);
		pushRoot(state, linked, graph);
	}

	/// 姿勢と状態機械の持ち主を 1 行ずつ並べ、選んでいる持ち主の名前を返す
	std::string pushList(const std::vector<TypedValue>& poses, const std::vector<TypedValue>& graphs)
	{
		std::vector<std::string> labels;
		nlohmann::json rows = nlohmann::json::array();
		const auto add = [&](const TypedValue& v, const std::string& note) {
			const std::string label = ownerLabel(v);
			if (std::find(labels.begin(), labels.end(), label) != labels.end()) { return; }
			labels.push_back(label);
			rows.push_back({ { "label", label }, { "n", note } });
		};
		for (const TypedValue& p : poses)
		{
			const Snapshot* layers = findAt(*p.value, { "layers" });
			add(p, std::to_string(layers != nullptr && layers->is_array() ? layers->size() : 0) + " レイヤ");
		}
		for (const TypedValue& g : graphs)
		{
			const AnimGraphNames* names = namesFor(static_cast<std::uint32_t>(numberOr(findAt(*g.value, { "graph" }), 0.0)));
			add(g, "グラフ " + (names != nullptr ? names->name : std::string("?")));
		}
		const bool known = std::find(labels.begin(), labels.end(), m_selected) != labels.end();
		const std::string sel = known ? m_selected : (labels.empty() ? std::string() : labels.front());
		for (auto& r : rows) { r["selected"] = r["label"] == sel; }
		m_view->set("poses", std::move(rows));
		return sel;
	}

	void pushLayers(const TypedValue* sel)
	{
		nlohmann::json rows = nlohmann::json::array();
		const Snapshot* layers = sel != nullptr ? findAt(*sel->value, { "layers" }) : nullptr;
		if (layers != nullptr && layers->is_array()) { for (const Snapshot& l : *layers) { rows.push_back(layerRow(l)); } }
		m_view->set("in_place", sel != nullptr && truthy(findAt(*sel->value, { "inPlace" })));
		m_view->set("layers", std::move(rows));
	}

	nlohmann::json graphLayerRows(const Snapshot& g, const AnimGraphNames* names) const
	{
		nlohmann::json rows = nlohmann::json::array();
		const Snapshot* layers = findAt(g, { "layers" });
		for (std::size_t i = 0; layers != nullptr && layers->is_array() && i < layers->size(); ++i)
		{
			const Snapshot& l = (*layers)[i];
			const AnimGraphNames::Layer* def = names != nullptr && i < names->layers.size() ? &names->layers[i] : nullptr;
			const std::vector<std::string> none;
			const auto& states = def != nullptr ? def->states : none;
			const int from = static_cast<int>(numberOr(findAt(l, { "from" }), -1.0));
			const int frozen = static_cast<int>(numberOr(findAt(l, { "frozen" }), 0.0));
			const bool fading = truthy(findAt(l, { "fading" }));
			const double fade = numberOr(findAt(l, { "fade" }), 1.0);
			const int t = static_cast<int>(numberOr(findAt(l, { "transition" }), -1.0));
			const bool knownT = def != nullptr && t >= 0 && static_cast<std::size_t>(t) < def->transitions.size();
			rows.push_back({ { "name", def != nullptr ? def->name : "layer" + std::to_string(i) },
			                 { "state", nameAt(states, static_cast<int>(numberOr(findAt(l, { "state" }), -1.0))) },
			                 { "from", !fading ? std::string() : from >= 0 ? nameAt(states, from) : "止めた姿勢 " + std::to_string(frozen) },
			                 { "fading", fading }, { "fade", toFixed(fade, 2) }, { "fade_w", toFixed(std::clamp(fade, 0.0, 1.0) * 100.0, 1) + "%" },
			                 { "phase", toFixed(numberOr(findAt(l, { "phase" }), 0.0), 2) },
			                 { "time", toFixed(numberOr(findAt(l, { "time" }), 0.0), 2) + " s" },
			                 { "via", knownT ? def->transitions[static_cast<std::size_t>(t)].from + " \xE2\x86\x92 " +
			                                       def->transitions[static_cast<std::size_t>(t)].to
			                                 : std::string() } });
		}
		return rows;
	}

	nlohmann::json graphParamRows(const Snapshot& g, const AnimGraphNames* names) const
	{
		nlohmann::json rows = nlohmann::json::array();
		const Snapshot* params = findAt(g, { "params" });
		if (params == nullptr || !params->is_array()) { return rows; }
		for (std::size_t i = 0; i < params->size(); ++i)
		{
			const bool named = names != nullptr && i < names->params.size();
			if (!named && numberOr(&(*params)[i], 0.0) == 0.0) { continue; }   // 名前の無い 0 は使っていない欄
			const std::string type = named ? names->params[i].type : std::string("float");
			rows.push_back({ { "name", named ? names->params[i].name : "#" + std::to_string(i) }, { "type", type },
			                 { "value", paramValue((*params)[i], type) } });
		}
		return rows;
	}

	nlohmann::json graphEventRows(const Snapshot& g, const AnimGraphNames* names) const
	{
		nlohmann::json rows = nlohmann::json::array();
		const Snapshot* events = findAt(g, { "events" });
		const std::vector<std::string> none;
		for (std::size_t i = 0; events != nullptr && events->is_array() && i < events->size(); ++i)
		{
			const Snapshot& e = (*events)[i];
			const int clip = static_cast<int>(numberOr(findAt(e, { "clip" }), -1.0));
			rows.push_back({ { "name", nameAt(names != nullptr ? names->events : none, static_cast<int>(numberOr(findAt(e, { "name" }), 0.0))) },
			                 { "clip", clip < 0 ? std::string("状態") : "clip " + std::to_string(clip) } });
		}
		return rows;
	}

	void pushGraph(const TypedValue* graph)
	{
		const Snapshot empty = Snapshot::object();
		const Snapshot& g = graph != nullptr ? *graph->value : empty;
		const auto key = static_cast<std::uint32_t>(numberOr(findAt(g, { "graph" }), 0.0));
		const AnimGraphNames* names = graph != nullptr ? namesFor(key) : nullptr;
		m_view->set("has_graph", graph != nullptr);
		m_view->set("graph_name", names != nullptr ? names->name : std::string("?"));
		m_view->set("graph_error", m_queryError);
		m_view->set("graph_unnamed", graph != nullptr && names == nullptr);
		m_view->set("graph_layers", graphLayerRows(g, names));
		m_view->set("graph_params", graphParamRows(g, names));
		auto events = graphEventRows(g, names);
		m_view->set("no_graph_events", events.empty());
		m_view->set("graph_events", std::move(events));
	}

	void pushEvents(const Snapshot* state, const TypedValue* sel)
	{
		nlohmann::json rows = nlohmann::json::array();
		if (state != nullptr && sel != nullptr)
		{
			for (const TypedValue& e : findTyped(*state, "mitiru.AnimEventHit"))
			{
				if (e.owner != sel->owner) { continue; }
				rows.push_back({ { "name", jsString(e.value->value("name", Snapshot(0))) },
				                 { "clip", jsString(e.value->value("clip", Snapshot(-1))) },
				                 { "time", toFixed(numberOr(findAt(*e.value, { "time" }), 0.0), 3) + " s" } });
			}
		}
		m_view->set("no_events", rows.empty());
		m_view->set("events", std::move(rows));
	}

	/// 同じ持ち主の YawXform が無ければ、状態機械が出したこの step のルートモーションを出す
	void pushRoot(const Snapshot* state, const TypedValue* sel, const TypedValue* graph)
	{
		const auto deltas = state != nullptr ? findTyped(*state, "mitiru.YawXform") : std::vector<TypedValue>{};
		const TypedValue* d = sel != nullptr ? findOwned(deltas, sel->owner) : nullptr;
		const Snapshot* root = d != nullptr ? d->value : (graph != nullptr ? findAt(*graph->value, { "root" }) : nullptr);
		m_view->set("has_root", root != nullptr);
		m_view->set("root_t", root != nullptr ? vec3Text(findAt(*root, { "t" })) + " m" : std::string());
		m_view->set("root_yaw", root != nullptr ? toFixed(numberOr(findAt(*root, { "yawDeg" }), 0.0), 2) + "\xC2\xB0" : std::string());
	}

	ToolView* m_view;
	Snapshot m_snap = Snapshot::object();
	std::string m_selected;
	std::string m_queryFile;
	std::string m_queryError;
	AnimGraphNames m_queryNames;
	std::filesystem::file_time_type m_queryStamp{};
	bool m_queryLoaded = false;
	std::string m_assetKey;
	std::vector<AnimGraphNames> m_assetGraphs;
};

} // namespace

std::unique_ptr<ToolPage> makeAnimPage(const PageContext& ctx)
{
	return std::make_unique<AnimPage>(ctx);
}

} // namespace mitiru::tool
